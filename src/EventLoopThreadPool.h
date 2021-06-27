#pragma once

#include "noncopyable.h"

#include <functional>
#include <string>
#include <vector>
#include <memory>

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThreadPool(EventLoop* baseloop, const std::string& nameArg);
    ~EventLoopThreadPool();

    void setThreadNum(int numThreads)   {   numThreads_ = numThreads;   }
    void start(const ThreadInitCallback& cb = ThreadInitCallback());

    // 如果是多线程，baseloop 默认以 轮询方式分配 channel 给 subloop
    EventLoop* getNextLoop();
    std::vector<EventLoop*> getAllLoops();

    bool started() const {  return started_;    }
    const std::string& name() const {return name_;   }

private:
    EventLoop* baseLoop_;       // Eventloop 
    
    std::string name_;
    bool started_;
    int numThreads_;
    int next_;

    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};