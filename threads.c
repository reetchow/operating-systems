#include <stdio.h>
#include <pthread.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <semaphore.h>

#define JB_BX 0
#define JB_SI 1
#define JB_DI 2
#define JB_BP 3
#define JB_SP 4
#define JB_PC 5

#define MAX_THREADS 128
#define STACK_SIZE 32767

#define RUNNING 1
#define READY 2
#define EXITED 3
#define BLOCKED 4

#define THREAD_CNT 5

void * change(void * ret);
void schedule();
void timer();
void choose_next_thread();
static int ptr_demangle(int p);
static int ptr_mangle(int p);
void pthread_exit(void *retval);
int pthread_create(pthread_t *thread,
		const pthread_attr_t *attr,
                void *(*start_routine)
		(void *), void *arg);

typedef struct tcb tcb;

struct tcb
{
	pthread_t id;
	jmp_buf registers;
	void * stack;
	int status;
	tcb* waiting_on_me;
	int initialized;
	int index;
	void* retval;
};

typedef struct
{
	int id;
	int value;
	int initialized;
	int num_waiting;
	tcb* waiting_threads[128];
}semaphore;

//first pthread call is true
int first = 1;
//global array of all thread control blocks
tcb threads[MAX_THREADS];

//thread IDs
pthread_t thread_id = 1;
//current number of threads
int thread_count = 0;
//next available index in the threads array
int next_available = 0;
//index of currently running thread
int current_thread = 0;

//global array of sem_t and semaphores
sem_t sem_ts[128];
semaphore semaphores[128];

//semaphore count
int sem_count = 0;
//semaphore IDs
int sem_id = 0;

int index_from_align(sem_t* sem)
{
	//get the index of the semaphore from the __align variable in corresponding sem_t
	int id = sem->__align;
	int i = 0;
	for(i = 0; i < 128; i++)
		if(semaphores[i].id == id)
			return i;
}

int sem_find()
{
	int i = 0;
	for(i = 0; i < 128; i++)
		if(semaphores[i].initialized == 0)
			return i;
}

void sem_pop(semaphore* sem)
{
	int i = 0;
	for(i = 0; i < 128; i++)
	{
		if(i == 0)
		{
			sem->waiting_threads[0]->status = READY;
			sem->waiting_threads[i] = sem->waiting_threads[i + 1];
		} else if (i != 127) {
			sem->waiting_threads[i] = NULL;
		}
	}
}

void sem_push(semaphore* sem, tcb* thread)
{
	int i = 127;
	for(i = 127; i >= 0; i--)
	{
		if(i != 0)
		{
			sem->waiting_threads[i] = sem->waiting_threads[i - 1];

		} else {
			sem->waiting_threads[i] = thread;
		}
	}
}

int sem_init(sem_t* sem, int pshared, unsigned value)
{
	//initialize unnamed semaphore
	//semaphores[sem_count].id = sem_id;
	//sem_id++;

	//find an index in the semaphore table
	int index = sem_find();
	//set sem pointer to reference the created semaphore
	sem = &sem_ts[index];
	//sem_t references the semaphore that will be created at that index
	sem->__align = index;

	//semaphore id
	semaphores[index].id = sem_id;
	//initial value
	semaphores[index].value = value;

	semaphores[index].num_waiting = 0;
	//semaphore has been initialized
	semaphores[index].initialized = 1;

	//one more semaphore has been created
	sem_count++;
}

int sem_wait(sem_t* sem)
{
	//sem_wait will decrement the semaphore referred to by sem
	int index = index_from_align(sem);

	//if the semaphores value is greater than zero the decrement proceeds and the function returns immediately
	if(semaphores[index].value > 0)
	{
		semaphores[index].value--;
		return 0;
	}

	//if the semaphore currently has the value 0
	else if(semaphores[index].value == 0)
	{
		//then the call blocks until it becomes possible to perform the decrement
		threads[current_thread].status = BLOCKED;
		sem_push(&semaphores[index], &threads[current_thread]);
		semaphores[index].num_waiting++;
		schedule();
		semaphores[index].value--;
		return 0;
	}
	//for this project the value of the semaphore never falls below zero
}

