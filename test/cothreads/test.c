#include <stdio.h>

#include "glib.h"
#include "cothreads.h"
#include "object.h"
#include "looper.h"

cothread_context *ctx;

int main(int argc,char *argv[]) {
  looper *l1,*l2;

  ctx = cothread_init();

  l1 = looper_create("looperone",1,ctx);
  l2 = looper_create("loopertwo",0,ctx);
  object_setpeer(OBJECT(l1),OBJECT(l2));

  fprintf(stderr,"about to start l1\n\n");
  while (1)
   object_start(l1);
}
