#include "EventLoop.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

// 防止一个线程创建多个EventLoop
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;

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
    wakeupChannel_->disableAll();
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
         * IO线程 mainLoop accept接收新用户的链接 fd <= channel 分发给subLoop
         * mainLoop 事先注册一个回调cb (需要subloop执行)  wakeup  subLoop后, 执行下面的方法, 执行之前mainLoop注册的doPendingFunctors
         */
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping", this);
}

/*
 * 1. loop在自己的线程中调用quit
 *              mainLoop
 * 
 * no ============= 生产者-消费真的线程安全队列 =============
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

    // 唤醒相应的, 执行上面回调操作的loop线程
    // callingPendingFunctors_: 当前loop正在执行回调, 但是又有了新的回调
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

void EventLoop::hasChannel(Channel *channel)
{
    poller_->hasChannel(channel);
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
        functors.swap(pendingFunctors_);
    }

    for (auto functor : functors)
    {
        // 执行当前loop需要执行的回调操作
        functor();
    }

    callingPendingFunctors_ = false;
}