int sem_post(sem_t* sem)
{
	//sem_post increments the semaphore pointed to by sem.
	int index = index_from_align(sem);
	semaphores[index].value++;
	//if the semaphores value consequently becomes greater then 0
	if(semaphores[index].num_waiting > 0)
	{
		//then another thread blocked in a sem_wait call will be woken up and proceeds to lock the semaphore
		sem_pop(&semaphores[index]);
		semaphores[index].num_waiting--;
		schedule();
	} else {
		//then another thread blocked in a sem_wait call will be woken up and proceeds to lock the semaphore
		//sem_pop(&semaphores[index]);
		schedule();
	}
	//note that when a thread is woken up and takes the lock as part of sem_post, the value of the semaphore will remain zero
}

int sem_destroy(sem_t* sem)
{
	int index = index_from_align(sem);

	semaphores[index].id = 0;
	semaphores[index].value = 0;
	semaphores[index].initialized = 0;

	int i = 0;
	for(i = 0; i < 128; i++)
		sem_pop(&semaphores[index]);
	//sem_destroy destroys the semaphore specified at the address pointed to by sem 
	//which means that only a semaphore that has been initialized by sem_init should be destroyed using sem_destroy
	//destroying a semaphore that other threads are currently blocked on (in sem_wait) produces undefined results
	//until the semaphore has been reinitialized using sem_init
}

void sig_handler(int signo)
{
	fprintf(stderr, "SIGNAL HANDLER TRIGGERED\n");
	schedule();
}

void timer()
{
	struct sigaction action;
	action.sa_handler = sig_handler;
	action.sa_flags = SA_NODEFER;
	sigaction(SIGALRM, &action, NULL);

	ualarm(50000, 50000);

	//printf("TIMER INITIALIZED\n");
}

void lock()
{
        sigset_t sig;
        sigemptyset(&sig);
        sigaddset(&sig, SIGALRM);
        sigprocmask(SIG_BLOCK, &sig, NULL);
}

void unlock()
{
        sigset_t sig;
        sigemptyset(&sig);
        sigaddset(&sig, SIGALRM);
        sigprocmask(SIG_UNBLOCK, &sig, NULL);
}

int index_from_thread_id(pthread_t tid)
{
	fprintf(stderr, "SEARCHING\n");
	int i;
	for(i = 0; i < MAX_THREADS; i++)
	{
		fprintf(stderr, "tid: %d, threads[i].id: %d, threads[i].index: %d\n", tid, threads[i].id, threads[i].index);
		if(threads[i].id == tid)
		{
			//fprintf(stderr, "tid: %d, threads[i].id: %d, threads[i].index: %d\n", tid, threads[i].id, threads[i].index);
			return threads[i].index;
		}
	}
	return -1;
}

void print_threads()
{
	printf("\nthreads\n-------\n");
	int i;
	for(i = 0; i < MAX_THREADS; i++)
	{

		if(threads[i].status == READY)
			printf("[id: %d, index: %d, state: READY, waiting_on_me: %x]\n", threads[i].id, threads[i].index, threads[i].waiting_on_me);
		else if(threads[i].status == BLOCKED)
			if(threads[i].waiting_on_me != 0)
				printf("[id: %d, index: %d, state: BLOCKED, waiting_on_me: %x, thread: %d]\n", threads[i].id, threads[i].index, threads[i].waiting_on_me, threads[i].waiting_on_me->id);
			else
				printf("[id: %d, index: %d, state: BLOCKED, waiting_on_me: %x]\n", threads[i].id, threads[i].index, threads[i].waiting_on_me);
		else if(threads[i].status == RUNNING)
			printf("[id: %d, index: %d, state: RUNNING, waiting_on_me: %x]\n", threads[i].id, threads[i].index, threads[i].waiting_on_me);
		else if(threads[i].status == EXITED)
			printf("[id: %d, index: %d, state: EXITED, waiting_on_me: %x]\n", threads[i].id, threads[i].index, threads[i].waiting_on_me);
	}
	printf("\n");
}

