#include "Accpetor.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

#include "Logger.h"
#include "InetAddress.h"

static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d listen socket create err:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd;
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblocking())
    , acceptChannel_(loop, acceptSocket_.fd())
    , listenning_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    acceptSocket_.bindAddress(listenAddr);      // bind
    // TcpServer::start()  Acceptor.listen 有新用户的连,接 要执行一个回调(connfd->channel->subloop)
    // baseLoop -> acceptChannel_(listenfd)=>
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    // 把从Poller中感兴趣的事件删除
    acceptChannel_.disableAll();
    // 调用EventLoop->removeChannel  ->  Poller->removeChannel 把Poller的ChannelMap对应的部分删除
    acceptChannel_.remove();
}

// 监听本地端口
void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen();             // listen 
    acceptChannel_.enableReading();     // acceptChannel_ => Poller acceptChannel_注册至Poller

}

// listenfd有事件发生了, 就是有新用户连接了
void Acceptor::handleRead()
{
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (NewConnectionCallback_)
        {
            NewConnectionCallback_(connfd, peerAddr);       // 轮询找到subLoop, 唤醒, 分发当前的新客户端的Channel
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d accept err:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
        if (errno == EMFILE)
        {
            LOG_ERROR("%s:%s:%d sockfd reached limit err:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
        }
    }
}