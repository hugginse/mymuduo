# mymuduo

****
## 阻塞、非阻塞、同步、异步

> 典型的一次IO的两个阶段是什么？ `数据准备`和`数据读写`

数据准备：根据系统IO操作的就绪状态
* 阻塞
* 非阻塞

数据读写：根据应用程序和内核的交互方式
* 同步
* 异步

**在处理IO的时候，阻塞和非阻塞都是同步IO，只有使用了特殊的API才是异步IO**

## Unix/Linux上的五种IO模型

### 阻塞 blocking

### 非阻塞 non-blocking

### IO复用 (IO multiplexing)

### 信号驱动(signal-driven)

### 异步(asynchronous)

## 异步
```cpp
struct aiocb{
    int                 aio_fildes;
    off_t               aio_offset;
    volatile void       *aio_buf;
    size_t              aio_nbytes;
    int                 aio_reqprio;
    sturct sigevent     aio_sigevent;
    int                 aio_lio_opcode;
};
```

******
## 良好的网络服务器设计

libev作者: one loop per thread is usually a good model.
多线程服务端编程问题=> 设计一个高效且易于使用的event loop, 然后每个线程run一个event loop(线程同步、互斥，另起线程处理其他任务)

event loop 是non-blocking网络编程的核心，non-blocking常和multiplexing一起使用
> * 使用轮询(busy-pooling)来检查某个non-blocking IO操作是否完成，会浪费CPU资源
> * IO-multiplexing一般不和blocking IO用在一起。因为blocking IO中的read()/write()/accept()/connect()等等方法可能阻塞当前线程，导致线程无法处理其他socket上的IO事件

因此，日常提到non-blocking时，实际上是指non-blocking + IO-multiplexing，单用其中任何一个都没有办法很好的实现功能

## Reactor模型
> The reactor design pattern is an event handing pattern for handing service requests delivered concurrently to a service handler by one or more inputs. The service handler then demultiplexes the incoming requests and dispatches them synchronously to the associated requests handlers.


## epoll
* **select和poll的缺点**
* select缺点
> 1. 单个进程能够监视的文件描述符的数量存在最大限制，通常是1024，可以通过修改最大数量，但由于select采用轮询的的方式扫描文件描述符，文件描述符数量越多，性能越差(在linux内核头文件中，有这样的定义:#define _FD_SETSIZE 1024)
> 2. 内核/用户空间内存拷贝问题，select需要赋值大量的句柄数据结构，产生巨大的开销
> 3. select返回的是含有整个句柄的数组，应用程序需要遍历整个数组才能法相哪些句柄发生了事件libev作者
> 4. select的触发方式是水平触发，应用程序如果没有完成对一个已经就绪的文件描述符进行IO操作，那么之后每次select调用还是会将这些文件描述符通知进程

* poll使用`链表`保存文件描述符，因此没有监视文件数量的限制，但是其他三个缺点依然存在


## epoll原理以及优势
epoll的实现机制与select/poll机制完全不同，
> 场景问题：有100万个客户端同时与一个服务器进程保持TCP连接。而每一时刻，通常只有几百上千个TCP连接时活跃的（大部分现实场景），如何实现高并发?

> 在select/poll时代，服务器进程每次把这100万个连接告诉操作系统(从用户态复制句柄数据结构到内核态)，让操作系统内核去查询这些套接字上是否有事件发生，在轮询完成后，再将句柄数据结构复制到用户态，让服务器应用程序轮询处理已发生网络事件，这一过程资源消耗巨大，因此，select/poll一般只能处理几千的并发连接

epoll的设计和实现与select完全不同。epoll通过在Linux内核中申请一个简易的文件系统(文件系统一般采用B+树数据结构实现：磁盘IO消耗低，效率很高)。把原先的select/poll调用分成以下3部分：
> 1. 调用epoll_create()建立一个epoll对象(在epoll文件系统中为这个句柄对象分配资源)
> 2. 调用epoll_ctl向epoll对象中添加这100万个连接的套接字
> 3. 调用epoll_wait收集发生的事件的fd资源

如此一来，要实现上面的场景，只需要在进程启动时建立epoll对象，然后在需要的时候向这个epoll对象中添加或者删除事件。同时，epoll_wait的效率也非常高，调用epoll_wait时无需向操作系统复制这100万个连接的句柄数据，内核也不需要去遍历全部的连接。


```cc
struct eventpoll{
    ...
    /*红黑树的根节点，这棵树中存储着左右添加到epoll中的需要监控的事件*/
    sturct rb_root rbr;
    /*双链表中则存放着将要通过epoll_wait返回给用户的满足条件的事件*/
    struct list_head rdlist;
    ...
}
```
