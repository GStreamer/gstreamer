/* LinuxThreads internal data structures */
/* hacked for use with gstreamer by andy wingo <wingo@pobox.com> */

#include <bits/local_lim.h> /* PTHREAD_THREADS_MAX */
#include <sys/types.h>      /* _pthread_fastlock */

typedef void * pthread_descr;

/* Global array of thread handles, used for validating a thread id
   and retrieving the corresponding thread descriptor. Also used for
   mapping the available stack segments. */

#pragma weak __pthread_handles
extern struct pthread_handle_struct __pthread_handles[PTHREAD_THREADS_MAX];

struct pthread_handle_struct {
  struct _pthread_fastlock h_lock; /* Fast lock for sychronized access */
  pthread_descr h_descr;        /* Thread descriptor or NULL if invalid */
  char * h_bottom;              /* Lowest address in the stack thread */
};

typedef struct pthread_handle_struct * pthread_handle;

/* Return the handle corresponding to a thread id */

static inline pthread_handle thread_handle(pthread_t id)
{
  return &__pthread_handles[id % PTHREAD_THREADS_MAX];
}

