#include <cothreads.h>

void co_thread (int argc, void **argv)
{
  int pthreadnum =  *(int*)argv[0];
  int cothreadnum = *(int*)argv[1];
  cothread *main = argv[2];
  cothread *self = argv[3];
  
  printf ("%d.%d: sleeping 1s...\n", pthreadnum, cothreadnum);
  sleep (1);
  printf ("%d.%d: returning to cothread 0\n", pthreadnum, cothreadnum);
  
  cothread_switch (self, main);
}

void pthread (void* _pthreadnum) 
{
  int pthreadnum = *(int*) _pthreadnum;
  int cothreadnum = 0;
  cothread *main, *new;
  char *argv[4];
  
  main = cothread_create (NULL, 0, NULL);
  
  while (cothreadnum++ < 25) {
    printf ("%d: spawning a new cothread\n", pthreadnum);
    
    argv[0] = &pthreadnum;
    argv[1] = &cothreadnum;
    argv[2] = main;
    argv[3] = cothread_create (co_thread, 4, argv);
    new = argv[3];
    
    printf ("%d: switching to cothread %d...\n", pthreadnum, cothreadnum);
    cothread_switch (main, new);
    
    printf ("%d: back now, looping\n", pthreadnum);
  }
}

#define NTHREADS 2

int main (int argc, char *argv[])
{
  GThread *thread[NTHREADS];
  int pthreadnum[4], i;
  
  g_thread_init(NULL);
  cothreads_init(NULL);
  
  printf ("0: creating the gthreads\n");
  
  for (i=0; i<NTHREADS; i++) {
    pthreadnum[i] = i+1;
    thread[i] = g_thread_create (pthread, &pthreadnum[i], TRUE, NULL);
  }
  
  printf ("0: joining the gthreads\n");
  for (i=0; i<NTHREADS; i++) {
    g_thread_join (thread[i]);
  }
  
  printf ("exiting\n");
  
  exit (0);
}

