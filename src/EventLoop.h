#pragma once

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

class Channel;
class Poller;

/**********************************
 * 事件循环类
 *     事件循环类中包含两个模块：Channel  Poller(epoll 的封装)。
 *     Channel ：用于管理socket
**********************************/
class EventLoop : noncopyable{
public:
    using Functor = std::function<void()>;
    EventLoop();
    ~EventLoop();

    // 开启 / 退出事件循环
    void loop();
    void quit();

    Timestamp pollReturnTime() const {  return pollReturnTime_;}
    void runInLoop(Functor cb);     // 在当前 loop 中执行
    void queueInLoop(Functor cb);   // 将 cb 放入队列中，唤醒loop所在线程，执行 cb

    // 用于唤醒 loop 所在线程
    void wakeup();

    // Eventloop  => Poller方法
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(Channel* channel);

    // 判断 Eventloop 释放在当前线程中
    bool isInLoopThread() const {   return threadId_ == CurrentThread::tid();   }

private:
    void handleRead();      // wakeup
    void doPendingFunctors();
    
    using ChannelList = std::vector<Channel*>;

    std::atomic_bool looping_;      // 原子操作，底层通过 CAS 实现
    std::atomic_bool quit_;         // 标识退出 loop 循环   
    
    const pid_t threadId_;          // 标识 当前线程 loop_ 所在的线程 id
    
    // poller
    Timestamp pollReturnTime_;      // Poller 返回发生事件channels 的事件
    std::unique_ptr<Poller> poller_;// Poller--> EpollPoller
    
    // 当 MainRactor 获取新客户端的 channel，通过轮询算法唤醒一个subreactor
    // 通过 wakeupFd_ 唤醒 subreactor处理
    int wakeupFd_;    
    std::unique_ptr<Channel> wakeupChannel_;

    // channel
    ChannelList activeChannels_;      // Eventloop 管理的所有 channel

    // 回调
    std::atomic_bool callingPendingFunctors_;   // 标识当前 loop 是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_;      // 存储 loop 需要执行的回调操作
    std::mutex mutex_;                          // 用来保护 vector 容器的线程安全
};