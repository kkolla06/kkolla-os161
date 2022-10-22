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
#include <kern/stat.h>

static
void init_file(struct file *file, int flags)
{
    KASSERT(file != NULL);
    file->lk = lock_create("File lock.");
    file->status = flags;
    file->offset = 0; // what should this be (no need to handle append flag, i think it's okay)
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
    init_file(file, flags);

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
    *retval = buflen - uio.uio_resid;  // uio_resid is the remaining amount to buffer
done:
    lock_release(file->lk);
    return result;
}

int 
sys_write(int fd, const void *buf, size_t nbytes, int32_t *retval) 
{
    // TODO: I assumed nbytes is the length of buf?
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

    uio_uinit(&iov, &uio, (userptr_t)buf, nbytes, file->offset, UIO_WRITE);

    result = VOP_WRITE(file->vn, &uio);
    if (result) {
        goto done;
    }

    file->offset += uio.uio_offset;  // I am adding the offset but does uio_uinit return the updated offset?
    *retval = nbytes - uio.uio_resid;
    
done:
    lock_release(file->lk);

    return result;
}

int
sys_close(int fd) 
{
    struct file *file;

    KASSERT(curproc->p_fds != NULL);
    if (fd < 0 || fd >= OPEN_MAX || curproc->p_fds->files[fd] == NULL) {
        return EBADF;
    }

    file = curproc->p_fds->files[fd];

    lock_acquire(curproc->p_fds->lk);
    lock_acquire(file->lk);
    vfs_close(file->vn); // this will decrement the refcount for this file
    
    if (file->vn->vn_refcount > 0) {
        lock_release(file->lk);
        goto done;
    }
    lock_release(file->lk);
    lock_destroy(file->lk);

    kfree(curproc->p_fds->files[fd]);
done:
    curproc->p_fds->files[fd] = NULL;
    curproc->p_fds->open_count--;
    lock_release(curproc->p_fds->lk);
    
    return 0;
}

int
sys_lseek(int fd, off_t pos, const_userptr_t whence_ptr, int32_t *retval0, int32_t *retval1) 
{
    KASSERT(curproc->p_fds != NULL);
    int result = 0;
    int whence;
    if (fd < 0 || fd >= OPEN_MAX || curproc->p_fds->files[fd] == NULL) {
        return EBADF;
    }
    struct file *file = curproc->p_fds->files[fd];
    if (!VOP_ISSEEKABLE(file->vn)) {
        return ESPIPE;
    }

    lock_acquire(file->lk);
    struct stat *st = kmalloc(sizeof(struct stat *));
    result = VOP_STAT(file->vn, st);
    off_t file_size = st->st_size;
    kfree(st);
    if (result) {
        goto done;
    }

    result = copyin(whence_ptr, &whence, sizeof(int));
    if (result) {
        goto done;
    }

    off_t seek_pos = -1;
    switch (whence) {
        case 0: // SEEK_SET
            seek_pos = pos;
        break;
        case 1: // SEEK_CUR
            seek_pos = file->offset + pos;
        break;
        case 2: // SEEK_END
            seek_pos = file_size + pos;
        break;
        default:
        result = EINVAL;
        goto done;
    }

    if (seek_pos < 0) {
        result = EINVAL;
        goto done;
    }
    file->offset = seek_pos;
    // Kern byte order is Big-Endian, endian.h:42
    *retval1 = seek_pos;
    *retval0 = seek_pos >> 32;
done: 
    lock_release(file->lk);
    return result;
}

int
sys_chdir(const char *pathname) 
{
    if (pathname == NULL) {
        return EFAULT;
    }
    char *kpathname = (char *)kmalloc(PATH_MAX);
    size_t *actual = (size_t *)kmalloc(sizeof(size_t));
    int result = 0;

    // Copy the filename string from  user space to kernel space.
    result = copyinstr((const_userptr_t)pathname, kpathname, PATH_MAX, actual);
    if (result) {
        goto done;    
    }

    result = vfs_chdir(kpathname);
done:
    kfree(kpathname);
    kfree(actual);
    return result;
}

int 
sys___getcwd(char *buf, size_t buflen, int32_t *retval) 
{
	int result;

    struct uio uio;
    struct iovec iov;
    result = 0;

    lock_acquire(curproc->p_fds->lk);

	uio_uinit(&iov, &uio, (userptr_t)buf, buflen, 0, UIO_READ);

	result = vfs_getcwd(&uio);
	if (result) {
		goto done;
	}

	*retval = buflen - uio.uio_resid;   //TODO: unsure

done:
    lock_release(file->lk);

    return result;
}

int 
sys_dup2(int oldfd, int newfd, int32_t *retval) 
{
    KASSERT(curproc->p_fds != NULL);
    int result = 0;
    if (oldfd == newfd) {
        *retval = oldfd;
        return result;
    }
    if (oldfd < 0 || oldfd >= OPEN_MAX || curproc->p_fds->files[oldfd] == NULL ||
        newfd < 0 || newfd >= OPEN_MAX) {
        return EBADF;
    }
    lock_acquire(curproc->p_fds->lk);
    if (curproc->p_fds->open_count == OPEN_MAX) {
        result = EMFILE;
        goto done;
    }

    if (curproc->p_fds->files[newfd] != NULL) {
        // close newfd
        struct file *file = curproc->p_fds->files[newfd];
        lock_acquire(file->lk);
        vfs_close(file->vn);
        // if there are no more refs, we free memory
        if (file->vn->vn_refcount == 0) {
            lock_release(file->lk);
            lock_destroy(file->lk);
            kfree(curproc->p_fds->files[newfd]);
        }
        else {
            lock_release(file->lk);
        }
        curproc->p_fds->files[newfd] = NULL;
        curproc->p_fds->open_count--;
    }
    lock_acquire(curproc->p_fds->files[oldfd]->lk);
    VOP_INCREF(curproc->p_fds->files[oldfd]->vn);
    curproc->p_fds->files[newfd] = curproc->p_fds->files[oldfd];
    lock_release(curproc->p_fds->files[oldfd]->lk);

    curproc->p_fds->open_count++; // should we inc the count on a dup?
    *retval = newfd;
done:
    lock_release(curproc->p_fds->lk);
    return result;
}
