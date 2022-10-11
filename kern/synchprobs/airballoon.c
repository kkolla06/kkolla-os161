/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;

// Total Threads = Dancelion + Marigold + Balloon + Main + FlowerKiller Threads (8)
static int threads_left = N_LORD_FLOWERKILLER + 4;

/* Data structures for rope mappings */
struct rope {
	int rope_num;			
	volatile bool severed;
	struct lock *rope_lk;
};

// functions to create & destroy ropes
struct rope* create_rope (const char *name, int num);
void destroy_rope (struct rope* rope);

struct rope* create_rope(const char *name, int num) {
	struct rope *rp;
    rp = kmalloc(sizeof(struct rope));
    if (rp == NULL) {
        return NULL;
    }

	rp->rope_num = num;
	rp->severed = false;
	rp->rope_lk = lock_create(name);
	
	return rp;
}
void destroy_rope(struct rope* rp)
{
    KASSERT(rp != NULL);
	lock_destroy(rp->rope_lk);
    kfree(rp);
}

struct rope* ropes[NROPES]; // array of ropes
int stakes[NROPES];	// array of stakes: indicates which rope is attached to which stake 


/* Synchronization primitives */
struct lock *rps_left_lk;
struct lock *thread_lk;
struct cv *thread_cv;
struct lock *stakes_lk;

static void setup() {
	rps_left_lk = lock_create("rps_left_lk");
	stakes_lk = lock_create("stakes_lk");
	thread_lk = lock_create("thread_lk");
	thread_cv = cv_create("thread_cv");
	
	for (int i = 0; i < NROPES; i++) {
		ropes[i] = create_rope("rope", i);
		stakes[i] = i;
	}
}
static void clean_up() {
	while (threads_left != 1) {
		thread_yield();
	}

	for (int i = 0; i < NROPES; i++) {
		destroy_rope(ropes[i]);
	}

	lock_destroy(rps_left_lk);
	lock_destroy(stakes_lk);
	lock_destroy(thread_lk);
	cv_destroy(thread_cv);

	kprintf("Main thread done\n");

	// Reset to default value
	ropes_left = NROPES;
	threads_left = N_LORD_FLOWERKILLER + 4;
}

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done? 

Dandelion runs while at least 1 rope is left. It chooses a random rope since Dandelion does not interchange hooks. I first check if 
the rope is already severed, if not, I acquire the rope lock and recheck if the rope is severed while acquiring the lock. The rope 
is then severed and the rope lock is released. I also acquire and release an other lock to decrement the 'ropes_left' counter. Finally, 
a 'thread_lk' lock is acquired to signal the next thread in the CV and the thread then exits.

Marigold works very similar to Dandelion but we access the ropes through stakes and runs while at least 1 rope is left. If the randomly
chosen rope is not severed, I acquire the lock and recheck if the rope is severed while acquiring the lock. The rope is then severed 
and the rope lock is released. I also acquire and release an other lock to decrement the 'ropes_left' counter. Finally, a 'thread_lk' 
lock is acquired to signal the next thread in the CV and the thread then exits.

FlowerKiller also access ropes through stakes. I randomly choose 2 different stakes and check if the ropes are not severed. If not, I 
acquire both the rope locks and recheck if the ropes are severed while acquiring the locks. I also acquire and release an other lock to
modify the stakes array. I ensure the locks are acquired and released in the same order to prevent deadlocks. 

The main thread exits only after all other threads have exited. 

The code works well but takes some time to run when we have 8 N_LORD_FLOWERKILLER. */

static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");
	thread_yield();

	// since hooks cannot change, we directly check if a random rope is severed.
	while (ropes_left > 0) {
		int rp = random() % NROPES;

		if(!ropes[rp]->severed) {
			lock_acquire(ropes[rp]->rope_lk);

			//recheck rope is not severed after acquiring the lock
			if(!ropes[rp]->severed) {
				ropes[rp]->severed = true;
				kprintf("Dandelion severed rope %d\n", rp);
				lock_release(ropes[rp]->rope_lk);

				lock_acquire(rps_left_lk);
				ropes_left--;
				lock_release(rps_left_lk);

				thread_yield();
			} else {
				lock_release(ropes[rp]->rope_lk);
			}
		}
	}

	lock_acquire(thread_lk);
	
	threads_left--;
	cv_signal(thread_cv, thread_lk);
	kprintf("Dandelion thread done\n");
	
	lock_release(thread_lk);

	thread_exit();
}

static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");
	thread_yield();

	while (ropes_left > 0) {

		// access ropes through stakes
		int stake = random() % NROPES;
		int rope = stakes[stake];

		if(!ropes[rope]->severed) {
			lock_acquire(ropes[rope]->rope_lk);

			//recheck rope are not severed while acquiring the lock
			if(!ropes[rope]->severed) {
				ropes[rope]->severed = true;
				kprintf("Marigold severed rope %d from stake %d\n", rope, stake);
				lock_release(ropes[rope]->rope_lk);

				lock_acquire(rps_left_lk);
				ropes_left--;
				lock_release(rps_left_lk);

				thread_yield();
			} else {
				lock_release(ropes[rope]->rope_lk);
			}
		}
	}

	lock_acquire(thread_lk);

	cv_signal(thread_cv, thread_lk);
	kprintf("Marigold thread done\n");
	threads_left--;

	lock_release(thread_lk);

	thread_exit();
}

static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");
	thread_yield();

	while (ropes_left > 0) {

		// acces ropes through stakes
		int stake_old = random() % NROPES;
		int rope_old = stakes[stake_old];
		int stake_new = random() % NROPES;
		int rope_new = stakes[stake_new];

		// If both ropes are the same, choose a different new rope
		if(rope_old == rope_new) {	
			continue;
		}

		if(!ropes[rope_old]->severed && !ropes[rope_new]->severed) {
			lock_acquire(ropes[rope_new]->rope_lk);
			lock_acquire(ropes[rope_old]->rope_lk);

			// Recheck ropes are not severed while acquiring the lock
			if(!ropes[rope_old]->severed && !ropes[rope_new]->severed) {
				lock_acquire(stakes_lk);
				int temp = stakes[stake_old];
				stakes[stake_old] = stakes[stake_new];
				stakes[stake_new] = temp;
				lock_release(stakes_lk);
				lock_release(ropes[rope_old]->rope_lk);
				lock_release(ropes[rope_new]->rope_lk);
				
				kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope_old, stake_old, stake_new);
				kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope_new, stake_new, stake_old);

				thread_yield();
			} else {
				lock_release(ropes[rope_old]->rope_lk);
			}
		}
	}

	lock_acquire(thread_lk);
	
	threads_left--;
	cv_signal(thread_cv, thread_lk);
	kprintf("Lord FlowerKiller thread done\n");
	
	lock_release(thread_lk);

	thread_exit();
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	while (ropes_left != 0) {
		thread_yield();
	}

	lock_acquire(thread_lk);

	threads_left--;
	cv_signal(thread_cv, thread_lk);
	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	kprintf("Balloon thread done\n");

	lock_release(thread_lk);

	thread_exit();
}

// Change this function as necessary
int
airballoon(int nargs, char **args)
{
	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

	setup();

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	goto done;

panic:
	panic("airballoon: thread_fork failed: %s)\n", strerror(err));

done:
	clean_up();
	return 0;
}
