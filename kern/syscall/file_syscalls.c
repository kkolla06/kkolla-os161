#include <types.h>
#include <file_syscalls.h>
#include <syscall.h>
#include <proc.h>
#include <current.h>
#include <limits.h>
#include <copyinout.h>
#include <vfs.h>
#include <kern/errno.h>
#include <vnode.h>
#include <uio.h>
#include <kern/fcntl.h>

static
void init_file(struct file *file, int fd, int flags)
{
    KASSERT(file != NULL);
    file->lk = lock_create("File lock.");
    file->status = flags;
    file->offset = 0; // what should this be.
    file->refcount = 1;
}

static
int get_next_fd()
{
    KASSERT(lock_do_i_hold(curproc->p_fds->lk));
    
    int fd = OPEN_MAX+1;
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->p_fds->files[i] == NULL) {
            fd = i;
            break;
        }
    }
    return fd;
}

int
sys_open(const char *filename, int flags, int32_t *retval)
{
    char *kern_filename = (char *)kmalloc(PATH_MAX);
    size_t *actual = kmalloc(sizeof(size_t));
    KASSERT(kern_filename != NULL);
    KASSERT(actual != NULL);
    struct file *file;
    int result = 0;
    int fd;

    // Copy the filename string from  user space to kernel space.
    result = copyinstr((const_userptr_t)filename, kern_filename, PATH_MAX, actual); // actual will hold actual bytes read
    if (result) {
        goto copyin_error;
    }

    lock_acquire(curproc->p_fds->lk);
    fd = get_next_fd();

    if (fd > OPEN_MAX) {
        result = EMFILE;
        goto done;
    }

    // create and init a file
    file = kmalloc(sizeof(struct file));
    init_file(file, fd, flags);

    // vfs_open will handle opening the file and setting the vnode
    result = vfs_open(kern_filename, flags, 0, &(file->vn)); 
    if (result) {
        lock_destroy(file->lk);
        kfree(file);
        file = NULL;
        goto done;
    }
    curproc->p_fds->files[fd] = file;
    *retval = fd;
    curproc->p_fds->open_count++;
done: 
    lock_release(curproc->p_fds->lk);
copyin_error:
    kfree(kern_filename);
    kfree(actual);
    return result;
}

int
sys_read(int fd, void *buf, size_t buflen, int32_t *retval)
{
    int result;
    struct file *file;
    KASSERT(curproc->p_fds != NULL);
    if (fd < 0 || fd >= OPEN_MAX || curproc->p_fds->files[fd] == NULL) {
        return EBADF;
    }
    file = curproc->p_fds->files[fd];
    if (file->status & O_WRONLY) {
        return EBADF;
    }
    struct uio uio;
    struct iovec iov;
    result = 0;
    lock_acquire(file->lk);

    uio_uinit(&iov, &uio, (userptr_t)buf, buflen, file->offset, UIO_READ);

    result = VOP_READ(file->vn, &uio);
    if (result) {
        goto done;
    }

    file->offset = uio.uio_offset;
    *retval = uio.uio_resid;
done:
    lock_release(file->lk);
    return result;
}

int 
sys_write(int fd, const void *buf, size_t nbytes) {
    int result;
    struct file *file;
    KASSERT(curproc->p_fds != NULL);
    if (fd < 0 || fd >= OPEN_MAX || curproc->p_fds->files[fd] == NULL) {
        return EBADF;
    }
    file = curproc->p_fds->files[fd];
    if (file->status & O_WRONLY) {
        return EBADF;
    }

    struct uio uio;
    struct iovec iov;
    result = 0;
    lock_acquire(file->lk);
}

int
sys_close(int fd) {
    struct file *file;

    KASSERT(curproc->p_fds != NULL);
    if (fd < 0 || fd >= OPEN_MAX || curproc->p_fds->files[fd] == NULL) {
        return EBADF;
    }

    file = curproc->p_fds->files[fd];
    if (file->status & O_WRONLY) {
        return EBADF;
    }

    lock_acquire(curproc->p_fds->lk);

    kfree(file->kern_filename);
    kfree(file);
    // vfs_close(file);

    curproc->p_fds->open_count--;
    lock_release(curproc->p_fds->lk);
    
    return 0;
}

int 
__getcwd(char *buf, size_t buflen) {

}