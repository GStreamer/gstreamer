#include <cothreads.h>

cothread *main_context;
cothread *ctx;
int threadnum = 0;

void co_thread (void)
{
  printf ("1.%d: sleeping 1s in thread %d...\n", threadnum, threadnum);
  sleep (1);
  printf ("1.%d: returning to cothread 0\n", threadnum);
  cothread_switch (ctx, main_context);
}

void pthread (void* unused) 
{
  char *skaddr;
  
  printf ("1: saving the main context\n");
  main_context = cothread_init(NULL);
  
  while (threadnum < 25) {
    printf ("1: spawning a new cothread\n");
    ctx = cothread_create (co_thread);
    
    printf ("1: switching to cothread %d...\n", ++threadnum);
    cothread_switch (main_context, ctx);
  
    printf ("1: back now, looping\n");
  }
}


int main (int argc, char *argv[])
{
  GThread *thread;
  
  g_thread_init(NULL);
  
  printf ("0: creating the gthread\n");

  thread = g_thread_create (pthread, NULL, TRUE, NULL);

  printf ("joining the gthread\n");
  g_thread_join (thread);

  printf ("exiting\n");
  
  exit (0);
}

