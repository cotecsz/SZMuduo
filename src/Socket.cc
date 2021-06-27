
#include "Socket.h"
#include "Logger.h"
#include "InetAddress.h"

#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <strings.h>

Socket::~Socket()
{
    ::close(sockfd_);
}

void Socket::bindAddress(const InetAddress& localaddr)
{
    if (::bind(sockfd_, (sockaddr*)localaddr.getSockAddr(), sizeof(sockaddr_in)) != 0)
    {
        LOG_FATAL("bind sockfd: %d fail \n", sockfd_);
    }
}

void Socket::listen()
{
    if (::listen(sockfd_, 1024) != 0)
    {
        LOG_FATAL("listen sockfd: %d fail \n", sockfd_);
    }
}

int Socket::accept(InetAddress* peeraddr)
{
    sockaddr_in addr;
    socklen_t len;
    bzero(&addr, sizeof(addr));

    int connfd = ::accept(sockfd_, (sockaddr*)&addr, &len);
    if (connfd >= 0)
    {
        peeraddr->setSockAddr(addr);
    }
    
    return connfd;
}

void Socket::shutdownWrite()
{
    if (::shutdown(sockfd_, SHUT_WR) < 0)
    {
        LOG_ERROR("shutdownWrite error!\n");
    }
}

void Socket::setTcpNoDelay(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)); 
}

void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)); 
}
void Socket::setReusePort(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)); 
}

void Socket::setKeepAlive(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)); 
}