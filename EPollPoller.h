#ifndef _EPOLLPOLLER_H_
#define _EPOLLPOLLER_H_

#include <vector>
#include <sys/epoll.h>

#include "Poller.h"
#include "Timestamp.h"

/*
epoll的使用
1. epoll_create
2. epoll_ctl        add/mod/del
3. epoll_wait
*/

class Channel;

class EPollPoller : public Poller
{
public:
    EPollPoller(EventLoop *loop);
    ~EPollPoller() override;

    // 重写基类的抽象方法
    Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
    void updateChannel(Channel *channel) override;
    void removeChannel(Channel *channel) override;
private:
    static const int kInitEventListSize = 16;

    // 填写活跃的连接
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;
    // 更新channel通道
    void update(int operation, Channel *channel);

    using EventList = std::vector<epoll_event>;

    int epollfd_;
    EventList events_;
};

#endif