#pragma once

#include "noncopyable.h"

#include <functional>
#include <thread>
#include <memory>
#include <unistd.h>
#include <string>
#include <atomic>

class Thread
{
public:
    using ThreadFunc = std::function<void()> ;

    explicit Thread(ThreadFunc, const std::string& name = std::string());
    ~Thread();

    void start();
    void join();

    bool started() const {return started_;}
    pid_t tid() const {return tid_;}        // top 打印出来的 pid

    const std::string& name() const {   return name_;}
    static int numCreated() {   return numCreated_; }

private:
    void setDefaultname();
    
    bool started_;
    bool joined_;    
    
    // 线程相关信息
    std::shared_ptr<std::thread> thread_;
    pid_t tid_;
    ThreadFunc func_;
    std::string name_;

    // 静态变量，统计线程个数
    static std::atomic_int numCreated_;
};