void print_threads_stderr()
{
	fprintf(stderr, "\nthreads\n-------\n");
	int i;
	for(i = 0; i < MAX_THREADS; i++)
	{
		if(threads[i].status == READY)
			fprintf(stderr, "[id: %d, index: %d, state: READY, waiting_on_me: %x]\n", threads[i].id, threads[i].index, threads[i].waiting_on_me);
		else if(threads[i].status == BLOCKED)
			fprintf(stderr, "[id: %d, index: %d, state: BLOCKED, waiting_on_me: %x, thread: %d]\n", threads[i].id, threads[i].index, threads[i].waiting_on_me, threads[i].waiting_on_me->id);
		else if(threads[i].status == RUNNING)
			fprintf(stderr, "[id: %d, index: %d, state: RUNNING, waiting_on_me: %x]\n", threads[i].id, threads[i].index, threads[i].waiting_on_me);
		else if(threads[i].status == EXITED)
			fprintf(stderr, "[id: %d, index: %d, state: EXITED, waiting_on_me: %x]\n", threads[i].id, threads[i].index, threads[i].waiting_on_me);
	}
	fprintf(stderr, "\n");
}

void choose_next_thread()
{
	//check each potential next thread starting from current + 1
	int i, potential_next;
	potential_next = current_thread + 1;

	//loop through the array of threads
	for(i = 0; i < MAX_THREADS; i++)
	{
		//if we've hit the last thread, start searching at 0
		if(potential_next == MAX_THREADS)
			potential_next = 0;

		//if the thread is READY then make it the current_thread and exit the checking loop
		if(threads[potential_next].status == READY)
		{
			current_thread = potential_next;
			break;
		}

		//check the next index
		potential_next++;
	}

	//next thread is set to RUNNING
	threads[current_thread].status = RUNNING;
}

void schedule()
{
	fprintf(stderr, "\n--------------THIS IS THE SCHEDULER--------------\n");
	print_threads();

	//save context of thread we just came out of
	if(setjmp(threads[current_thread].registers) != 0)
		return;

	//if the thread we just came out of hasn't exited and isn't blocked then set it from running to ready
	if(threads[current_thread].status != EXITED && threads[current_thread].status != BLOCKED)
		threads[current_thread].status = READY;

	printf("PREVIOUS THREAD INDEX: %d\n", current_thread);
	//print_threads_stderr();
	choose_next_thread();
	printf("NEXT THREAD INDEX: %d\n", current_thread);

	//fprintf(stderr, "Scheduling thread #%d\n", current_thread);
	print_threads();
	//switch context to next thread
	longjmp(threads[current_thread].registers, 1);
}

int pthread_join(pthread_t thread, void ** retval)
{
	fprintf(stderr, "\n    PTHREAD JOIN, current_thread: %d, target_thread: %d\n", current_thread, thread);
	//find the index of the thread that we're going to wait for
	int index = index_from_thread_id(thread);

	//if thread index is valid
	if(index != -1)
	{
		fprintf(stderr, "target index = %d\n", index);

		if(threads[index].status == EXITED && retval != NULL)
		{
			*retval = threads[thread].retval;
			return 0;
		}

		//make the current thread block on thread
		threads[current_thread].status = BLOCKED;

		//when pthread_exit is called, it can refer to this pointer to set this thread back to READY
		threads[index].waiting_on_me = &threads[current_thread];
		schedule();
		if(retval != NULL)
		{
			printf("RETVAL IS NOT NULL\n");
			*retval = threads[index].retval;
		}
	}

	return 0;
}

void pthread_exit(void* retval)
{
	//mark the thread as EXITED
	threads[current_thread].status = EXITED;
	threads[current_thread].retval = retval;

	//free the allocated stack space
	free(threads[current_thread].stack);

	//if the waiting on me pointer isn't null then there's a thread blocked on this one
	if(threads[current_thread].waiting_on_me != NULL)
	{
		fprintf(stderr, "Changed blocking thread to READY\n");
		//so set that one equal to READY because this thread is exiting
		threads[current_thread].waiting_on_me->status = READY;
		threads[current_thread].waiting_on_me = NULL;
	}

	//change the thread_count
	thread_count--;

	printf("PTHREAD_EXIT FOR THREAD #%d\n", current_thread);

	schedule();

	__builtin_unreachable();
}

