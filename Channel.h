#ifndef _CHANNEL_H_
#define _CHANNEL_H_

#include <functional>
#include <memory>

#include "noncopyable.h"
#include "Timestamp.h"

class EventLoop;    // 只使用对象类型，做对象类型前置声明

/*
EventLoop、Channel、Poller之间的关系  <=> Reactor模型上对应Demultiplex
Channel 理解为通道，封装了sockfd和其感兴趣的event，如EPOLLIN、EPOLLOUT事件
绑定了polle返回的具体事件
*/
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    // 处理事件，调用相应的回调方法(fd得到poller的通知之后)
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止当Channel被手动remove掉，channel还在执行回调
    void tie(const std::shared_ptr<void>&);

    // 
    int fd() const { return fd_; }
    int events() const { return events_;}
    void set_revents(int revt) { revents_ = revt; }

    // 将Channel中的文件描述符及其感兴趣的事件注册到事件监听器上, 或从事件监听器上移除
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }

    // 返回fd当前的事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const { return events_ & kReadEvent; }


    int index() { return index_; }
    void set_index(int index) { index_ = index; }

    /*
    onerLoop表示当前这个Channel属于哪个EventLoop
    一个线程有一个EventLoop, 一个EventLoop有一个Poller, 一个Poller可以监听很多个Channel
    */
    // one loop per thread
    EventLoop* ownerLoop() { return loop_; }
    void remove();
private:
    void update();                  // 本质调用epoll_ctl()
    void handlerEventWithGuard(Timestamp receiveTime);
    
    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_;               // 事件循环
    const int fd_;                  // fd, Poller监听的对象
    int events_;                    // 注册fd感兴趣的事件
    int revents_;                   // poller返回的具体发生事件
    int index_;

    std::weak_ptr<void> tie_;
    bool tied_;

    // 因为Channel通道里面能够获得fd最终发生的具体事件revents，所以它负责调用具体事件的回调操作
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

#endif