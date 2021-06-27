#include "EPollPoller.h"
#include "EventLoop.h"
#include "Channel.h"
#include "Logger.h"

#include <errno.h>
#include <unistd.h>
#include <string.h>

const int kNew = -1;        // channel 未添加, channel index_ 默认为 -1
const int kAdded = 1;       // channel 已添加
const int kDeleted = 2;     // channel 已删除

EPollPoller::EPollPoller(EventLoop* loop)       // epoll_create
    : Poller(loop),
      epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize)               // vector<epoll_event>
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error: %d\n", errno);
    }
}

EPollPoller::~EPollPoller() 
{
    ::close(epollfd_);
}


/********************************************************************************************
 * 重写基类的方法，实现 epoll_wait, epoll_ctl(add mod del)
 *     功能：通过 epoll_wait, 将已经发生的事件，以传出参数 ChannelList* 传出至 EventLoop
**********************************************************************************************/
Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    LOG_INFO("func = %s => fd total count: %ld\n", __FUNCTION__, channels_.size());

    int numEvents = ::epoll_wait(epollfd_, 
                                &*events_.begin(), 
                                static_cast<int>(events_.size()), 
                                timeoutMs 
                            );
    int savedError = errno;
    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        LOG_INFO("%d events happened\n", numEvents);
        fillActiveChannels(numEvents, activeChannels);

        // LT 模式
        if (numEvents == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_INFO("%s timeout, nothing happened!", __FUNCTION__);
    }
    else
    {
        if (savedError != EINTR)
        {
            errno = savedError;
            LOG_ERROR("EPOLLPoller::poll() error!");
        }
    }
    return now;
}

// 填写活跃连接
void EPollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels)
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);        // 记录channel 发生事件
        activeChannels->push_back(channel);     // EventLoop 得到所有发生事件的 channel 列表
    }
}

// channel update remove => EventLoop updateChannle removeChannel => Poller updateChannle removeChannel
void EPollPoller::updateChannel(Channel* channel)
{
    const int index = channel->index();
    LOG_INFO("func = %s => fd = %d, events = %d, index = %d \n", 
            __FUNCTION__, channel->fd(), channel->events(), index);

    // channel 未添加，或者添加后删除
    //      未添加：则重新添加至 channels_，然后设置为 kAdded，然后epoll_ctl_add
    //      已删除：设置 channel 为 kAdded，然后epoll_ctl_add
    if (index == kNew || index == kDeleted)
    {
        if (index == kNew)
        {
            int fd = channel->fd();
            channels_[fd] = channel;
        }

        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else        // channel 在Poller 上 注册，如果 channel 没有感兴趣事件，则delete，否则 epoll_ctl_mod
    {
        int fd = channel->fd();
        if (channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

void EPollPoller::removeChannel(Channel* channel)
{
    int fd = channel->fd();
    channels_.erase(fd);     // Poller 中 channelMap 中删除

    LOG_INFO("func = %s => fd = %d\n", 
            __FUNCTION__, channel->fd());

    const int index = channel->index();
    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

// 更新 channel， 监听事件
void EPollPoller::update(int operation, Channel* channel)
{
    epoll_event event;
    bzero(&event, sizeof(event));

    int fd = channel->fd();
    event.events = channel->events();
    event.data.ptr = channel;
    event.data.fd = fd;

    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error: %d\n", errno);
        }     
        else
        {
            LOG_FATAL("epoll_ctl add/mod error: %d\n", errno);
        }    
    }
}