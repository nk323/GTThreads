#include "gtthread.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>

static steque_t globalQ; /* to keep track of all threads */
static steque_t currently_running; /* for scheduling */

typedef struct 
{
  gtthread_t id; /* thread id */
  void* retval; /* return value */
  ucontext_t uctx; /* context for thread */
  /* flags */
  char finished;
  char cancel; 
  steque_t joining;/* queue for threads that will join current thread */
  
} gtthread;
    
static void scheduleNextAndSwap(gtthread* current);
void alrm_handler(int sig);
static gtthread* getThread(gtthread_t threadId);
void run_thread(void *(*start_routine)(void *), void *arg);

struct itimerval *T; 
struct sigaction preempt;
static sigset_t alrm;

int init = 0;
static int thread_num = 0;

gtthread *mthread; /* main thread */

/* will swap out currently running thread for next thread in the queue*/
void alrm_handler(int sig)
{

	gtthread *currentThread;
	/* block signals */
	sigset_t oldset;
	
	sigprocmask(SIG_BLOCK, &alrm, &oldset);
	currentThread = (gtthread *) gtthread_self();
    /* currently running thread has reached the end of its time slice */
	/* push this thread to the back of the queue */
	steque_cycle(&currently_running); 
	scheduleNextAndSwap(currentThread);
	
	sigprocmask(SIG_UNBLOCK, &alrm, NULL);
}


void gtthread_init(long period)
{
	if((mthread = malloc(sizeof(gtthread))) != NULL) /* if malloc successful */
	{
		/* init thread */
		mthread -> id = thread_num++;
		mthread -> finished = 0;
		mthread -> cancel = 0;

		/* getcontext() */
		if(getcontext(&mthread -> uctx) == 0)
		{
			mthread -> uctx.uc_stack.ss_sp = (char*) malloc(SIGSTKSZ);
			mthread -> uctx.uc_stack.ss_size = SIGSTKSZ;
			mthread -> uctx.uc_link = NULL;
		}

		/* init queues */
		steque_init(&globalQ);
		steque_init(&currently_running);
		steque_init(&mthread -> joining);
		
		/* add main thread to global queue and currently running queue*/
		steque_enqueue(&globalQ, mthread);
		steque_enqueue(&currently_running, mthread);

		/* now setting timer and alarm */
		/* preemption only allowed if period valid & != 0) */
		if(period != 0 && period > 0) 
		{
			sigemptyset(&alrm);
			sigaddset(&alrm, SIGVTALRM); 
			sigprocmask(SIG_UNBLOCK, &alrm, NULL);

			T = (struct itimerval*) malloc(sizeof(struct itimerval));
			T -> it_value.tv_sec = T -> it_interval.tv_sec = 0;
			T -> it_value.tv_usec = T -> it_interval.tv_usec = period;

			/* alarm handler */
			memset(&preempt, '\0', sizeof(preempt));
			preempt.sa_handler = &alrm_handler;
			if (sigaction(SIGVTALRM, &preempt, NULL) < 0) 
			{
				perror ("sigaction");
				exit(1);
  			}

			setitimer(ITIMER_VIRTUAL, T, NULL);
		}
		else /*if period != 0, there is nothing else to be done!*/
		{
			/*printf("Period initialized to 0. Preemption turned off. \n");*/
		}
		
	} 
	else
	{
		/*printf("gtthread_init malloc failed. Exiting.\n") ;*/
		exit(-1);

	}

	init = 1;
}



int  gtthread_create(gtthread_t *thread, void *(*start_routine)(void *), void *arg)
{
  if (init != 1)
  {
 	/*printf("Error - gtthread not initialized. \n");*/
	exit(-1);
  }
  else
  {
	gtthread *newThread, *current;
	sigset_t oldset;

	/* Block alarms */
	sigprocmask(SIG_BLOCK, &alrm, &oldset);
	/* sigprocmask(SIG_BLOCK, &alrm, NULL); */

	/* do malloc for new thread and init its attributes */
	if((newThread = malloc(sizeof(gtthread))) != NULL)
	{
		newThread -> id = thread_num++;
		newThread -> finished = 0;
		newThread -> cancel = 0;
		/* store thread id */
		*thread = newThread -> id;
		
		/* get thread at front of currently running queue*/
		current = (gtthread *) gtthread_self();

		/* getcontext() */
		if ( getcontext(&newThread -> uctx) == 0)
		{
			newThread -> uctx.uc_stack.ss_sp = (char*) malloc(SIGSTKSZ);
			newThread -> uctx.uc_stack.ss_size = SIGSTKSZ;	
			/* set its successor context */
			newThread -> uctx.uc_link = &current -> uctx;
		}
	
		/* init join queue for the new thread */
		steque_init(&newThread -> joining);
		
		makecontext(&newThread -> uctx, (void (*)(void)) run_thread, 2, start_routine, arg);


		/* Add new thread to back of queue */
		steque_enqueue(&globalQ, newThread);
		steque_enqueue(&currently_running, newThread);
	
		sigprocmask(SIG_UNBLOCK, &alrm, NULL);
  	}	
  	else
  	{
		/*printf("Thread creation malloc failed. Exiting.\n");*/
		sigprocmask(SIG_UNBLOCK, &alrm, NULL);
		return 1;/* ? */
  	}

  	return 0;
  }
}


