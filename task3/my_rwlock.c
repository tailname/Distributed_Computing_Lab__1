#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#ifndef my_rwlock
#define my_rwlock

#ifdef MY_RWLOCK_ALIGNED
/* mutex + counters (40 + 16 = 56 bytes) share one cache line: they are touched
   on every lock/unlock. Condvars go to a separate line: they are touched only
   when someone has to sleep. The whole lock occupies its own cache lines. */
typedef struct {
	pthread_mutex_t mutex;

	int readers_count;
	int wating_readers_count;
	int wating_writers_count;

	int writer_active;

	pthread_cond_t read_cond __attribute__((aligned(64)));
	pthread_cond_t write_cond;
} __attribute__((aligned(64))) my_rwlock_t;
#else
typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t read_cond;
	pthread_cond_t write_cond;

	int readers_count;
	int wating_readers_count;
	int wating_writers_count;

	int writer_active;
} my_rwlock_t;
#endif

int my_rwlock_init(my_rwlock_t* lock) {
	lock->readers_count = 0;
	lock->wating_readers_count = 0;
	lock->wating_writers_count = 0;
	lock->writer_active = 0;
	pthread_mutex_init(&lock->mutex, NULL);
	pthread_cond_init(&lock->read_cond, NULL);
	pthread_cond_init(&lock->write_cond, NULL);
	return 0;
}

int my_rwlock_destroy(my_rwlock_t* lock) {
	pthread_cond_destroy(&lock->write_cond);
	pthread_cond_destroy(&lock->read_cond);
	pthread_mutex_destroy(&lock->mutex);
	return 0;
}

int my_rwlock_rdlock(my_rwlock_t* lock) {
    pthread_mutex_lock(&lock->mutex);
	++lock->wating_readers_count; 

	while (lock->writer_active)
		pthread_cond_wait(&lock->read_cond, &lock->mutex);

	--lock->wating_readers_count;
	++lock->readers_count;
	pthread_mutex_unlock(&lock->mutex);
	return 0;
}
int my_rwlock_wrlock(my_rwlock_t* lock) {
	
    pthread_mutex_lock(&lock->mutex);
	++lock->wating_writers_count; 

	while (lock->writer_active || lock->readers_count > 0)
		pthread_cond_wait(&lock->write_cond, &lock->mutex);

	--lock->wating_writers_count;
	lock->writer_active = 1;
	pthread_mutex_unlock(&lock->mutex);		

	return 0;
}

int my_rwlock_unlock(my_rwlock_t* lock) {
	pthread_mutex_lock(&lock->mutex);
	if (lock->readers_count > 0)	--lock->readers_count;
	else lock->writer_active = 0;
	if (lock->readers_count == 0) {
		if (lock->wating_writers_count > 0) pthread_cond_signal(&lock->write_cond);
		else if (lock->wating_readers_count > 0) pthread_cond_broadcast(&lock->read_cond);
	}
	pthread_mutex_unlock(&lock->mutex);
	return 0;
}

#endif
