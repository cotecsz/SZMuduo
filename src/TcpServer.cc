#include "TcpServer.h"
#include "Logger.h"
#include "EventLoopThreadPool.h"
#include "TcpConnection.h"
#include "InetAddress.h"
#include "Callbacks.h"

#include <functional>
#include <strings.h>
#include <string>

static EventLoop* checkLoopNotNULL(EventLoop* loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d MainLoop is NULL!\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpServer::TcpServer(EventLoop* loop, 
                    const InetAddress& listenAddr, 
                    const std::string nameArg,
                    Option option)
                    : loop_(checkLoopNotNULL(loop)),
                      ipPort(listenAddr.toIpPort()),
                      name_(nameArg),
                      acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
                      threadPool_(new EventLoopThreadPool(loop, name_)),
                      connectionCallback_(),
                      messageCallback_(),
                      nextConnId_(1),
                      started_(0)
{
    // 有新用户连接时，会执行 TcpnewConnection 回调函数
    acceptor_->setNewConnectionCallback(
        std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2)
    );
}

TcpServer::~TcpServer()
{
    for (auto & item : connections_)
    {
        TcpConnectionPtr conn(item.second);
        item.second.reset();

        // 销毁连接
        conn->getLoop()->runInLoop(
            std::bind(&TcpConnection::connectDistroyed, conn)
        );
    }
}

void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setThreadNum(numThreads);
}

void TcpServer::start()
{
    if (started_++ == 0)    // 防止一个tcpServer对象被 start多次
    {
        threadPool_->start(threadInitCallback_);    // 启动底层 loop线程池
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));    // mainloop 开启监听
    }
}


// 有一个新客户端连接， acceptor 会执行回调操作
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
    // 1. 轮询算法选择一个 subLoop，管理channel
    EventLoop* ioLoop = threadPool_->getNextLoop();

    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "-%s#%d", ipPort.c_str(), nextConnId_);
    ++nextConnId_;

    std::string connName = name_ + buf;
    LOG_INFO("TcpServer::newConection [%s] - new connection [%s] from %s \n",
        name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());
    
    // 通过 sockfd 获取绑定本机的IP地址和端口号
    sockaddr_in local;
    bzero(&local, sizeof(local));
    socklen_t addrLen = sizeof(local);
    if (::getsockname(sockfd, (sockaddr*)&local, &addrLen) < 0)
    {
        LOG_ERROR("sockets:getsockname\n");
    }
    InetAddress localAddr(local);

    // 2. 根据连接成功的 sockfd， 创建TcpConnection
    TcpConnectionPtr conn(new TcpConnection(
                            ioLoop,
                            connName,
                            sockfd,
                            localAddr,
                            peerAddr
    ));
    connections_[connName] = conn;

    // // 绑定相应连接
    // // TcpServer => TcpConnection => Channel => Poller => notify channel 调用回调
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // // 设置如何关闭连接的回调
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1)
    );
    // 3. 直接调用 tcpConnection::connectEstablished()
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
    loop_->runInLoop(
        std::bind(&TcpServer::removeConnectionInLoop, this, conn)
    );
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s \n",
        name_.c_str(), conn->name().c_str());

    connections_.erase(conn->name());
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDistroyed, conn)
    );
}
