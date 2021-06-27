#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <string>
#include <functional>
#include <errno.h>
#include <string>

static EventLoop* checkLoopNotNULL(EventLoop* loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d TcpConnection is NULL!\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop* loop,
                const std::string nameArg,
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr)
    : loop_(checkLoopNotNULL(loop)),
      name_(nameArg),
      state_(kConnecting),
      reading_(true),
      socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)),
      localAddr_(localAddr),
      peerAddr_(peerAddr),
      highWaterMark_(64*1024*1024)
{
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1)
    );
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this)
    );
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this)
    );
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this)
    );

    LOG_INFO("TcpConnection::ctor[%s] at fd %d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);        // 启动 tcp 保活机制 
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s[ at fd = %d state = %d\n",
        name_.c_str(), channel_->fd(), (int)state_);
    
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);

    if (n > 0)  // 有可读事件发生
    {
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0)    // 客户端断开
    {
        handleClose();
    }
    else        // 出错
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead\n");
        handleError();
    }

}

void TcpConnection::handleWrite()
{
    if (channel_->isWriting())  // 可读
    {
        int saveErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &saveErrno);

        if (n > 0)
        {
            outputBuffer_.retrieve(n);  // 已经读取 n 个数据
            if (outputBuffer_.readableBytes() == 0)       // 可读数据为0， 设置不可写
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    // 唤醒 loop_ 对应 thread，执行回调
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this())
                    );
                }

                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite\n");
        }
    }
    else{   // 不可读
        LOG_ERROR("TcpDConnection fd = %d is down, no more writing\n", channel_->fd());
    }
}

// poller => channel::closeCallback =>TcpConnection：：handleClose
void TcpConnection::handleClose()
{
    LOG_INFO("fd = %d state = %d\n", channel_->fd(), (int)state_);
    setState(kDisconnected);    

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr);   // 连接关闭的回调
    closeCallback_(connPtr);        // 关闭连接,TcpServer ::removeConnection 回调
}

void TcpConnection::handleError()
{
    int err = 0; 
    int optval;
    socklen_t optlen = sizeof(optval);
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }

    LOG_ERROR("TcpConnection::handleError name: %s -s SO_ERROR: %d\n", name_.c_str(), err);
}

void TcpConnection::send(const std::string& buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(
                &TcpConnection::sendInLoop,
                this,
                buf.c_str(),
                buf.size()
            ));
        }
    }
}

// 发送数据，应用快，内核发送较慢，需要将发送数据写入缓冲区，设置了水位回调
void TcpConnection::sendInLoop(const void* data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    if (state_ == kDisconnected)   // shutdown，正在关闭
    {
        LOG_ERROR("disconnected, give up writing\n");
        return ;
    }

    // channel 第一次开始写数据，且缓冲区无发送数据
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 数据全部发送完成，无需给channel 设置 epollout 事件
                loop_->queueInLoop(std::bind(
                    writeCompleteCallback_, shared_from_this()
                ));
            }
        }
        else        // 出错
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop\n");
                if (errno == EPIPE || errno == ECONNRESET)      // SIGPIPE RESET
                {
                    faultError = true;
                }
            }
        }
    }

    // write 并未将全部数据发送，保存剩余数据至 缓冲区，给channel 注册epollout 事件
    // poller 发送 tcp发送缓冲区有空间，触发相应的 sock channel ，调用 writeCallback
    // 即调用 handlewrite再次发送把数据全部发送完成
    if (!faultError && remaining > 0)   
    {
        // 目前发送缓冲区剩余的待发送数据
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_    // 上一次会调用 高水位回调
            && oldLen < highWaterMark_
            && highWaterMark_)
        {
            loop_->queueInLoop(
                std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining)
            );
        }

        outputBuffer_.append(static_cast<const char*> (data) + nwrote, remaining);
        if (!channel_->isWriting())
        {
            channel_->enableWriting();
        }
    }
}

// 连接建立
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading();

    // 新连接建立，执行回调
    connectionCallback_(shared_from_this());
}

// 连接销毁
void TcpConnection::connectDistroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll();

        connectionCallback_(shared_from_this());
    }
    channel_->remove(); // 从 poller 中删除 channel
}

// 关闭连接
void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::shutdownInLoop()
{
    // outputBuffer 中数据全部发送完成
    if (!channel_->isWriting())
    {
        socket_->shutdownWrite();       // 关闭写端
    }
}




