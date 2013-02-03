#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/sysinfo.h>

#include <gmp.h>

#define SUM_THREADS_PER_CALC 9

typedef struct _CALC_THREAD_CONTEXT
{
	unsigned long long st;
	unsigned long long end;
	pthread_t id;
	int threadIndex;
} CALC_THREAD_CONTEXT, *PCALC_THREAD_CONTEXT;

typedef struct _SUM_THREAD_CONTEXT
{
	pthread_t id;

	//In
	mpz_t st;
	mpz_t end;
	mpz_t num;
	unsigned long long *termCount;
	pthread_mutex_t *termMutex;
	pthread_cond_t *termVar;

	//Temps
	mpz_t otherfactor;
	mpz_t factor;

	//Out
	mpz_t *sum;
} SUM_THREAD_CONTEXT, *PSUM_THREAD_CONTEXT;

// This variable has bits set that represent the terminated status of calc threads
unsigned long long TerminationBits;
pthread_mutex_t TerminationMutex;
pthread_cond_t TerminationVariable;

void *SumThread(void *context)
{
	PSUM_THREAD_CONTEXT tcontext = (PSUM_THREAD_CONTEXT)context;

	while (mpz_cmp(tcontext->factor, tcontext->end) < 0)
	{
		//Check if the number is divisible by the factor
		if (mpz_divisible_p(tcontext->num, tcontext->factor) != 0)
		{
			pthread_mutex_lock(tcontext->termMutex);

			//Add the factor
			mpz_add(*tcontext->sum, *tcontext->sum, tcontext->factor);

			//Add the other factor in the pair
			mpz_divexact(tcontext->otherfactor, tcontext->num, tcontext->factor);
			mpz_add(*tcontext->sum, *tcontext->sum, tcontext->otherfactor);

			//Bail early if we've exceeded our number
			if (mpz_cmp(*tcontext->sum, tcontext->num) > 0)
			{
				pthread_mutex_unlock(tcontext->termMutex);
				break;
			}

			pthread_mutex_unlock(tcontext->termMutex);
		}

		//This is a valid cancellation point
		pthread_testcancel();

		mpz_add_ui(tcontext->factor, tcontext->factor, 1);
	}

	pthread_mutex_lock(tcontext->termMutex);
	(*tcontext->termCount)++;
	pthread_cond_signal(tcontext->termVar);
	pthread_mutex_unlock(tcontext->termMutex);

	pthread_exit(NULL);
}

int executeSumCalculation(mpz_t num)
{
	mpz_t sum, squareRoot, sqrtRem, st;
	mpz_t range, rem;
	int i;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	unsigned long long termCount;
	int err;
	SUM_THREAD_CONTEXT contexts[SUM_THREADS_PER_CALC];

	mpz_init(squareRoot);
	mpz_init(sqrtRem);
	mpz_init(range);
	mpz_init(rem);

	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);
	termCount = 0;

	//The sum starts at 1 because 1 is a factor of any number
	mpz_init_set_ui(sum, 1);

	//We know that exactly 1 factor of every pair of factors will
	//appear before the square root.
	mpz_sqrtrem(squareRoot, sqrtRem, num);

	//Check if the the number is a square
	if (mpz_cmp_ui(sqrtRem, 0) != 0)
	{
		//Emulate ceil
		mpz_add_ui(squareRoot, squareRoot, 1);
	}

	//Factor starts at 2
	mpz_init_set_ui(st, 2);

	//Compute range
	mpz_mod_ui(rem, squareRoot, SUM_THREADS_PER_CALC);
	mpz_sub(squareRoot, squareRoot, rem);
	mpz_divexact_ui(range, squareRoot, SUM_THREADS_PER_CALC);

	for (i = 0; i < SUM_THREADS_PER_CALC; i++)
	{
		mpz_init_set(contexts[i].st, st);
		mpz_init_set(contexts[i].factor, st);
		mpz_init(contexts[i].otherfactor);
		mpz_add(st, st, range);
		if (i == 0)
		{
			mpz_sub_ui(st, st, 2);
		}
		mpz_init_set(contexts[i].end, st);
		if (i == 0)
		{
			mpz_add(contexts[i].end, contexts[i].end, rem);
			mpz_add(st, st, rem);
		}

		mpz_init_set(contexts[i].num, num);
		contexts[i].sum = &sum;
		contexts[i].termCount = &termCount;
		contexts[i].termMutex = &mutex;
		contexts[i].termVar = &cond;

		printf("Assigning work to sum thread %d\n", i);

		err = pthread_create(&contexts[i].id, NULL, SumThread, &contexts[i]);
		if (err != 0)
			return -1;
	}

	fflush(stdout);

	for (;;)
	{
		pthread_mutex_lock(&mutex);

		printf("%llu sum threads complete\n", termCount);
		fflush(stdout);

		if (termCount == SUM_THREADS_PER_CALC)
		{
			pthread_mutex_unlock(&mutex);
			break;
		}

		if (mpz_cmp(sum, num) > 0)
		{
			printf("sum is too large; terminating children\n");
			fflush(stdout);
			pthread_mutex_unlock(&mutex);
			break;
		}

		pthread_cond_wait(&cond, &mutex);

		pthread_mutex_unlock(&mutex);
	}

	for (i = 0; i < SUM_THREADS_PER_CALC; i++)
	{
		void *ret;

		pthread_cancel(contexts[i].id);

		pthread_join(contexts[i].id, &ret);

		mpz_clear(contexts[i].st);
		mpz_clear(contexts[i].num);
		mpz_clear(contexts[i].factor);
		mpz_clear(contexts[i].end);
		mpz_clear(contexts[i].otherfactor);
	}

	mpz_clear(squareRoot);
	mpz_clear(sqrtRem);
	mpz_clear(range);
	mpz_clear(rem);
	mpz_clear(st);

	if (mpz_cmp(sum, num) == 0)
	{
		mpz_clear(sum);
		return 1;
	}
	else
	{
		mpz_clear(sum);
		return 0;
	}
}


