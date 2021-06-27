#pragma once

#include <vector>
#include <string>
#include <algorithm>

/**************************************************************************************
 * SZMuduo 网络库底层缓冲器类型
**************************************************************************************/
class Buffer
{
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + kInitialSize),
          readerIndex_(kCheapPrepend),
          writerIndex_(kCheapPrepend)
        {
        }

    size_t readableBytes() const 
    {
        return writerIndex_ - readerIndex_;
    }

    size_t wirterableBytes() const 
    {
        return buffer_.size() - writerIndex_;
    }

    size_t prependableBytes() const
    {
        return readerIndex_;
    }

    // 返回 可读数据缓冲区 的起始地址
    const char* peek() const 
    {
        return begin() + readerIndex_;
    }

    // onMessage 时， 将 Buffer --> string
    void retrieve(size_t len)
    {
        if (len < readableBytes())
        {
            readerIndex_ += len;
        }
        else        // len == readableBytes()
        {
            retrieveAll();
        }
    }

    void retrieveAll()
    {
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }

    // 将 onMessage 函数上报的 Buffer 数据，转成 string类型的数据
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());       // 可读取数据的长度
    }

    std::string retrieveAsString(size_t len)
    {
        std::string result(peek(), len);        // 将缓冲区中可读数据 转化为 string，并返回
        retrieve(len);                          // 更新缓冲区
        return result;
    }

    void ensureWriteableBytes(size_t len)
    {
        if (wirterableBytes() < len)
        {
            makeSpace(len);
        }
    }

    // 添加数据：将内存数据 [data, data+len] ，添加到 writeable 缓冲区中
    void append(const char* data, size_t len)
    {
        ensureWriteableBytes(len);
        std::copy(data, data + len, beginWrite());
        writerIndex_ += len;
    }

    char* beginWrite()
    {
        return begin() + writerIndex_;
    }

    const char* beginWrite()  const 
    {
        return begin() + writerIndex_;
    }

    ssize_t readFd(int fd, int* savedErrno);
    ssize_t writeFd(int fd, int* savedErrno);
    
private:
    char* begin()
    {
        return &*buffer_.begin();
    }

    const char* begin() const
    {
        return &*buffer_.begin();
    }

    void makeSpace(size_t len)
    {
        if (wirterableBytes() + (prependableBytes() - kCheapPrepend) < len)
        {
            buffer_.resize(writerIndex_ + len);
        }
        else
        {
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_,
                    begin() + writerIndex_,
                    begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = kCheapPrepend + readable;
        }
    }

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};