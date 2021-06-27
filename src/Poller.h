#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <vector>
#include <unordered_map>

class Channel;
class EventLoop;

/**************************************************************************************
 * SZMuduo  库中多路复用分发器的核心IO复用模块
**************************************************************************************/
class Poller{
public:
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop *loop);
    virtual ~Poller() = default;

    // 给IO多路复用，保留统一的接口：如果派生类是epoll则是 epoll_wait 的封装
    virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;   // epoll_wait
    virtual void updateChannel(Channel* channel) = 0;       // epoll_ctl ADD MOD
    virtual void removeChannel(Channel* channel) = 0;       // epoll_ctl DEL

    // 参数 channel 是否在当前的 Poller 中
    bool hasChannel(Channel* channel) const;

    // 获取 默认的IO多路复用的 EventLoop
    static Poller* newDefaultPoller(EventLoop* loop);

protected:
    // map-key : socketfd   value : socketfd 所属的 channel
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;

private:
    EventLoop* ownerLoop_;      // Poller 所需的事件循环 EventLoop 
};