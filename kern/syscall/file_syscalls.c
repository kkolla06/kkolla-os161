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

/*
 * Initialze a fields of a `struct file` while providing the file's flags.
 * Offset is set to 0 when the file is opened becuase O_APPEND is not required. 
 * 
 * Params: struct file * for the struct we are initializing, int for the file flags
 */
static
void init_file(struct file *file, int flags)
{
    KASSERT(file != NULL);
    file->lk = lock_create("File lock.");
    file->status = flags;
    file->offset = 0; // what should this be (no need to handle append flag, i think it's okay)
}

/*
 * Returns an available file descriptor.
 *
 * It will search through our file descriptors and return the 
 * smallest available file descriptor. Returns OPEN_MAX+1 if 
 * no file descriptors are available. 
 */
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

/*
 * The open syscall. 
 *
 * Opens a file, `filename`, with `flags` and stores the file descriptor in `retval`.
 * The process's `struct fds` will be updated to reflect the new open file.
 * 
 * Params: const char * for the filename, int for file flags, int32_t * for user return value.
 * Return: Returns 0 on success, otherwise an error code is returned. *retval stores the fd of
 *         file we open. 
 */
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

/*
 * The read syscall.
 *
 * Reads up to `buflen` bytes from the file specified by `fd` at its current offset,
 * and stores the bytes in `buf`. The file must be open for reading.
 * 
 * Params:  `fd` is the file descriptor associated to the file we want to read, 
 *          `buf` is a buffer where we will be reading data into, `buflen` is 
 *          a non-negative value and is the size of `buf`, `retval` is where 
 *          we will store the user return value.
 * Returns: Returns 0 on success, otherwise returns an error code.
 *          The user return value is stored in `retval`, this value will
 *          be the number of bytes read into `buf`.
 */
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

/*
 * The write syscall.
 * 
 * Writes up to `nbytes` bytes to the file associated with `fd` at its current offset.
 * The data to be written will be taken from `buf` and the file must be open for writing.
 * 
 * Params: `fd` is a file descriptor associated to a file we will write to, 
 *         `buf` is the buffer containing the bytes we want to write to a file,
 *         `nbytes` is the size of `buf`, `retval` will hold the return value that
 *         is returned to the user.
 * Return: Returns 0 on success, otherwise an error code is returned. 
 *         `retval` will store the user return value which will be the
 *         the number of bytes written to the file associated to `fd`.
 */
int 
sys_write(int fd, const void *buf, size_t nbytes, int32_t *retval) 
{
    int result;
    struct file *file;
    KASSERT(curproc->p_fds != NULL);
    if (fd < 0 || fd >= OPEN_MAX || curproc->p_fds->files[fd] == NULL) {
        return EBADF;
    }
    file = curproc->p_fds->files[fd];
    if (file->status & O_RDONLY) {
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

    file->offset = uio.uio_offset;  
    *retval = nbytes - uio.uio_resid;
done:
    lock_release(file->lk);

    return result;
}

/*
 * The close syscall.
 * 
 * Close the file handle `fd`, decrement the refcount of the file associated to this `fd`.
 * 
 * Params: `fd` is the file decsriptor that we will close. 
 * Return: Return 0.
 */
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

/*
 * The lseek syscall. 
 *
 * Alter the current seek position of the file associated to `fd`, seeking to a new position
 * based on `pos` and `whence_ptr`.
 * 
 * Params: `fd` is a file descriptor for a file we want to update the offset for,
 *         `pos` is a value used for updating the offset, `whence_ptr` will indicate
 *         how we want to update the file's offset, `retval0` will hold the upper
 *         32-bits of the return value and `retval1` will hold the lower 32-bits.
 * Return: Returns 0 on success, otherwise an error code will be returned. 
 *         The user return value is 64-bits and is stored in `retval0` and `retval1`. 
 *         The lower 32-bits for the user return value are stored in `retval1`, the upper
 *         32-bits are stored in `retval0`.
 */
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

/*
 * The chdir syscall.
 *
 * The current directory of the current process is set to the directory named in `pathname`.
 * 
 * Params: `pathname` holds the name we want to change directories to.
 * Return: Return 0 on success, otherwise return an error code.
 */
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

    lock_acquire(curproc->p_fds->lk);
    result = vfs_chdir(kpathname);
    lock_release(curproc->p_fds->lk);
done:
    kfree(kpathname);
    kfree(actual);
    return result;
}

/*
 * The __getcwd syscall.
 *
 * The name of the current directory will be fetched and stored in `buf`.
 * 
 * Params: `buf` is the userspace buffer where we will read the cwd into.
 *         `buflen` (must be non-negative) is the size allocated for `buf`, 
 *         `retval` is where we will store the user return value.
 * Return: Returns 0 on success, otherwise an error code is returned. `retval` will
 *         store the length of the cwd, this will be returned to the user.
 */
int 
sys___getcwd(char *buf, size_t buflen, int32_t *retval) 
{   
    if (buf == NULL) {
        return EFAULT;
    }
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

	*retval = buflen - uio.uio_resid;
done:
    lock_release(curproc->p_fds->lk);
    return result;
}

/*
 * The dup2 syscall.
 *
 * Duplicate the contents pointed to by `oldfd` to `newfd`. If `newfd` is currently in use we will close
 * that file before duplicating `oldfd`. The refcount associated to the vnode for `oldfd` will be incremented
 * upon duplication.
 *
 * Params: `oldfd` is the old descriptor we will duplicate, `newfd` is the descriptor we will update,
 *         `retval` is where we will store the return value for the user.
 * Return: On success we will return 0, otherwise an error code is returned. `retval` will store the 
 *         file descriptor of the newfd which will be a duplicate of oldfd on success.
 */
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
