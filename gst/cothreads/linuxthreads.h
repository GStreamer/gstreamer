#ifndef _GNU_SOURCE /* pull in the nonposix static mutex initializers */
#define _GNU_SOURCE
#define __USE_GNU   /* just to be sure */
#endif

#include <pthread.h>

#ifndef CURRENT_STACK_FRAME
#define CURRENT_STACK_FRAME  ({ char __csf; &__csf; })
#endif /* CURRENT_STACK_FRAME */

#define STACK_SIZE 0x200000

/* this function is only really necessary to get the main thread's
 * pthread_descr, as the other threads store the pthread_descr (actually the
 * first member of struct _pthread_descr_struct, which points to itself for the
 * default non-indirected case) at the top of the stack. */
static inline _pthread_descr __linuxthreads_self() 
{
  pthread_mutex_t mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
  _pthread_descr self;
  
  pthread_mutex_lock (&mutex);
  self = mutex.__m_owner;
  pthread_mutex_unlock (&mutex);
  
  printf ("pthread_self: %d\n", pthread_self());
  printf ("descr: %p\n", self);
  printf ("*descr: %p\n", *(int*)self);
  
  return self;
}

