#ifndef _FILE__SYSCALLS_H_
#define _FILE__SYSCALLS_H_

#include <vnode.h>
#include <types.h>
#include <synch.h>

struct file {
    off_t offset;           // current position in the file
    int status;             // value for the read and write flags
    struct vnode *vn;       
    struct lock *lk;        // lock so that only one thread can access this file at time
    int refcount;           // count for number of refrences to the file
};

struct fds {
    // if an index is null then the fd is free.
    struct file **files;    // 0, 1, 2 are taken for stdin, stdout, stderr
    struct lock *lk;        // lock so that only one thread can access this struct a time
    int open_count;         // used to check if we have reached max open files
};

int sys_open(const char *filename, int flags, int32_t *retval);
int sys_read(int fd, void *buf, size_t buflen, int32_t *retval);
int sys_write(int fd, const void *buf, size_t nbytes);
int sys_close(int fd);
int __getcwd(char *buf, size_t buflen);
#endif /* _FILE__SYSCALLS_H_ */