int gtthread_join(gtthread_t thread, void **status)
{
  gtthread *target_thread, *callingThread;
  sigset_t oldset;

  /* Block alarms */
  sigprocmask(SIG_BLOCK, &alrm, &oldset);

  /* find target thread in globalQ */
  target_thread = getThread(thread);

  if(target_thread != NULL) /*target thread found */
  {
  	/* Check if it has finished */
	if(target_thread -> finished) 
	{
		/*If it has finished, unblock alarm and then set status and return*/
		sigprocmask(SIG_UNBLOCK,&alrm, NULL);
	}
	else
	{
		/* If not finished, get the currently running thread(calling thread)
		   and queue it to join target */
		callingThread = (gtthread *) steque_pop(&currently_running);
		steque_enqueue(&target_thread -> joining, callingThread);
		/* schedule next thread */
		scheduleNextAndSwap(callingThread);
		/* Now unblock alarms */
		sigprocmask(SIG_UNBLOCK, &alrm, NULL);
	}


	/* Set status */
	if(status != NULL)
	{
		*status = target_thread -> retval;
	}

	/* successful so return 0 */
	return 0;
  
  }
  else /* if target thread not found  */
  {
	/* unclock alarms and return */
	sigprocmask(SIG_UNBLOCK, &alrm, NULL);
	return 1;
  }
}

/* to find thread with threadId in the globalQ */
gtthread* getThread(gtthread_t threadId)
{
  int size = steque_size(&globalQ);
  int i = 0;
  gtthread *temp, *returnThread;

  returnThread = NULL;
 /* Get front of queue in temp, compare id
    Do this till you find a match, save it in returnThread and continue loop to restore
    original order  */
  while(i < size)
  {
	temp = (gtthread *) steque_front(&globalQ);
	if(temp -> id == threadId)
	{
		returnThread = temp;
	}
	steque_cycle(&globalQ);
	i++;
  }

  return returnThread;
}


void gtthread_exit(void *retval)
{
	gtthread *exiting, *runnable;
	sigset_t oldset;

	sigprocmask(SIG_BLOCK, &alrm, &oldset);
	exiting = (gtthread *) steque_pop(&currently_running);
	exiting -> retval = retval;
	exiting -> finished = 1;
	/*reschedJoinedThreads(exiting);*//* reschedule other threads in exiting thread's join Q */
		
	while( !steque_isempty(&exiting -> joining))
	{
		runnable = (gtthread *) steque_pop(&exiting -> joining);
		steque_enqueue(&currently_running, runnable);
	}
	scheduleNextAndSwap(exiting);

	sigprocmask(SIG_UNBLOCK, &alrm, NULL);
	
}

int gtthread_yield(void)
{
	gtthread *lastThread;
	sigset_t oldset;
	
	sigprocmask(SIG_BLOCK, &alrm, &oldset);
	lastThread = (gtthread *) steque_pop(&currently_running);
	steque_enqueue(&currently_running, lastThread);
	/* steque_cycle(&currently_running); */

	scheduleNextAndSwap(lastThread); /* swapcontext  */
	sigprocmask(SIG_UNBLOCK, &alrm, NULL);
	return 0;
}

int gtthread_equal(gtthread_t t1, gtthread_t t2)
{
	if (t1 == t2) return 1;
	else return 0;
}

int gtthread_cancel(gtthread_t thread)
{
	gtthread *targetTh;
	sigset_t oldset;
	
	sigprocmask(SIG_BLOCK, &alrm, &oldset);
	targetTh = getThread(thread);
	
	sigprocmask(SIG_UNBLOCK, &alrm, NULL);
	
	if(targetTh == NULL)
	{
		return 1;/* failed to cancel */
	}
	else 
	{
		targetTh -> cancel = 1;	/* if thread found, set cancel = 1 */
		return 0;
	}
}

gtthread_t gtthread_self(void)
{
	gtthread *front;
	sigset_t oldset;

	sigprocmask(SIG_UNBLOCK, &alrm, &oldset);
	front = (gtthread *) steque_front(&currently_running);
    sigprocmask(SIG_UNBLOCK, &alrm, NULL);

    return front -> id;
}


/* schedules next thread in the currently_running queue*/
void scheduleNextAndSwap(gtthread* current)
{
	gtthread *nextThread, *runnable;
	
	nextThread = (gtthread *) steque_front(&currently_running);
    		
	if(nextThread -> cancel)
	{
		nextThread -> finished = 1;
		steque_pop(&currently_running);
			
		while( !steque_isempty(&nextThread -> joining))
		{
			runnable = (gtthread *) steque_pop(&nextThread -> joining);
			steque_enqueue(&currently_running, runnable);
		}
		nextThread = (gtthread *) steque_front(&currently_running);
	}
	
	/* swapcontext(old, new) */

	if(swapcontext(&current -> uctx, &nextThread -> uctx) == -1)
	{		
		/*printf("swapcontext");*/
		exit(EXIT_FAILURE);
	}
}


void run_thread(void *(*start_routine)(void *), void *arg)
{
	void *rv; 
	sigprocmask(SIG_UNBLOCK, &alrm, NULL);

	rv = start_routine(arg);
	gtthread_exit(rv);
}

/* function that helps gtthread_mutex access data in gthread_sched */
/* add the thread with nextId to the currently_running queue */
void addToQueue(gtthread_t nextId)
{
	gtthread *nextThread;
	nextThread = (gtthread *) getThread(nextId);
	steque_enqueue(&currently_running, nextThread);
}

