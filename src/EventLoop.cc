#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <memory>


// 防止一个线程创建多个 EventLoop，全局指针变量，当线程中EventLoop 创建后，则指向该EventLoop
// 在下一次创建时，如果指针不为空，则表示已经创建。
__thread EventLoop* t_loopInThisThread = nullptr;


// 默认的 Poller IO复用接口的超时时间，10s
const int kPollTimeMs = 10000;

// 用来创建 wakeup fd， 用于唤醒 subReactor 处理新的 channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error: %d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      callingPendingFunctors_(false),
      threadId_(CurrentThread::tid()),
      poller_(Poller::newDefaultPoller(this)),
      wakeupFd_(createEventfd()),
      wakeupChannel_(new Channel(this, wakeupFd_))
{
    LOG_DEBUG("EventLoop created %p in thread %d", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exist in this thread %d\n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }

    // 设置 wakupfd 事件类型以及发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每个 eventLoop 都会监听 wakeupchannel 的 EPOLLIN事件
    wakeupChannel_->enableReading();        
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));

    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::handleRead() reads %ld bytes instead of 8", n);
    }
}


void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping \n", this);

    while (!quit_)
    {
        activeChannels_.clear();
        // 监听两类fd 1. client fd 2. wakeup fd
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);

        for (Channel* channel : activeChannels_)
        {
            // Poller 监听哪些 channel 发生事件，然后通知 EventLoop ，通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }

        // 执行当前 EventLoop 事件循环需要处理的回调操作
        /**
         * @brief Construct a new do Pending Functors object
         *   IO 线程， MainLoop 主要处理 accept事件，将接受的 新的客户端，分发给 subReactor
         * MainLoop 注册一个回调，需要 subReactor 执行
         * 唤醒 subReactor 后，执行回调方法，即MainLoop 注册的回调函数
         */
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping. \n", this);
    looping_ = false;
}

// 退出事件循环 
// 1. loop 在自己线程中调用 quit   
// 2. 如果在其他线程中调用 quit，在一个subLoop 调用 MainLoop的quit，需要先唤醒 MainLoop 
void EventLoop::quit()
{
    quit_ = true;

    if (!isInLoopThread())
    {
        wakeup();
    }
}


void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())   // 在当前的loop 线程中，执行 callback
    {
        cb();
    }
    else                    // 在非当前loop 线程中执行 callback，需要唤醒 loop 所在线程，执行callback
    {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }

    // 唤醒相应的 需要执行回调操作的 loop 的线程
    // 当前loop 正在执行回调，则 loop 又有新的回调 
    if (!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();       // 唤醒 loop 所在线程
    }
}

void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));

    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::wakeup() writes %ld bytes instead of 8 \n", n);
    }
}

void EventLoop::updateChannel(Channel* channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
    return poller_->hasChannel(channel);
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (const Functor& functor : functors)
    {
        functor();
    }
    callingPendingFunctors_ = false;
}
