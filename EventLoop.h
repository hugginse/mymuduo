#ifndef _EVENTLOOP_H_
#define _EVENTLOOP_H_

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>


#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"


class Channel;
class Poller;

// 事件循环类   主要包含了两个大模块 Channel  Poller(epoll抽象)
class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    // 开启事件循环
    void loop();
    // 退出事件循环
    void quit();

    Timestamp pollReturnTime() const { return pollReturnTime_; }

    // 在当前loop中执行
    void runInLoop(Functor cb);
    // 把上层注册的回调函数cb放入队列中, 唤醒loop所在的线程, 执行c
    void queueInLoop(Functor cb);

    // 用来唤醒loop所在的线程
    void wakeup();

    // EventLoop的方法 => Poller的方法
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);

    // 判断EventLoop对象是否在自己的线程里面
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
private:
    // wake up
    void handleRead();
    // 执行回调
    void doPendingFunctors();

    using ChannelList = std::vector<Channel*>;

    std::atomic_bool looping_;                      // 原子操作，通过CAS实现
    std::atomic_bool quit_;                         // 标志退出loop循环

    const pid_t threadId_;                          // 记录当前EventLoop是被哪个线程id创建, 即表示了当前EventLoop的所属线程id
                                                    
    Timestamp pollReturnTime_;                      // poller返回发生时间的channel的时间点
    std::unique_ptr<Poller> poller_;                
    
    int wakeupFd_;                                  // 主要作用: 当mainLoop获取一个新用户的channel，通过轮询算法选择subloop, 通过该成员唤醒subloop处理事件
    std::unique_ptr<Channel> wakeupChannel_;        

    ChannelList activeChannels_;                    // 返回Poller检测到当前有事件发生的所有Channel列表

    std::atomic_bool callingPendingFunctors_;       // 标识当前loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_;          // 存储loop需要执行的所有回调操作
    std::mutex mutex_;                              // 互斥锁, 用来保护上面vector容器的线程安全操作
};

#endif 