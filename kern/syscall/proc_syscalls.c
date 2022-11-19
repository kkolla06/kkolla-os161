// #include <kern/wait.h>
// #include <lib.h>
// #include <machine/trapframe.h>
// #include <clock.h>
// #include <thread.h>
// #include <copyinout.h>
// #include <pid.h>

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <vnode.h>
#include <openfile.h>
#include <filetable.h>
#include <syscall.h>
#include <mips/trapframe.h>

static 
void 
fork_helper(void *tf, unsigned long temp)
{
	(void) temp;

	struct trapframe *new_tf;

	new_tf = kmalloc(sizeof(struct trapframe));
	// my_tf = *new_tf;
	memcpy(new_tf, tf, sizeof(tf));

	enter_forked_process(new_tf);
}

int 
sys_fork(struct trapframe *tf, pid_t *retval) {
    struct trapframe *new_tf;
    
	struct proc *new_proc;

	new_tf = kmalloc(sizeof(struct trapframe));

    if (new_tf == NULL) {
		return ENOMEM;  //Error: Sufficient virtual memory for the new process was not available
	}
    *new_tf = *tf;

    int result = proc_fork(&new_proc);
    if (result) {
		kfree(new_tf);
		return result;
	}
    *retval = new_proc->p_pid;

    result = thread_fork(curthread->t_name, new_proc, fork_helper, new_tf, 0);
	// result = thread_fork(curthread->t_name, new_proc, enter_forked_process, &new_tf, 0);

    if (result) {
		kfree(new_tf);
		return result;
	}

    return 0;
}


int
sys_getpid(pid_t *retval)
{
	*retval = curproc->p_pid;
	return 0;
}


int 
sys_waitpid(pid_t pid, userptr_t status, int flags, pid_t *retval) {
	
	(void) pid;
	(void) status;
	(void) flags;
	(void) retval;
	return 0;
}


void 
sys__exit() {
    proc_destroy(curproc);
	thread_exit();
}