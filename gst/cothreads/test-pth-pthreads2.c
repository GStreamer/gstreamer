#include <stdio.h>
#include "linuxthreads.h"
#include "pth_p.h"

pth_mctx_t main_context;
int threadnum = 0;

void cothread ()
{
  printf ("1.%d: current stack frame: %p\n", threadnum, CURRENT_STACK_FRAME);
  printf ("1.%d: sleeping 2s...\n", threadnum);
  sleep (2);
  printf ("1.%d: returning to cothread 1.0\n");
  pth_mctx_restore (&main_context);
}

void *pthread (void *unused) 
{
  char *sp = CURRENT_STACK_FRAME;
  char *cothread_stack;
  pth_mctx_t ctx;
  _pthread_descr descr;
  
  descr = __linuxthreads_self();
  printf ("sp: %p\n", sp);
  printf ("STACK_SIZE: %p\n", STACK_SIZE);
  printf ("sp | STACK_SIZE: 0x%x\n", (int) sp | STACK_SIZE );
  printf ("(sp | (STACK_SIZE-1))+1 - 1K: 0x%x\n", ((((int)sp | (STACK_SIZE-1))+1) - 1024));
  printf ("*(sp | (STACK_SIZE-1))+1 - 1K: %p\n", *(int*)((((long int)sp | (STACK_SIZE-1))+1) - 1024));
  
  while (threadnum < 10) {
    if (posix_memalign (&cothread_stack, STACK_SIZE, STACK_SIZE)) {
      printf ("could not malloc a chunk of aligned memory\n");
      exit (-1);
    }
    
    printf ("1: setting *%p = %p\n", cothread_stack + STACK_SIZE - 1024, descr);
    memcpy(cothread_stack + STACK_SIZE - 1024, descr, 1024);
    
    pth_mctx_save (&main_context);
    printf ("1: spawning new thread, bottom = %p\n", cothread_stack);
    pth_mctx_set (&ctx, cothread, cothread_stack, cothread_stack + STACK_SIZE - 1024);
    printf ("1: switching to cothread %d...\n", ++threadnum);
    pth_mctx_switch (&main_context, &ctx);
  }
  
  printf ("1: back, returning...\n");
  
  return NULL;
}

int main (int argc, char *argv[]) 
{
  pthread_t tid;
  
  pthread_create (&tid, NULL, pthread, NULL);
  pthread_join (tid, NULL);
  
  __linuxthreads_self();
  
  exit (0);
}
