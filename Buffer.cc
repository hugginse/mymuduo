#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

/*
 * 从fd上读取数据, Poller工作在LT模式
 * Buffer缓冲区是有大小的, 但是从fd上读取数据的时候, 却不知道tcp数据最终的大小
 * 
 * @description: 从socket读到缓冲区的方法是使用readv先读至buffer_,
 * Buffer_空间如果不够会读入栈上65536个字节大小的空间, 然后以append的方式追加上buffer_,
 * 考虑了避免系统调用带来的开销, 又不影响数据的接收
*/
ssize_t Buffer::readFd(int fd, int* saveErrno)
{
    // 栈上的额外空间, 用于从套接字往出读时, 当buffer_暂时不够用时暂存数据, 待buffer_重新分配足够空间后, 把数据交换给buffer_
    char extrabuf[65536] = { 0 };             // 栈上的内存空间 64k

    /*
    struct iovec{
        ptr_i iov_base;     // iov_base指向的缓冲区存放的时readv所接收的数据或是writev将要发送的数据
        size_t iov_len;     // iov_len在各种情况下分别确定了接受的最大长度以及实际写入的长度
    };
    */
    
    // 使用iovec分配两个连续的缓冲区
    struct iovec vec[2];

    const size_t writable = writableBytes();    // 这是Buffer底层缓冲区剩余的可写空间的大小
                                                
    // 第一块缓冲区, 指向可写空间
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    // 第二块缓冲区, 指向栈空间
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;

    // when there is enough space in this buffer, don't read into extrabuf.
    // when extrabuf is used, we read 128k-1 bytes at most
    // 这里之所以说最多128k-1字节, 那是因为若writetable为64k-1, 那么需要两个缓冲区 第一个为64k-1 第二个为64k, 所以最多128k-1
    // 如果第一个缓冲区>=64k, 那就只采用一个缓冲区, 而不是用栈空间extrabuf[65536]的内容
    const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0)
    {
        *saveErrno = errno;
    }
    else if (n <= writable)     // Buffer的可写缓冲区已经够存储读出来的数据了
    {
        writerIndex_ += n;
    }
    else  // 表示extrabuf里面也写入了数据
    {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);     // writerIndex_ 开始写 n - writable 大小的数据
    }

    return n;
}

/*
inputBuffer_.readFd表示将对端数据读到inputBuffer_中, 移动writeIndex_指针
outputBuffer_.writeFd表示将数据写入outputBuffer_中, 从readerIndex_开始, 可以写readableBytes()个字节
*/
// 通过fd发送数据
ssize_t Buffer::writeFd(int fd, int* saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *saveErrno = errno;
    }
    return n;
}