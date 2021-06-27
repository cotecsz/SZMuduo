#pragma once

#include "noncopyable.h"
#include "Socket.h"
#include "Channel.h"

#include <functional>

class EventLoop;
class InetAddress;


class Acceptor : noncopyable
{
public:
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress&)>;

    Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);
    ~Acceptor();

    void setNewConnectionCallback(const NewConnectionCallback& cb)
    {
        newConnectionCallback_ = cb;
    }

    bool listening()  const  {   return listening_;  }
    void listen();

private:
    void handleRead();
    EventLoop* loop_;       // Acceptor 用的是 用户定义的 baseloop，即Mainloop
    Socket acceptSocket_;
    Channel acceptChannel_;

    NewConnectionCallback newConnectionCallback_;
    bool listening_;
};