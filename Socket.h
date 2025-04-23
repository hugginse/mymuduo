#ifndef _SOCKET_H_
#define _SOCKET_H_

#include "noncopyable.h"

class InetAddress;

// 封装socket fd
class Socket : noncopyable
{
public:
    explicit Socket(int sockfd)
        : sockfd_(sockfd)
    {}

    ~Socket();

    int fd() const { return sockfd_; }
    void bindAddress(const InetAddress &localaddr);
    void listen();
    int accept(InetAddress *peeraddr);

    void shutdownWrite();
    // 设置Nagel算法
    void setTcpNoDelay(bool on);
    // 设置地址复用
    void setReuseAddr(bool on);
    // 设置端口复用
    void setReusePort(bool on);
    // 设置长连接
    void setKeepAlive(bool on);

private:
    const int sockfd_;
};

#endif 