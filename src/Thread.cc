#include "Thread.h"
#include "CurrentThread.h"
#include <semaphore.h>

std::atomic_int Thread::numCreated_(0);

Thread::Thread(ThreadFunc func, const std::string& name)
    : started_(false),
      joined_(false),
      tid_(0),
      func_(std::move(func)),
      name_(name)
{
    setDefaultname();
}

Thread::~Thread()
{
    if (started_ && !joined_)
    {
        thread_->detach();      // thread 提供设置分离线程的方法
    }
}

// 线程对象，记录新线程的详细信息：启动状态，join，线程id，线程函数，线程名称
void Thread::start()
{
    started_ = true;

    sem_t sem;
    sem_init(&sem, false, 0);

    thread_ = std::shared_ptr<std::thread>(new std::thread([&](){
        // 获取线程的tid
        tid_ = CurrentThread::tid();
        sem_post(&sem);
        func_();        // 开启新线程，专门执行该线程函数
    }));

    // 必须在创建线程后，才能开始执行
    sem_wait(&sem);
}

void Thread::join()
{
    joined_ = true;
    thread_->join();
}


void Thread::setDefaultname()
{
    int num = ++numCreated_;

    if (name_.empty())
    {
        char buf[32] = {0};
        snprintf(buf, sizeof(buf), "Thread%d", num);
    }
}