#include <stdio.h>

#include "cothreads.h"
#include "object.h"
#include "looper.h"

int main(int argc,char *argv[]) {
  cothread_context *ctx = cothread_init();
  looper *l1,*l2;

  l1 = looper_create("looperone",1,ctx);
  l2 = looper_create("loopertwo",0,ctx);
  object_setpeer(OBJECT(l1),OBJECT(l2));

  fprintf(stderr,"about to start l1\n\n");
  object_start(l1);
}
