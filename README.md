# SZMuduo
基于C++11，利用IO多路复用技术Epoll与线程池实现事件驱动的多线程Reactor模型，实现一个高性能的网络库。



## Noncopyable 禁止拷贝构造和赋值

Noncopyable 类设计：用于继承，对于一些不能拷贝构造，赋值的类，继承该类，可以使得类不能被拷贝，赋值。



实现方式：将 构造函数和析构函数声明为 protected，保证派生类可以访问，外部不可访问。将拷贝构造函数和赋值函数设置为 delete，则派生类就不能拷贝构造基类，所以自己也无法拷贝构造和赋值。

```cpp
#pragma once

class noncopyable{
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;

protected:
    noncopyable() = default;
    ~noncopyable() = default;
};
```



## Logger 日志

​    	对于日志系统，设置为单例模式，并且定义宏来方便使用。



​		定义日志级别：INFO（基本信息）  ERROR（错误信息）  FATAL （致命错误） DEBUG（调试信息）

  ```cpp
enum LogLevel
{
    INFO,       // 普通信息
    ERROR,      // 错误信息
    FATAL,      // core 信息
    DEBUG       // 调试信息
};

class Logger : noncopyable{
public:
    // 获取唯一日志实例对象
    static Logger& instance();      
    // 设置日志级别
    void setLogLevel(int level);
    // 写日志
    void log(std::string msg);
private:
    int logLevel_;
};
  ```



## Timestamp 时间

​		64位的int型 成员变量，通过 now 获取当前时间，通过 系统调用 localtime() 转换为年月日时间。最后转换为 string 输出即可。

```cpp
class Timestamp{
public:
    Timestamp();
    explicit Timestamp(int64_t microSecondsSinceEpoch);
    static Timestamp now();
    std::string toString() const;

private:
    int64_t microSecondsSinceEpoch_;
};
```



问题：将 noncopyable 声明为 protected，继承的派生类才可以进行private 继承，否则private继承，基类的构造和析构函数不可见，导致派生类无法构造。



## Channel 通道实现：对于监听 fd 的封装

​		对于 每个客户端的fd 的封装，包含 监听事件和 实际发生的事件，通过实际发生的事件，调用相应的回调函数。

```cpp
class Channel : noncopyable{
public:
    using EventCallback = std::function<void()> ;
    using ReadEventCallback = std::function<void(Timestamp)> ;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    // fd 得到poller 通知后，调用相应的回调函数，处理事件
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数
    void setReadCallback(ReadEventCallback cb)  {readCallback_ = std::move(cb);}
    void setWriteCallback(EventCallback cb) {writeCallback_ = std::move(cb);}
    void setCloseCallback(EventCallback cb) {closeCallback_ = std::move(cb);}
    void setErrorCallback(EventCallback cb) {errorCallback_ = std::move(cb);}

    // 防止当 channel 被手动remove 掉，channel 还在执行回调操作。
    void tie(const std::shared_ptr<void>& );

    int fd() const {return fd_; }
    int events() const {return events_; }
    void set_revents(int revt)   {revents_ = revt;    }

    // 返回fd 当前的事件状态
    bool isNoneEvnet() const {return events_ == kNoneEvent;  }
    bool isWriting() const {return events_ & kWriteEvent;    }
    bool isReading() const {return events_ & kReadEvent;     }

    // 设置 fd 相应的读写事件
    void enableReading()    {events_ |= kReadEvent; update();   }
    void disableReading()   {events_ &= ~kReadEvent; update();  }
    void enableWriting()    {events_ |= kWriteEvent; update();  }
    void disableWriting()   {events_ &= ~kWriteEvent; update(); }
    void disableAll()       {events_ = kNoneEvent;  update();   }

    int index() {return index_;}
    void set_index(int idx){ index_ = idx;};

    EventLoop* ownerLoop()  {return loop_;};
    // one loop pre thread: 网络模型 muduo, libevent, libev

    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    // 当前fd 状态
    static const int kNoneEvent;        
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_;

    const int fd_;      // fd，Poller监听的对象
    int events_;        // fd 监听的事件
    int revents_;       // Poller 返回具体的发生的事件
    int index_;         

    /* 
    *   跨线程生存状态监听：保证在手动释放 channel 后，防止继续使用。
    *   weak_ptr 的作用
    *       1. 可以防止循环引用问题
    *       2. 多线程中监听资源生存状态，在使用资源前对指针 lock() 提升，
    *          如果提升成功，则访问，否则访问失败。
    */
    std::weak_ptr<void> tie_;     
    bool tied_; 

    // 函数对象，用于绑定具体发生的事件的回调函数
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};   
```





