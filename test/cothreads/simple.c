#include <stdio.h>
#include "cothreads.h"

// cothread_context is passed in argv
int loopfunc(int argc,char **argv) {
  fprintf(stderr,"SIMPLE: in loopfunc\n");
  cothread_switch((cothread_context *)cothread_main(argv));
}

int main(int argc,char *argv[]) {
  cothread_context *ctx;
  cothread_state *state;

  ctx = cothread_init();
  state = cothread_create(ctx);
  cothread_setfunc(state,loopfunc,0,(char **)ctx);

  fprintf(stderr,"SIMPLE: about to switch to cothread 1\n");
  cothread_switch(state);
  fprintf(stderr,"SIMPLE: back from cothread_switch\n");

  return 0;
}
