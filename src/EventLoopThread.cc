#include "EventLoopThread.h"
#include "EventLoop.h"


EventLoopThread::EventLoopThread(const ThreadInitCallback& cb, const std::string& name)
        : loop_(nullptr),
          exiting_(false),
          thread_(std::bind(&EventLoopThread::threadFunc, this), name),
          mutex_(),
          cond_(),
          callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
        thread_.join();
    }
}

EventLoop* EventLoopThread::startLoop()
{
    thread_.start();        // start 才开始创建线程，执行 线程回调函数，即 threadFunc

    EventLoop* loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (loop_ == nullptr)
        {
            cond_.wait(lock);
        }
        loop = loop_;
    }
    return loop;
}

void EventLoopThread::threadFunc()
{
    EventLoop loop;     // one loop per thread：创建一个独立的 Eventloop，与上面的线程一一对应

    if (callback_)
    {
        callback_(&loop);
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }

    loop.loop();        // EventLoop loop --> poller.poll()
    
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}