## Poller 抽象类：对于 Epoll 和 Poll 的抽象基类

​		Poller 是对IO多路复用的一个封装，可以选择 poll / Epoll，即 epoll_wait，epoll_ctl 的 封装，同时获取其默认的EventLoop。



```cpp
class Poller{
public:
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop *loop);
    virtual ~Poller();

    // 给IO多路复用，保留统一的接口：如果派生类是epoll则是 epoll_wait 的封装
    virtual Timestamp poll(int timeout, ChannelList* activeChannels) = 0;   // epoll_wait
    virtual void updateChannel(Channel* channel) = 0;       // epoll_ctl ADD MOD
    virtual void removeChannel(Channel* channel) = 0;       // epoll_ctl DEL

    // 参数 channel 是否在当前的 Poller 中
    bool hasChannel(Channel* channel) const;

    // 获取 默认的IO多路复用的 EventLoop
    static Poller* newDefaultPoller(EventLoop* loop);

protected:
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;

private:
    EventLoop* ownerLoop_;      // Poller 所需的事件循环 EventLoop 
};
```



​		newDefaultPoller 需要单独文件创建：因为通过基类返回派生类对象（EpollPoller，PollPoller），如果在基类的 cpp文件中，则需要包含派生类的头文件，会比较混乱，所以新建一个文件，使得基类不依赖派生类，减少其耦合性。



## EpollPoller 实现：Poller 的派生类，实现对事件管理

​		EpollPoller 继承自 Poller类，通过实现 epoll 的 epoll_wait 与 epoll_ctl 的封装，实现对事件的管理。

```cpp
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
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
    // 更新 channel， 监听事件
    void update(int operation, Channel* channel);
};
```



## EventLoop：事件循环

​		包含 用于管理连接的 Epoll 和 Channel ，用于fd 以及fd 感兴趣的事件和实际发生的事件（Channel ，通道），可以看做 Epoll_wait。

​		

​		EventLoop 包含 Channel 和 Poller，一个 EventLoop 中有个IO多路复用对象Poller，通过 Poller 抽象基类选择不同的IO多路复用对象，如 Epoll, poll。 

​    	EpollPoller 构造函数即为 epoll_create, 析构函数为 close(epollfd_)。updateChannel 和 removeChannel 对应 epoll_ctl（epoll_ctl_add, mod, del），poll对应的是 epoll_wait() 方法。



  	成员方法

