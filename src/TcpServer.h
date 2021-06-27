#pragma once

#include "noncopyable.h"
#include "EventLoop.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"

#include <functional>
#include <string.h>
#include <memory>
#include <atomic>
#include <unordered_map>

class EventLoopThreadPool;

class TcpServer : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;
    enum Option
    {
        kNoReusePort,
        kReusePort,
    };
    TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string nameArg, Option option = kNoReusePort);
    ~TcpServer();

    void setThreadInitCallback(const ThreadInitCallback& cb) {  threadInitCallback_ = cb;   }
    void setConnectionCallback(const ConnectionCallback& cb) {  connectionCallback_ = cb;   }
    void setMessageCalback(const MessageCallback& cb)        {  messageCallback_ = cb;      }
    void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb;}

    void setThreadNum(int numThreads);

    void start();       // 开启监听


private:
    void newConnection(int sockfd, const InetAddress& peerAddr);
    void removeConnection(const TcpConnectionPtr& conn);
    void removeConnectionInLoop(const TcpConnectionPtr& conn);

    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop* loop_;       // base loop: the acceptor loop
    const std::string ipPort;
    const std::string name_;
    
    std::unique_ptr<Acceptor> acceptor_;        // 监听连接事件
    std::shared_ptr<EventLoopThreadPool> threadPool_;

    ConnectionCallback connectionCallback_;      // 新连接回调
    MessageCallback messageCallback_;           // 读写消息回调
    WriteCompleteCallback writeCompleteCallback_;   // 消息发送完成的回调
    ThreadInitCallback threadInitCallback_;     // 线程初始化

    std::atomic_int started_;

    int nextConnId_;
    ConnectionMap connections_;     // 保存所有连接
};