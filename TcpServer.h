#ifndef _TCPSERVER_H_
#define _TCPSERVER_H_

/*
 * 用户使用muduo编写服务器程序
 * 
*/

#include <functional>
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>

#include "EventLoop.h"
#include "Accpetor.h"
#include "InetAddress.h"
#include "noncopyable.h"
#include "EventLoopThreadPool.h"
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"

// 对外的服务器编程使用的类
class TcpServer : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    enum Option
    {
        kNoReusePort,       // 不允许重用本地端口
        kReusePort,         // 允许重用本地端口
    };

    TcpServer(EventLoop *loop,
                const InetAddress &listenAddr,
                const std::string &nameArg,
                Option option = kNoReusePort);

    ~TcpServer();

    void setThreadIinitCallback(const ThreadInitCallback &cb) { threadInitCallback_ = cb; }
    void setConnectonCallback(const ConnectionCallback &cb) { connectionCallback_ = cb;}
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }

    // 设置底层subloop的个数
    void setThreadNum(int numThreads);

    // 开启服务器监听
    void start();

private:
    void newConnection(int sockfd, const InetAddress &peerAddr);
    void removeConnection(const TcpConnectionPtr &conn);
    void removeConnectionInLoop(const TcpConnectionPtr &conn);

    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop *loop_;       // baseloop 用户定义的loop

    const std::string ipPort_;
    const std::string name_;

    std::unique_ptr<Acceptor> acceptor_;                // 运行在mainLoop, 任务是监听新的连接事件

    std::shared_ptr<EventLoopThreadPool> threadPool_;   // one loop per thread
    
    ConnectionCallback connectionCallback_;             // 有新连接时的回调
    MessageCallback messageCallback_;                   // 有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_;       // 消息发送完成以后的回调

    ThreadInitCallback threadInitCallback_;             // loop线程初始化的回调
    std::atomic_int started_;

    int nextConnId_;
    ConnectionMap connections_;                         // 保存所有的连接
};
#endif 