void *CalculationThread(void *context)
{
	PCALC_THREAD_CONTEXT tcontext = (PCALC_THREAD_CONTEXT)context;
	mpz_t num, nextprime;
	unsigned long p, i;

	mpz_init_set_ui(num, tcontext->st);
	mpz_init(nextprime);

	mpz_nextprime(nextprime, num);

	while ((p = mpz_get_ui(nextprime)) < tcontext->end)
	{
		mpz_set_ui(num, 0);

		printf("P=%lu\n", p);
		fflush(stdout);

		for (i = 0; i < p; i++)
		{
			mpz_setbit(num, p + i - 1);
		}
		
		if (executeSumCalculation(num) != 0)
		{
			printf("Thread %d - Found one: ", tcontext->threadIndex);
			mpz_out_str(stdout, 10, num);
			printf(" (P: %lu)\n", p);
			fflush(stdout);
		}

		// Next p value
		mpz_nextprime(nextprime, nextprime);
	}

	mpz_clear(num);

	// Set the termination bit to notify the arbiter that this thread needs to be respawned
	// with more work.
	pthread_mutex_lock(&TerminationMutex);
	TerminationBits |= (1 << tcontext->threadIndex);
	pthread_cond_signal(&TerminationVariable);
	pthread_mutex_unlock(&TerminationMutex);

	pthread_exit(context);
}

int main(int argc, char *argv[])
{
	PCALC_THREAD_CONTEXT threads;
	int threadCount, i, err;
	unsigned long long st;

	threadCount = 1;

	threads = (PCALC_THREAD_CONTEXT) malloc(sizeof(*threads) * threadCount);
	if (threads == NULL)
		return -1;


	pthread_mutex_init(&TerminationMutex, NULL);
	pthread_cond_init(&TerminationVariable, NULL);

	st = 2;

	// Setup some initial state of the thread contexts
	for (i = 0; i < threadCount; i++)
	{
		// Do one-time setup of thread context index
		threads[i].threadIndex = i;

		// Set termination bit in order for the arbiter to respawn the thread
		TerminationBits |= (1 << i);
	}

	// This is the arbitration loop that handles work assignments as threads complete
	// their assigned work blocks.
	pthread_mutex_lock(&TerminationMutex);
	for (;;)
	{
		for (i = 0; i < threadCount; i++)
		{
			// If the thread has indicated that it's terminated, respawn it.
			if ((1 << i) & TerminationBits)
			{
				// Setup the context again
				threads[i].st = st;
				st += 10;
				threads[i].end = st;

				// Clear the termination bit
				TerminationBits &= ~(1 << i);

				printf("Assigning work to calc thread %d from %llu to %llu\n", i, threads[i].st, threads[i].end);
				err = pthread_create(&threads[i].id, NULL, CalculationThread, &threads[i]);
				if (err != 0)
					return -1;
			}
		}

		// Flush output stream
		fflush(stdout);

		// Wait on the termination bits to change. This releases the
		// mutex while the wait is in progress to avoid a deadlock.
		// The function returns with the mutex locked. These mutex operations
		// are atomic. This wait is done after the loop in order to handle the
		// initial setup of the threads.
		pthread_cond_wait(&TerminationVariable, &TerminationMutex);
	}
	pthread_mutex_unlock(&TerminationMutex);
}
