#pragma once

#include "Poller.h"
#include "Timestamp.h"

#include <vector>
#include <sys/epoll.h>

class Channel;

/**************************************************************************************
 * 继承自 Poller 的EPollPoller
 *      用于实现 epoll_create, epoll_ctl epoll_wait 
**************************************************************************************/
class EPollPoller : public Poller{
public:
    EPollPoller(EventLoop* loop);       // epoll_create
    ~EPollPoller()  override;

    // 重写基类的方法，实现 epoll_wait, epoll_ctl(add mod del)
    Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;
    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;

private:
    static const int kInitEventListSize = 16;

    using EventList = std::vector<epoll_event>;
    
    int epollfd_;
    EventList events_;

    // 填写活跃连接
    void fillActiveChannels(int numEvents, ChannelList* activeChannels);
    // 更新 channel， 监听事件
    void update(int operation, Channel* channel);
};