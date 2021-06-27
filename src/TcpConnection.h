#pragma once

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"

#include <memory>
#include <string>
#include <atomic>

class Channel;
class EventLoop;
class Socket;

class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop* loop,
                const std::string name,
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const {    return loop_;   }
    const std::string name() const {    return name_;   }
    const InetAddress& localAddr() const {  return localAddr_;  }
    const InetAddress& peerAddr() const {   return peerAddr_;   }
    bool connected() const {    return state_ == kConnected;    }
    bool disconnected() const { return state_ == kDisconnected; }

    // 设置回调函数
    void setConnectionCallback(const ConnectionCallback& cb){   connectionCallback_ = cb;   }
    void setMessageCallback(const MessageCallback& cb)      {   messageCallback_ = cb;      }
    void setWriteCompleteCallback(const WriteCompleteCallback& cb){ writeCompleteCallback_ = cb;    }
    void setHighWaterMarkCallback(const HighWaterMarkCallback& cb){ highWaterMarkCallback_ = cb;    }
    void setCloseCallback(const CloseCallback& cb)          {   closeCallback_ = cb;        }

    // 建立 / 销毁连接
    void connectEstablished();
    void connectDistroyed();

    // 关闭连接
    void shutdown();
    void shutdownInLoop();

    // 发送数据
    void send(const std::string& buf);
private:
    enum StateE{
        kDisconnected,
        kConnecting,
        kConnected,
        kDisconnecting
    };
    void setState(StateE state){    state_ = state; }


    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();
    
    void sendInLoop(const void* data, size_t len);

    EventLoop* loop_;
    const std::string name_;

    std::atomic_int state_;
    bool reading_;

    // 与 Acceptor 类似
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    ConnectionCallback connectionCallback_;      // 新连接回调
    MessageCallback messageCallback_;           // 读写消息回调
    WriteCompleteCallback writeCompleteCallback_;   // 消息发送完成的回调
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;

    size_t highWaterMark_;       // 水位线
    
    Buffer inputBuffer_;        // 接受数据
    Buffer outputBuffer_;       // 发送数据
};