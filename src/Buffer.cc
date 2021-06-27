#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

// 从 fd 中读取数据
/**
 *     Buffer 的缓冲区大小确定，但是从 fd 中读数据，无法确定 tcp 数据的最终大小，如何解决？
 * 
 */
ssize_t Buffer::readFd(int fd, int* savedErrno)
{
    char extrabuf[65536] = {0};     // 栈空间, 64K 
    struct iovec vec[2];

    const size_t writeable = wirterableBytes();
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writeable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 一次最多读 64K数据，如果 writeable < 64K ,使用栈空间，否则不使用
    const int iovcnt = (writeable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n= ::readv(fd, vec, iovcnt);
    if (n < 0)
    {
        *savedErrno = errno;
    }
    else if (n <= writeable)   // extra 没有写入数据
    {
        writerIndex_ += n;
    }   
    else        // extra 写入数据
    {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writeable);
    }

    return n;
}

ssize_t Buffer::writeFd(int fd, int* savedErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *savedErrno = errno;
    }
    return n;
}