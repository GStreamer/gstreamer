#define __USE_GNU /* non-posix functions */
#include <pthread.h>
#undef __USE_GNU
#include <stdio.h>
#include "linuxthreads-internals.h"

// the thread_self algorithm:
/*
  char * sp = CURRENT_STACK_FRAME;
  int self = (int) pthread_self();
  
  if (self % PTHREAD_THREADS_MAX < 2)
  * we only support the main thread, not the manager. *
    return &__pthread_initial_thread;
  
#ifdef _STACK_GROWS_DOWN
  return (pthread_descr)(((unsigned long)sp | (STACK_SIZE-1))+1) - 1;
#else
  return (pthread_descr)((unsigned long)sp &~ (STACK_SIZE-1));
#endif
*/

/* this function is only really necessary to get the main thread's
 * pthread_descr, as the other threads store the pthread_descr (actually the
 * first member of struct _pthread_descr_struct, which points to itself for the
 * default (non-indirected) case) at the top of the stack. */
static _pthread_descr linuxthreads_self() 
{
  pthread_mutexattr_t mutexattr;
  pthread_mutex_t mutex;
  _pthread_descr self;
  
  pthread_mutexattr_init (&mutexattr);
  pthread_mutexattr_setkind_np (&mutexattr, PTHREAD_MUTEX_ERRORCHECK_NP);
  pthread_mutex_init (&mutex, &mutexattr);

  pthread_mutex_lock (&mutex);
  self = mutex.__m_owner;
  pthread_mutex_unlock (&mutex);
  
  printf ("pthread_self: %d\n", pthread_self());
  printf ("descr: %p\n", self);
  printf ("*descr: %p\n", *(int*)self);
  
  return self;
}

void *pthread (void *unused) 
{
  char *sp = CURRENT_STACK_FRAME;
  
  linuxthreads_self();
  printf ("sp: %p\n", sp);
  printf ("sp | 0x020000: 0x%x\n", (int) sp | 0x020000 );
  printf ("(sp | (0x020000-1))+1 - 1K: 0x%x\n", ((((int)sp | (STACK_SIZE-1))+1) - 1024));
  printf ("*(sp | (0x020000-1))+1 - 1K: %p\n", *(int*)((((long int)sp | (STACK_SIZE-1))+1) - 1024));
  printf ("(sp &~ (0x020000-1))+1: 0x%x\n", (((int)sp &~ (STACK_SIZE-1))+1));
  return NULL;
}

int main (int argc, char *argv[]) 
{
  pthread_t tid;
  int i;
  
  for (i=0; i<10; i++) {
    pthread_create (&tid, NULL, pthread, NULL);
    sleep(2);
  }
  
  linuxthreads_self();
  exit (0);
}
