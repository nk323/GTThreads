#include "gtthread.h"


#include <signal.h>
#include <stdlib.h>

static sigset_t alrm;


int gtthread_mutex_init(gtthread_mutex_t* mutex)
{
	/* init parameters */
	steque_init(&mutex -> queue);
	mutex -> lock = 0;
	mutex -> init = 1;
	/* Set up signal */
	sigemptyset(&alrm);
	sigaddset(&alrm, SIGVTALRM);

	return 0;
}

int gtthread_mutex_lock(gtthread_mutex_t* mutex)
{
	/* block signals before changing mutex */
	sigset_t oldset;
	gtthread_t currentThreadId;
	sigprocmask(SIG_BLOCK, &alrm, &oldset);
	
	if(mutex -> init)	
	{
		if( mutex -> lock != 0)
		{
			/* mutex not locked, go ahead and lock it*/		
			mutex -> lock = 1;
		}
		else
		{
		
			/* mutex already locked */
			
            /* get the thread at the front of the currenty_running queue*/
			currentThreadId = gtthread_self();
            /* enqueue it at the back */
			steque_enqueue(&mutex -> queue, &currentThreadId);
            /* yield the CPU to next runnable thread */
			gtthread_yield();
		}

		sigprocmask(SIG_UNBLOCK, &alrm, NULL);

		return 0;
	}
	else
	{
		/*printf("Error. Trying to unlock uninitialized mutex. \n");*/
		sigprocmask(SIG_UNBLOCK, &alrm, NULL);
		return 1;
	}
}

int gtthread_mutex_unlock(gtthread_mutex_t* mutex)
{
	/* block signals before changing mutex */
	sigset_t oldset;
	gtthread_t *nextThreadId;
	sigprocmask(SIG_BLOCK, &alrm, &oldset);
	
	if(mutex -> init) 
	{
		if( mutex -> lock == 0)
		{
			/* mutex locked, go ahead and unlock it*/		
			mutex -> lock = 0;
			/* then enqueue next thread waiting on the mutex to currently_running */
			if( !steque_isempty(&mutex -> queue))
			{
				nextThreadId = (gtthread_t *) steque_pop(&mutex -> queue);
				addToQueue(*nextThreadId);
			}			
		}
	
		sigprocmask(SIG_UNBLOCK, &alrm, NULL);
		return 0;
	}
	else
	{
		/*printf("Error. Trying to unlock uninitialized mutex. \n");*/
		sigprocmask(SIG_UNBLOCK, &alrm, NULL);
		return 1;
	}
}


int gtthread_mutex_destroy(gtthread_mutex_t* mutex)
{
	sigset_t oldset;
	sigprocmask(SIG_BLOCK, &alrm, &oldset);
	if(mutex -> init)	/* already initialized therefore valid. */
	{
		if(!mutex -> lock) /* mutex busy, return a busy code */
		{
			sigprocmask(SIG_UNBLOCK, &alrm, &oldset);			
			return 1;
		}
		else /* mutex valid and not busy */
		{
		/*nothing to be done other than making mutex ->init = 0 since no malloc */
			mutex -> init = 0; /* reset to zero so it can be initialized again*/
			steque_destroy(&(mutex -> queue));
			sigprocmask(SIG_UNBLOCK, &alrm, &oldset);			
			return 0;
		}
	}
	else /* mutex has not been initalized, return invalid code */
	{			
		/*printf("Trying to detroy inavlid mutex. \n"); */		
		sigprocmask(SIG_UNBLOCK, &alrm, &oldset);			
		return -1;
	}
}
