#include "EPollPoller.h"

#include <errno.h>
#include <unistd.h>
#include <strings.h>

#include "Logger.h"
#include "Channel.h"

// channel 未添加到poller中
const int kNew = -1;        // Channel类成员index_= -1
// channel 已经添加到poller中
const int kAdded = 1;
// channel 从poller中删除
const int kDeleted = 2;

EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    , events_(kInitEventListSize)       // vector<epoll_event>
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}

Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    // 由于频繁调用epoll, 实际上应该用LOG_DEBUG输出日志更为合理, 当遇到并发场景 关闭DEBUG日志提升效率
    LOG_DEBUG("func%s => fd total count:%d\n", __FUNCTION__, channels_.size());
    //LOG_INFO("func%s => fd total count:%lu\n", __FUNCTION__ ,channels_.size());
    
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int savedErrno = errno;     // 把当前loop发生的errno暂存下来
    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        // LOG_DEBUG("%d events happened \n", numEvents);
        LOG_INFO("%d events happened \n", numEvents);
        fillActiveChannels(numEvents, activeChannels);
        if (numEvents == events_.size())    // 扩容操作
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("%s timeout", __FUNCTION__);
    }
    else 
    {
        if (savedErrno != EINTR)
        {
            errno = savedErrno;
            LOG_ERROR("EPollPoller::poll() err!");
        }
    }
    return now;
}

// 重写基类Poller的抽象方法
/*
 * Channel update remove => EventLoop updateChannel removeChannel
 * 
 *                  EventLoop   poller.poll
 *  ChannelList                 Poller
 *                              ChannelMap  <fd, channel*>
*/
void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();
    LOG_INFO("func=%s => fd=%d events=%d index=%d \n", __FUNCTION__, channel->fd(), channel->events(), index);
    if (index == kNew || index == kDeleted)
    {
        if (index == kNew)
        {
            int fd = channel->fd();
            channels_[fd] = channel;
        }
        else   // index == kDeleted
        {}
        
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    // 表示channel已经在poller上注册过
    else
    {
        int fd = channel->fd();
        if(channel->isNoneEvent())
        {
           update(EPOLL_CTL_DEL, channel);
           channel->set_index(kDeleted);
        }
        else 
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

// 从poller中删除Channel
void EPollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    channels_.erase(fd);

    LOG_INFO("func=%s => fd=%d \n", __FUNCTION__, fd);

    int index = channel->index();
    if (index == kDeleted)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}



// 填写活跃的连接
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);         // EventLoop就拿到了它的poller给它返回的所有发生事件的channel列表了
    }
}

// 更新channel通道  epoll_ctl  add/mod/del
void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
    bzero(&event, sizeof event);

    int fd = channel->fd();

    event.events = channel->events();
    event.data.fd = fd;
    event.data.ptr = channel;

    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error:%d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
        }
    }
}