#ifndef _CURRENTTHREAD_H_
#define _CURRENTTHREAD_H_

#include <unistd.h>
#include <sys/syscall.h>

namespace CurrentThread
{
    //__thread <==> thread_local
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

#endif 