void pthread_exit_wrapper()
{
	unsigned int res;
	asm("movl %%eax, %0\n":"=r"(res));
	pthread_exit((void *) res);
}

pthread_t pthread_self()
{
	return threads[current_thread].id;
}

int pthread_create(pthread_t *thread,
		const pthread_attr_t *attr,
                void *(*start_routine) (void *), 
                void *arg)
{
	//create main thread upon first run
	if(first)
	{
		first = 0;
		//initialize timer for thread preemption
		timer();

		//main thread id assigned
		threads[next_available].id = 0;

		if(setjmp(threads[next_available].registers) != 0)
			return 0;

		threads[next_available].status = READY;
		threads[next_available].initialized = 1;
		threads[next_available].index = next_available;

		thread_count++;
		//printf("MAIN THREAD CREATED\n");
	}
	if(thread_count < MAX_THREADS)
	{
		//parse the threads array to find an empty spot indexed by next_available
		int i;
		for(i = 0; i < MAX_THREADS; i++)
			if(threads[i].status == 0 /*|| threads[i].status == EXITED*/)
			{
				next_available = i;
				break;
			}

		//set the new threads id = next thread_id, increment the thread id
		threads[next_available].id = thread_id;
		*thread = thread_id;
		thread_id++;

		//allocate stack space for the new thread
		threads[next_available].stack = malloc(STACK_SIZE);

		//using pointer p place the arg and pthread_exit onto the new threads stack
		void * p = threads[next_available].stack;
		p += STACK_SIZE - 4;
		*((unsigned long int *)p) = (unsigned long int) arg;
		p -= 4;
		*((unsigned long int *)p) = (unsigned long int) pthread_exit_wrapper;


		//set the program counter of the new thread to the start_routine
		threads[next_available].registers[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long int) start_routine);
		threads[next_available].registers[0].__jmpbuf[JB_SP] = ptr_mangle((unsigned long int) p);

		//init
		threads[next_available].waiting_on_me = NULL;
		threads[next_available].initialized = 1;
		threads[next_available].index = next_available;
		threads[next_available].status = READY;
		thread_count++;

		printf("Created thread #%d.\n", *thread);

		schedule();

		return 0;
	}else{
		return -1;
	}
}

static int ptr_demangle(int p)
{
	unsigned int ret;
	asm(
	" movl %1, %%eax;\n"
	" rorl $0x9, %%eax;"
	" xorl %%gs:0x18, %%eax;"
	" movl %%eax, %0;"
	: "=r"(ret)
	: "r"(p)
	: "%eax"
	);
	return ret;
}

static int ptr_mangle(int p)
{
	unsigned int ret;
	asm(" movl %1, %%eax;\n"
	    " xorl %%gs:0x18, %%eax;"
	    " roll $0x9, %%eax;"
	    " movl %%eax, %0;"
	: "=r"(ret)
	: "r"(p)
	: "%eax"
	);
	return ret;
}

/*

sem_t sem;
char str1[20];

void * change(void * ret)
{
	//test cases for the semaphore
	sem_wait(&sem);

	printf("Enter name: ");
	scanf("%s", str1);

	int waste;
	for(waste = 0; waste < 1000000; waste++)
	{
		//waste some time
	}

	printf("change - critical: %s\n", str1);
	sem_post(&sem);
}

main()
{
	pthread_t thread1, thread2;
	pthread_t array[2] = {thread1, thread2};

	sem_init(&sem, 0, 1);
	pthread_create(&array[0], NULL, change, NULL);
	pthread_create(&array[1], NULL, change, NULL);

	//fprintf(stderr, "critical: %d\n, sem.value: %d", critical, semaphores);

	int i;
	for(i = 0; i < 2; i++)
	{
		pthread_join(array[i], NULL);
		print_threads_stderr();
	}
}
*/