#include "Poller.h"
#include "EPollPoller.h"

#include <stdlib.h>

Poller* Poller::newDefaultPoller(EventLoop* loop)
{
    if (::getenv("SZMUDUO_USE_POLL"))
    {
        return nullptr;     // 生成 poll 实例
    }
    else
    {
        return new EPollPoller(loop);     // 生成 epoll 实例
    }
}
