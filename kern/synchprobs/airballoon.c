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
static volatile int ropes_left = NROPES;

/* Data structures for rope mappings */

/* Implement this! */
struct rope {
	int rope_num; // rope num is the same as the hook num
	bool severed;

	struct lock* lk;
};

struct stake {
	volatile int rope_num;
	struct lock* lk;
};

static struct rope *ropes[NROPES];
static struct stake *stakes[NROPES];

/* Synchronization primitives */

/* Implement this! */
static struct lock *num_threads_lk;
static struct lock *flower_killer_lk;
static struct lock *kprintf_lk; // this lock will add some more consistency to print statements; makes log more readable.
static volatile int num_threads = N_LORD_FLOWERKILLER + 3;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 * 
 * Data Structures:
 * 	`struct rope`
 * 		- this data structure defines a rope
 * 		- a rope will have a unique rope number (the rope number is also equivalent to a hook number because hooks and ropes dont get swapped around)
 * 		- a rope can be severed or not severed
 * 		- a rope has a lock so that only one actor can handle the rope at a time
 * 	`struct stake`
 * 		- this data structure defines a stake
 * 		- a stake will be able to identify a rope with a rope num which is declared as volatile because it can be changed
 * 		- a stake has a lock so that only one actor can handle the stake at a time
 * 	`struct rope *ropes[]`
 * 		- this is an array used to index our ropes
 * 		- ropes[n] will mean that you are accessing rope n (rope n also corresponds to hook n because hooks do not change); 0 <= n < NROPES
 * 		- there are NROPES ropes in the array
 * 	`struct stake *stakes[]`
 * 		- this is an array used to to index array our stakes
 * 		- stake[n] corresponds to stake n; 0 <= n < NROPES
 * 		- there is one stake per rope in the array
 * 
 * Synchronization Primitives:
 * 	`num_threads` and `num_threads_lk`
 * 		- `num_threads` is a counter to keep track of how many threads are currently running. initialized to N_LORD_FLOWERKILLER + 3
 * 		- `num_threads_lk` a lock that a thread needs to hold before being able to update the running threads count
 * 		- the `num_threads` variable is used to indicate when the balloon function is able to terminate and when the main function is able to terminate
 *  `flower_killer_lk`
 * 		- this lock is used so that only one flower killer thread can acquire locks at a time. This solves a race condition that can occur when the flowerkiller
 * 		  needs to acquire locks before swapping the ropes around
 * 
 * Functions Implemented:
 * 	`dandelion`
 * 		- the dandelion function will run a while loop until the `ropes_left` global variable gets decremented to 0
 * 		- in the loop `dandelion` will pick a random hook number and with this hook number it will index the `ropes` array to get a rope
 * 		- the `dandelion` thread will try to acquire the lock before going any futher
 * 		- once the lock is acquired we check if the rope is severed, if it is not we set the rope to be severed and print out that dandelion severed that rope
 * 		- the lock is then released and the thread yields
 * 		- when the loop exits, we print that dandelion has completed and we acquire the num_threads_lk lock and then decrement num_threads then release num_threads_lk
 * 	`marigold`
 * 		- the marigold function will run a while loop until the `ropes_left` global variable gets decremented to 0
 * 		- in the loop marigold will pick a random stake number and with the stake number it will index the `stakes` array to get a stake
 * 		- the marigold thread will try to acquire the lock that corresponds to the stake before going any further
 * 		- once the lock is acquired for the stake, we use the `rope_num` associated with the stake to index the `ropes` array
 * 		- we then acquire the lock that corresponds to the rope we have indexed in the `ropes` array
 * 		- once we have acquire the lock we can check if the rope is severed, if it is not severed we can set it as severed then print that marigold has severed rope rope_num from hook hook_num
 * 		- the locks that we have acquired are then released and the thread yields
 * 		- the loop will terminate when `ropes_left` gets decremented down to 0
 * 		- once the loop terminates we print that the marigold thread is done then decremenet the num_threads counter once we have acquired the lock for it
 * 	`flowerkiller`
 * 		- the flowerkiller thread will loop until ropes_left is less than 1. at this point flowerkiller can no longer swap ropes so it is done.
 * 		- starts by generating two random stake numbers, stake_num1 and stake_num2
 * 		- the flowerkiller thread will then acquire the flowerkiller_lk lock to prepare for acquiring stake and rope locks (we don't want multiple flowerkiller threads doing this at once because it will lead to race condition)
 * 		- flowerkiller will first acquire the lock corresponding to stakes[stake_num1] and then acquire the rope for the rope_num corresponding to the stake
 * 		- the same step as above will follow for stake_num2
 * 		- once the locks are acquired we swap the rope_num corresponding to stake1 with the rope_num corresponding to stake2. We then report the swaps in print statements
 * 		- flowerkiller then releases the locks and the thread will yield
 * 		- once the loop ends the thread will print that it is has completed and it will decrement the num_threads counter once it has acquired the lock to do so	
 * `balloon`
 * 		- the balloon thread will sit in a while loop until the thread_count gets decremented to 1 which indicates that its the only thread remaining (other than the main thread)
 * 		- once the while loop terminates we print that dandelion has been freed and that the balloon thread has completed
 * 		- we then acquire the num_threads_lk lock and decrement the num_threads counter and release the lock
 * 	`airballoon`
 * 		- this is the main function which allocates the memory needed for the data structures
 * 		- the function will begin by creating the locks for flowerkiller_lk and the num_threads_lk
 * 		- then it will loop through NROPES and allocate the memory for the stake and rope data structures and store them in the stakes, and ropes arrays respectively
 * 		- We then run the threads for marigold, dandelion, flowerkiller * 8, balloon
 * 		- this main thread will sit in a while loop until `num_threads` == 0
 * 		- once num_threads is equal to 0, this means that all threads have completed and dandelion is free
 * 		- we will free all the memory that we previously allocated and reset the num_threads and num_ropes variables for if this function is ran again
 */

static
void
dandelion(void *p, unsigned long arg)
{
	// Dandelion will unhook the ropes on the balloon.
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

	/* Implement this function */
	while (ropes_left > 0) {
		int hook = random() % NROPES; // the hook num and rope num should always be the same becuase hooks don't swap
		lock_acquire(ropes[hook]->lk);
		if (!ropes[hook]->severed) {
			ropes_left--;
			ropes[hook]->severed = true;

			lock_acquire(kprintf_lk);
			kprintf("Dandelion severed rope %d\n", hook);
			lock_release(kprintf_lk);
		}
		else {
			lock_release(ropes[hook]->lk);
			continue; // don't yield if rope was severed.
		}
		lock_release(ropes[hook]->lk);
		thread_yield();
	}

	kprintf("Dandelion thread done\n");
	
	lock_acquire(num_threads_lk);
	num_threads--;
	lock_release(num_threads_lk);
}

static
void
marigold(void *p, unsigned long arg)
{
	// Marigold will sever the stakes on the ground.
	(void)p;
	(void)arg;
	struct rope* rope;

	kprintf("Marigold thread starting\n");

	/* Implement this function */
	while (ropes_left > 0) {
		int stake = random() % NROPES;
		lock_acquire(stakes[stake]->lk);
		rope = ropes[stakes[stake]->rope_num];
		lock_acquire(rope->lk);
		if (!rope->severed) {
			ropes_left--;
			rope->severed = true;
		
			lock_acquire(kprintf_lk);
			kprintf("Marigold severed rope %d from stake %d\n", rope->rope_num, stake);
			lock_release(kprintf_lk);
		}
		else {
			lock_release(rope->lk);
			lock_release(stakes[stake]->lk);
			continue; // don't yield if rope was severed.
		}
		lock_release(rope->lk);
		lock_release(stakes[stake]->lk);
		thread_yield();
	}

	kprintf("Marigold thread done\n");
	
	lock_acquire(num_threads_lk);
	num_threads--;
	lock_release(num_threads_lk);
}

static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	struct rope *rope1, *rope2;
	int stake_num1, stake_num2;	

	kprintf("Lord FlowerKiller thread starting\n");

	/* Implement this function */
	while (ropes_left > 1) {
		stake_num1 = random() % NROPES;
		stake_num2 = random() % NROPES;

		lock_acquire(flower_killer_lk);
		lock_acquire(stakes[stake_num1]->lk);
		rope1 = ropes[stakes[stake_num1]->rope_num];
		rope2 = rope1; // set to rope1 jic stake_num1 == stake_num2
		lock_acquire(rope1->lk);
		if (stake_num1 != stake_num2) {
			lock_acquire(stakes[stake_num2]->lk);
			rope2 = ropes[stakes[stake_num2]->rope_num];
			lock_acquire(rope2->lk);
		}
		lock_release(flower_killer_lk);

		// if the ropes are severed already we cannot swap them. Release locks.
		if (rope1->severed || rope2->severed) {
			if (stake_num1 != stake_num2) {
				lock_release(rope2->lk);
				lock_release(stakes[stake_num2]->lk);
			}
			lock_release(rope1->lk);
			lock_release(stakes[stake_num1]->lk);
			continue;
		}

		// swap the ropes for the two stakes
		stakes[stake_num1]->rope_num = rope2->rope_num;
		stakes[stake_num2]->rope_num = rope1->rope_num;

		lock_acquire(kprintf_lk);
		kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope1->rope_num, stake_num1, stake_num2);
		kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope2->rope_num, stake_num2, stake_num1);
		lock_release(kprintf_lk);
		if (stake_num1 != stake_num2) {
			lock_release(rope2->lk);
			lock_release(stakes[stake_num2]->lk);
		}
		lock_release(rope1->lk);
		lock_release(stakes[stake_num1]->lk);

		thread_yield();
	}

	kprintf("Lord FlowerKiller thread done\n");
	
	lock_acquire(num_threads_lk);
	num_threads--;
	lock_release(num_threads_lk);
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	/* Implement this function */
	while (num_threads > 1) {/* maybe we make this sleep on a cv */}

	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	kprintf("Balloon thread done\n");
	
	lock_acquire(num_threads_lk);
	num_threads--;
	lock_release(num_threads_lk);
}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{
	int err = 0, i;
	num_threads_lk = lock_create("Num Threads Lock");
	flower_killer_lk = lock_create("Flower killer lock");
	kprintf_lk = lock_create("kprintf lock");
	for (i = 0; i < NROPES; i++) {
		ropes[i] = (struct rope *)kmalloc(sizeof(struct rope));
		ropes[i]->rope_num = i;
		ropes[i]->severed = false;
		ropes[i]->lk = lock_create("A rope lock");

		stakes[i] = (struct stake *) kmalloc(sizeof(struct stake));
		stakes[i]->rope_num = i;
		stakes[i]->lk = lock_create("A stake lock");
	}

	(void)nargs;
	(void)args;
	(void)ropes_left;

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
	
	// Wait for threads to complete
	while (num_threads != 0) {}
	// cleanup & reset
	// make sure nobody is holding the lock before destroying it
	lock_acquire(num_threads_lk);
	lock_release(num_threads_lk);

	lock_destroy(num_threads_lk);
	lock_destroy(flower_killer_lk);
	lock_destroy(kprintf_lk);
	for (i = 0; i < NROPES; i++) {
		lock_destroy(ropes[i]->lk);
		kfree(ropes[i]);
		ropes[i] = NULL;

		lock_destroy(stakes[i]->lk);
		kfree(stakes[i]);
		stakes[i] = NULL;
	}
	num_threads = N_LORD_FLOWERKILLER + 3;
	ropes_left = NROPES;

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	kprintf("Main thread done\n");
	return 0;
}
