#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"

#include <memory>

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseloop, const std::string& nameArg)
    : baseLoop_(baseloop),
      name_(nameArg),
      started_(false),
      numThreads_(0),
      next_(0)
{

}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // 局部栈上创建的loop，所以无需释放
}

void EventLoopThreadPool::start(const ThreadInitCallback& cb)
{
    started_ = true;

    for (int i = 0; i < numThreads_; i++)
    {
        char buf[name_.size() + 32];
        snprintf(buf, sizeof(buf), "%s%d", name_.c_str(), i);

        EventLoopThread* t = new EventLoopThread(cb, buf);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));
        loops_.push_back(t->startLoop());       // 创建线程，绑定新的EventLoop，返回 loop地址
    }

    // server 只有一个 线程，运行base loop
    if (numThreads_ == 0 && cb)
    {
        cb(baseLoop_);
    }
}


// 如果是多线程，baseloop 默认以 轮询方式分配 channel 给 subloop
EventLoop* EventLoopThreadPool::getNextLoop()
{
    EventLoop* loop = baseLoop_;
    if (!loops_.empty())
    {
        loop = loops_[next_];
        ++next_;

        if (next_ >= loops_.size())
        {
            next_ = 0;
        }
    }

    return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops()
{
    if (loops_.empty())
    {
        return std::vector<EventLoop*>(1, baseLoop_);
    }
    else{
        return loops_;
    }
}