```cpp
#pragma once

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

class Channel;
class Poller;

/**********************************
 * 事件循环类
 *     事件循环类中包含两个模块：Channel  Poller(epoll 的封装)。
 *     Channel ：用于管理socket
**********************************/
class EventLoop : noncopyable{
public:
    using Functor = std::function<void()>;
    EventLoop();
    ~EventLoop();

    // 开启 / 退出事件循环
    void loop();
    void quit();

    Timestamp pollReturnTime() const {  return pollReturnTime_;}
    void runInLoop(Functor cb);     // 在当前 loop 中执行
    void queueInLoop(Functor cb);   // 将 cb 放入队列中，唤醒loop所在线程，执行 cb

    // 用于唤醒 loop 所在线程
    void wakeup();

    // Eventloop  => Poller方法
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(Channel* channel);

    // 判断 Eventloop 释放在当前线程中
    bool isInLoopThread() const {   return threadId_ == CurrentThread::tid();   }

private:
    void handleRead();      // wakeup
    void doPendingFunctors();
    
    using ChannelList = std::vector<Channel*>;

    std::atomic_bool looping_;      // 原子操作，底层通过 CAS 实现
    std::atomic_bool quit_;         // 标识退出 loop 循环   
    
    const pid_t threadId_;          // 标识 当前线程 loop_ 所在的线程 id
    
    // poller
    Timestamp pollReturnTime_;      // Poller 返回发生事件channels 的事件
    std::unique_ptr<Poller> poller_;// Poller--> EpollPoller
    
    // 当 MainRactor 获取新客户端的 channel，通过轮询算法唤醒一个subreactor
    // 通过 wakeupFd_ 唤醒 subreactor处理
    int wakeupFd_;    
    std::unique_ptr<Channel> wakeupChannel_;

    // channel
    ChannelList activeChannels_;      // Eventloop 管理的所有 channel
    Channel* currentActiveChannel_;

    // 回调
    std::atomic_bool callingPendingFunctors_;   // 标识当前 loop 是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_;      // 存储 loop 需要执行的回调操作
    std::mutex mutex_;                          // 用来保护 vector 容器的线程安全
};
```



#### **唤醒机制**

​		wakefd  ：subReactor 监听一个wakefd，mainReactor 向subReactor 的wakeupfd 写数据，subReactor 感知读事件发生，唤醒了。此时，MainReactor 将新的Channel 添加到 subReactor 中。



#### 派发机制

​		主线程 Loop 和 子线程 Loop 之间没有 使用消费队列进行缓冲（生产者消费者模型），而是使用轮询的方式派发。



```cpp
	退出事件循环 
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
```

1. loop 在自己线程中调用 quit   
2. 如果在其他线程中调用 quit，在一个subLoop 调用 MainLoop的quit，需要先唤醒 MainLoop 



```cpp
std::vector<Functor> functors;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }
```

​		将 待执行的 vector 放在局部变量中，由于在处理回调时需要对 vector 加锁，所以放入局部变量后，不会影响 MainReactor 向 subReactor 中加入新的回调。





## CurrentThread：获取当前线程的 tid

​		设置一个 thread_local 线程局部变量，通过系统调用 `syscall(SYS_gettid)` 获取线程id。

```cpp
namespace CurrentThread
{
    extern __thread int t_cachedTid;

    void cacheTid();

    inline int tid()
    {
        if (__builtin_expect(t_cachedTid == 0, 0))
        {
            cacheTid();
        }
        return t_cachedTid;
    }
} 
```



## Thread 线程实现

​		使用 lambda 创建 thread，代替 C中使用 struct 传递参数数据。



```cpp
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
```



1. 以引用方式接受外部所有对象，在线程中可以访问外部对象所有变量
2. 保证父线程在退出前，子线程的tid 已经赋值：调用 start后，开启一个线程，主线程和子线程执行顺序不确定，所以要保证线程创建后，才能继续执行，所以需要使用信号量来控制，当post 后表示，子线程创建完成，子线程才能往下执行。



## EventLoopThread 绑定 Loop的thread

​		一个线程绑定一个 loop 对象

​		one loop per thread

```cpp
class EventLoopThread : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
                const std::string& name = std::string());
    ~EventLoopThread();

    EventLoop* startLoop();
private:
    void threadFunc();

    EventLoop* loop_;
    bool exiting_;

    Thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;

    ThreadInitCallback callback_;
};

```



​		startLoop 执行，会获取一个新线程，新线程会创建一个 loop，开启事件循环，并且通过 条件变量通知父线程，创建完成，返回新的loop。



## EventLoopThreadPool ：管理事件循环调度

