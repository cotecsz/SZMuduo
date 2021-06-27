#pragma once

#include "noncopyable.h"
#include "Timestamp.h"
#include <functional>
#include <memory>

class EventLoop;

/**************************************************************************************
 *  EventLoop, Channel, Channel 之间的关系：Reactor模型中对应Demultiplex（多路复用分发器）
 * 
 *  Channel 类，封装了 socketfd 及 socket 等待的事件，如EPOLLIN、EPOLLOUT事件。同时绑定
 * Poller 监听返回的 具体的事件。
**************************************************************************************/

class Channel : noncopyable{
public:
    using EventCallback = std::function<void()> ;
    using ReadEventCallback = std::function<void(Timestamp)> ;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    // fd 得到poller 通知后，调用相应的回调函数，处理事件
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数
    void setReadCallback(ReadEventCallback cb)  {readCallback_ = std::move(cb);}
    void setWriteCallback(EventCallback cb) {writeCallback_ = std::move(cb);}
    void setCloseCallback(EventCallback cb) {closeCallback_ = std::move(cb);}
    void setErrorCallback(EventCallback cb) {errorCallback_ = std::move(cb);}

    // 防止当 channel 被手动remove 掉，channel 还在执行回调操作。
    void tie(const std::shared_ptr<void>& );

    int fd() const {return fd_; }
    int events() const {return events_; }
    void set_revents(int revt)   {revents_ = revt;    }

    // 返回fd 当前的事件状态
    bool isNoneEvent() const {return events_ == kNoneEvent;  }
    bool isWriting() const {return events_ & kWriteEvent;    }
    bool isReading() const {return events_ & kReadEvent;     }

    // 设置 fd 相应的读写事件
    void enableReading()    {events_ |= kReadEvent; update();   }
    void disableReading()   {events_ &= ~kReadEvent; update();  }
    void enableWriting()    {events_ |= kWriteEvent; update();  }
    void disableWriting()   {events_ &= ~kWriteEvent; update(); }
    void disableAll()       {events_ = kNoneEvent;  update();   }

    int index() {return index_;}
    void set_index(int idx){ index_ = idx;};

    EventLoop* ownerLoop()  {return loop_;};
    // one loop pre thread: 网络模型 muduo, libevent, libev

    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    // 当前fd 状态
    static const int kNoneEvent;        
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_;

    const int fd_;      // fd，Poller监听的对象
    int events_;        // fd 监听的事件
    int revents_;       // Poller 返回具体的发生的事件
    int index_;         

    /* 
    *   跨线程生存状态监听：保证在手动释放 channel 后，防止继续使用。
    *   weak_ptr 的作用
    *       1. 可以防止循环引用问题
    *       2. 多线程中监听资源生存状态，在使用资源前对指针 lock() 提升，
    *          如果提升成功，则访问，否则访问失败。
    */
    std::weak_ptr<void> tie_;     
    bool tied_; 

    // 函数对象，用于绑定具体发生的事件的回调函数
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};   