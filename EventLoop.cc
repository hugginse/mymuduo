#include "EventLoop.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

// 防止一个线程创建多个EventLoop  __thread <==> thread_local
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;  // 10000毫秒 = 10 秒钟
                                
/*
 * 创建线程之后主线程和子线程谁先运行是不确定的
 * 通过一个eventfd在线程之间传递数据的好处是多个线程无需上锁就可以实现同步
 * eventfd支持的最低内核版本为Linux 2.6.27, 在2.6.26之前的版本也可以使用eventfd, 但是flag必须设置为0
 * 函数原型
 *      #include <sys/eventfd.h>
 *      int eventfd(unsigned int initval, int flags);
 * 参数说明
 *      initval: 初始化计算器的值
 *      flags, EFD_NONBLOCK, 设置socket为非阻塞
 *             EFD_CLOEXEC, 执行fork的时候, 在父进程中的描述符会自动关闭, 子进程中的描述符保留
 * 场景：
 *      eventfd可以用于同一个进程中线程间的通信
 *      eventfd还可以用于同亲缘关系的进程之间的通信
 *      eventfd用于不同亲缘关系的进程间通信, 需要把eventfd放在几个进程的共享内存中
*/

// 创建wakeupfd, 用来notify唤醒subReactor处理新来的channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
{
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d \n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }

    // 设置wakeupfd的事件类型, 以及发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个EventLoop都将监听wakeupchannel的EPOllIN读事件了
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    // 给Channel移除所有感兴趣的事件
    wakeupChannel_->disableAll();       
    // 把Channel从EventLoop上删除掉
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    LOG_INFO("EventLoop %p start looping \n", this);

    while (!quit_)
    {
        activeChannels_.clear();
        // 监听两类fd  一种是clientfd, 一种是wakeupfd
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            // Poller监听哪些channel发生事件了, 然后上报给EventLoop, 通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }

        // 执行当前EventLoop事件循环需要处理的回调操作
        /*
         * IO线程 mainLoop accept接收新用户的链接 fd <== channel 分发给subLoop
         * mainLoop 事先注册一个回调cb (需要subloop执行)  wakeup  subLoop后, 执行下面的方法, 执行之前mainLoop注册的doPendingFunctors
         */
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping\n", this);
    looping_ = false;
}

/*
 * 退出事件循环
 * 1. loop在自己的线程中调用quit, 说明当前线程已经执行完毕了loop()函数的poller_->poll并退出
 * 2. 如果不是当前EventLoop所属线程中调用quit退出EventLoop 需要唤醒EventLoop所属线程的epoll_wait
 *
 * 比如在一个subloop(worker)中调用mainloop(IO)的quit时, 需要唤醒mainloop(IO)的poller_->poll 让其执行完loop()函数
 
 * 
 * no ============= 生产者-消费真的线程安全队列 =============
 * 
 *             mainLoop
 * 
 *  subLoop1    subLoop2     subLoop3 
 */
// 退出事件循环
void EventLoop::quit()
{
    quit_ = true;

    // 如果在其他线程中调用quit, 在一个subLoop(worker)中调用了mainLoop(IO)的quit
    if (!isInLoopThread())        
    {
        wakeup();
    }
}

// 在当前loop执行cb
void EventLoop::runInLoop(Functor cb)
{
    // 在当前的loop线程中执行cb
    if (isInLoopThread())
    {
        cb();
    }
    // 在非当前线程中执行cb, 需要唤醒loop所在线程执行cb
    else
    {
        queueInLoop(std::move(cb));
    }
}

// 把cb放入队列中, 唤醒loop所在的线程, 执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }

    /*
     * 唤醒相应的, 执行上面回调操作的loop线程
     * || callingPendingFunctors_: 当前loop正在执行回调, 但是loop又有了新的回调, 需要通过wakeup写事件
     * 唤醒相应的需要执行回调操作的loop线程, 让loop()下一个poller_->poll()不再阻塞(阻塞的话会延迟前一次新加入的回调的执行),
     * 然后继续执行pendingFunctors_中的回调函数
    */
    if ( !isInLoopThread() || callingPendingFunctors_)      
    {
        // 唤醒loop所在线程
        wakeup();
    }
}

// 用来唤醒loop所在的线程  向wakeupfd写一个数据 wakeupChannel就发生读事件, 当前loop线程就会被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
    }
}

// EventLoop的方法 => Poller的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8", n);
    }
}

// 执行回调
void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 交换的方式减少了锁的临界区范围, 提升效率, 同时避免了死锁, 如果执行functor()在临界区内, 且functor()中调用queueInLoop()就会产生死锁
        functors.swap(pendingFunctors_);
    }

    for (auto functor : functors)
    {
        // 执行当前loop需要执行的回调操作
        functor();
    }

    callingPendingFunctors_ = false;
}