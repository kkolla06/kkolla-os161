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

/* Data structures for rope mappings */

// functions to create & destroy ropes

/* Implement this! */
struct rope {
	int rope_num;			// TODO: needed?
	volatile bool severed;
	struct lock *rope_lk;
};

struct rope *create_rope (const char *name, int num);
void destroy_rope (struct rope* rope);

struct rope *create_rope(const char *name, int num) {
	struct rope *rope;
    rope = kmalloc(sizeof(struct rope));
    if (rope == NULL) {
        return NULL;
    }

	rope->rope_num = num;
	rope->severed = false;
	rope->rope_lk = lock_create(name);
	
	return rope;
}
void destroy_rope(struct rope* rope)
{
    KASSERT(rope != NULL);
	lock_destroy(rope->rope_lk);
    kfree(rope);
}

struct rope* ropes[NROPES]; // array of ropes
int stakes[NROPES];	// array of stakes: indicates which rope is attached to which stake 



/* Synchronization primitives */
struct lock *ropes_left_lk;
struct lock *thread_lk;
struct cv *thread_cv;
struct lock *stakes_lk;

/* Implement this! */

static void setup() {

	ropes_left_lk = lock_create("ropes_left");
	thread_lk = lock_create("thread_lk");
	thread_cv = cv_create("thread_cv");
	stakes_lk = lock_create("stakes_lk");
	
	for (int i = 0; i < NROPES; i++) {
		ropes[i] = create_rope("rope", i);
		stakes[i] = i;
	}
}

static void clean_up() {
	for (int i = 0; i < NROPES; i++) {
		destroy_rope(ropes[i]);		
	}

	lock_destroy(ropes_left_lk);
	lock_destroy(thread_lk);
	lock_destroy(stakes_lk);
	cv_destroy(thread_cv);

	kprintf("Main thread done\n");

	// Reset to default value
	ropes_left = NROPES;
}

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

	/* Implement this function */

	// since hooks cannot change, we directly check if the random rope is severed.
	while (ropes_left > 0) {
		int rope = random() % NROPES;

		if(!ropes[rope]->severed) {
			lock_acquire(ropes[rope]->rope_lk);

			//recheck rope are not severed after acquiring the lock
			if(!ropes[rope]->severed) {
				ropes[rope]->severed = true;
				kprintf("Dandelion severed rope %d\n", rope);

				lock_acquire(ropes_left_lk);
				ropes_left--;
				lock_release(ropes_left_lk);

				lock_release(ropes[rope]->rope_lk);
				thread_yield();
			} else {
				lock_release(ropes[rope]->rope_lk);
				thread_yield();		//TODO: needed?
			}
		}
	}

	lock_acquire(thread_lk);
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

	/* Implement this function */

	// access ropes through stakes

	while (ropes_left > 0) {
		int stake = random() % NROPES;
		int rope = stakes[stake];

		if(!ropes[rope]->severed) {
			lock_acquire(ropes[rope]->rope_lk);

			//recheck rope are not severed after acquiring the lock
			if(!ropes[rope]->severed) {
				ropes[rope]->severed = true;
				kprintf("Marigold severed rope %d from stake %d\n", rope, stake);

				lock_acquire(ropes_left_lk);
				ropes_left--;
				lock_release(ropes_left_lk);

				lock_release(ropes[rope]->rope_lk);
				thread_yield();
			} else {
				lock_release(ropes[rope]->rope_lk);
				thread_yield();		//TODO: needed?
			}
		}
	}

	lock_acquire(thread_lk);
	cv_signal(thread_cv, thread_lk);
	kprintf("Marigold thread done\n");
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

	/* Implement this function */

	// acces ropes through stakes

	while (ropes_left > 0) {
		int stake_old = random() % NROPES;
		int rope_old = stakes[stake_old];
		int stake_new = random() % NROPES;
		int rope_new = stakes[stake_new];

		// If both ropes are the same, choose a different new rope
		if(rope_old == rope_new) {	
			stake_new = random() % NROPES;
			rope_new = stakes[stake_new];
		}

		if(!ropes[rope_old]->severed && !ropes[rope_new]->severed) {
			lock_acquire(ropes[rope_new]->rope_lk);
			lock_acquire(ropes[rope_old]->rope_lk);

			// Recheck ropes are not severed after acquiring the lock
			if(!ropes[rope_old]->severed && !ropes[rope_new]->severed) {
				lock_acquire(stakes_lk);
				int temp = stakes[stake_old];
				stakes[stake_old] = stakes[stake_new];
				stakes[stake_new] = temp;
				lock_release(stakes_lk);
				
				kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope_old, stake_old, stake_new);
				kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope_new, stake_new, stake_old);

				lock_release(ropes[rope_new]->rope_lk);
				thread_yield();
			} else {
				lock_release(ropes[rope_old]->rope_lk);
				thread_yield();		//TODO: needed?
			}
		}
	}

	lock_acquire(thread_lk);
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

	/* Implement this function */

}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{

	// int err = 0, i;
	int err = 0;

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

	// for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
	// 	err = thread_fork("Lord FlowerKiller Thread",
	// 			  NULL, flowerkiller, NULL, 0);
	// 	if(err)
	// 		goto panic;
	// }
	err = thread_fork("Lord FlowerKiller Thread",
				NULL, flowerkiller, NULL, 0);
	if(err)
		goto panic;


	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	clean_up();
	return 0;
}
