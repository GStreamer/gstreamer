#include <pthread.h>
#include <stdio.h>
#include "linuxthreads-internals.h"

#pragma weak __pthread_initial_thread
extern pthread_descr __pthread_initial_thread;

static inline thread_self_descr() 
{
  char * sp = CURRENT_STACK_FRAME;
  int self = (int) pthread_self();
  
  if (self % PTHREAD_THREADS_MAX < 2)
    /* we only support the main thread, not the manager. */
    return &__pthread_initial_thread;
  
#ifdef _STACK_GROWS_DOWN
  return (pthread_descr)(((unsigned long)sp | (STACK_SIZE-1))+1) - 1;
#else
  return (pthread_descr)((unsigned long)sp &~ (STACK_SIZE-1));
#endif
}

int main (int argc, char *argv[]) 
{
    printf ("pthread_self: %d\n", pthread_self());
    printf ("descr: %p\n", thread_self_descr());
    exit (0);
}
