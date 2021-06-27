#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d listen socket create error: %d", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd;
}

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
    : loop_(loop),
      acceptSocket_(createNonblocking()),               // socket 创建套接字
      acceptChannel_(loop, acceptSocket_.fd()),
      listening_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    acceptSocket_.bindAddress(listenAddr);              // bind 套接字

    // TcpServer start() --> Acceptor.listen --> 新用户连接 --> 回调函数
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();
    acceptChannel_.remove();        // channel 从 loop 中移除
}

void Acceptor::listen()
{
    listening_ = true;
    acceptSocket_.listen();         // listen
    acceptChannel_.enableReading(); // acceptChannel -> Poller
}


// listenfd 有事件发生，即有新用户连接
void Acceptor::handleRead()
{
    InetAddress peeraddr;
    int connfd = acceptSocket_.accept(&peeraddr);
    if (connfd >= 0)
    {
        if (newConnectionCallback_)     // 有注册回调函数
        {
            newConnectionCallback_(connfd, peeraddr);
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d listen socket accept error: %d", __FILE__, __FUNCTION__, __LINE__, errno);
        if (errno == EMFILE)        // 文件描述符用尽
        {
            LOG_ERROR("%s:%s:%d socketfd reach limit  error", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}