​		baseloop 表示 第一个 eventloop，如果不设置线程数量，则表示单线程的 Reactor模型。



```cpp
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
```







## Socket封装

​		对 socket 的简单封装。

```cpp
class InetAddress;

class Socket : noncopyable
{
public:
    Socket(int sockfd) : sockfd_(sockfd)   {}
    ~Socket();

    int fd() const {    return sockfd_; }
    void bindAddress(const InetAddress& localaddr);
    void listen();
    int accept(InetAddress* peeraddr);

    void shutdownWrite();

    void setTcpNoDelay(bool on);
    void setReuseAddr(bool on);
    void setReusePort(bool on);
    void setKeepAlive(bool on);
private:
    const int sockfd_;
};
```



## Acceptor 实现

​		负责监听新的连接，处理 accept 事件。

​		newConnectionCallback 是新连接到来时的回调函数：

1. 封装 channel
2. 选择一个 fd，唤醒
3. 给 subReactor 进行监听



## TCPServer 实现

​		TCPServer 封装了 Poller 和 Channel ，分别

​		事件分发器，事件回调，事件循环线程池，维护所有连接。



​		TcpServer 对象构造，创建 Acceptor 负责监听

1. 根据轮询算法选择一个 subloop
2. 唤醒 subloop
3. 将当前connfd封装为一个 channel



## Buffer 缓冲区

​		非阻塞IO中缓冲区是必要的的，可以解决TCP粘包问题：在通信数据中加一个数据头，描述数据长度，截取相应数据包的大小，进行处理。



​		Buffer 的缓冲区大小确定，但是从 fd 中读数据，无法确定 tcp 数据的最终大小，如何解决？**Buffer 的缓冲区大小确定，但是从 fd 中读数据，无法确定 tcp 数据的最终大小，如何解决？

​		

iovec ，普通 read 在连续缓冲区存放数据，readv 将读出数据，自动填充输入的缓冲区，结构如下

```cpp
struct iovec {
    void  *iov_base;    /* Starting address */
    size_t iov_len;     /* Number of bytes to transfer */
};
```



​		使用 readv 读取数据，输入两个缓冲区，一个是 buffer 缓冲区，一个是栈空间分配的（效率很高），readv 从fd 读取数据，会首先填充 第一个buffer 缓冲区，如果够了，则不会将数据填入栈中，如果不足，会根据读取数据的大小，将剩余数据填充至 栈空间。然后将栈上的数据，移动在 buffer中。所以其内存利用率是比较高的。



## TcpConnection 已连接的连接

​		TcpServer 通过 Acceptor ，当新用户连接时，通过 accept时，得到 clientfd，将 其封装为 TcpConnection，设置回调，将其给定 channel ，再将channel 注册至 poller，当 poller检测到事件发生时，会调用相应的回调函数。



​		TcpConnection 与 Acceptor 类似，不同的是，Acceptor监听连接事件，在 baseloop中，TcpConnection 监听可读可写事件，在 subloop中。

​		高水位线：通过高水位线，控制两端的接收和发送速率，避免发送过快，数据丢失。



​		在构造函数中，为 channel 设置相应的回调函数，poller 给channel 通知感兴趣的事件发生，channel 会回调相应的操作函数。



第一部分：

​		事件循环 EventLoop

​		Thread，EventLoopThread，EventLoopThreadPool，

​		通过将一个线程绑定一个 loop，muduo库在使用时，需要先定义一个 loop，即baseloop，通过setThreadNum 设置线程的数量，实际上是设置subReactor 个数。



第二部分：

​		Acceptor，用于处理accept，监听新用户连接，新用户连接得到 clientfd，打包成channel，根据轮询算法，找一个subReactor，将chennle 给 subReactor，在给之前，通过wakeupfd 需要唤醒 subReactor。



最后，MainReactor 做 acceptor操作，将得到的clientfd 封装 channel 给 subReactor，让指定的subReactor监听新的channel 的读写事件。

















