#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"
#include <sys/epoll.h>
#include <assert.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;


Channel::Channel(EventLoop* loop, int fd) 
    : loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), tied_(false)
{

}

Channel::~Channel()
{
    // if (loop_->isInLoopThread()){
    //     std::assert(!loop_->hasChannel(this));
    // }
}

// channel tie 方法在建立新连接时建立
void Channel::tie(const std::shared_ptr<void>& obj)
{
    tie_ = obj;
    tied_ = true;
}

/********************************************************************************************
 * 用于在改变 channel 所表示的 fd 的events事件后，update 负责在poller 中更改fd 相应的事件 epoll_ctl
**********************************************************************************************/
void Channel::update()
{
    // Todo : 更新 fd的 监听事件 
    // eventloop_ -> updateChannel ==> EPollpoller->updateChannel ==> Epoll_ctl
    loop_->updateChannel(this);
}

void Channel::remove()
{
    // Todo : 移除 channel
    loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_){
        std::shared_ptr<void> guard = tie_.lock();

        if (guard){
            handleEventWithGuard(receiveTime);
        }
    }
    else{
        handleEventWithGuard(receiveTime);
    }
}

/********************************************************************************************
 * 根据poller 通知Channel 发生的具体事件，由 Channel 调用具体的回调函数
**********************************************************************************************/
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("Channel handleEvent revents: %d \n", revents_);

    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)){
        if (closeCallback_){
            closeCallback_();
        }
    }

    if ((revents_ & EPOLLERR))
    {
        if (errorCallback_)
        {
            errorCallback_();
        }
    }

    if (revents_ & (EPOLLIN | EPOLLPRI))
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }

    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
        }
    }
}





