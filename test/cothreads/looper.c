#include <stdio.h>
#include "looper.h"

void looper_loopfunc(object *obj);

void looper_init(looper *l,int source) {
  l->source = source;
  object_setloopfunc(OBJECT(l),looper_loopfunc);
}

looper *looper_create(char *name,int source,cothread_context *ctx) {
  looper *l = malloc(sizeof(looper));

  if (l == NULL) {
    fprintf(stderr,"sorry, couldn't allocate memory for looper\n");
    exit(2);
  }
  object_init(OBJECT(l),name,ctx);
  looper_init(l,source);

  return l;
}


void looper_loopfunc(object *obj) {
  looper *l = LOOPER(obj);

  if (l->source) {
    while (1) {
      char *buf = malloc(11);
      sprintf(buf,"Hello World!");
      fprintf(stderr,"\npushing buffer %p with '%s'\n",buf,buf);
      object_push(OBJECT(l)->peer,buf);		// this should switch
    }
  } else {
    while (1) {
      char *buf;
      fprintf(stderr,"\npulling buffer\n");
      buf = object_pull(OBJECT(l));
      printf("got %p: '%s' from peer\n",buf,buf);
      free(buf);
      // return to the main process now
      cothread_switch(cothread_main(OBJECT(l)->threadstate->ctx));
      sleep(1000);
    }
  }
}
