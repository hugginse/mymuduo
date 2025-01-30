#include "Channel.h"

#include <sys/epoll.h>

#include "EventLoop.h"
#include "Logger.h"

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , index_(-1)
    , tied_(false)
{ 
}

Channel::~Channel()
{
}

// Channel::tie_方法在什么时候调用过?
void Channel::tie(const std::shared_ptr<void>& obj)
{
    tie_ = obj;
    tied_ = true;
}

/*
当改变Channel所表示的fd的events事件后，需要update负责在poller里面更改fd相应的事件epoll_clt
EventLoop => ChannelList  Poller
*/
void Channel::update()
{
    // 通过Channel所属的EventLoop，调用poller的相应方法，注册fd的events事件
    loop_->updateChannel(this);
}

void Channel::remove()
{
    // 在Channel所属的EventLoop中，把当前二点Channel删除掉
    loop_->removeChannel(this);
}

// fd得到poller通知以后, 处理事件
void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handlerEventWithGuard(receiveTime);
        }
    }
    else
    {
        handlerEventWithGuard(receiveTime);
    }
}

// 根据poller通知的Channel发生的具体事件，由Channel负责调用具体的回调操作
void Channel::handlerEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("Channel handleEvent revents:%d", revents_);

    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (closeCallback_)
        {
            closeCallback_();
        }
    }

    if (revents_ & EPOLLERR)
    {
        if (errorCallback_)
        {
            errorCallback_();
        }
    }

    if (revents_ & (EPOLLIN | EPOLLPRI))
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }

    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
        }
    }
}