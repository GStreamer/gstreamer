#include <stdio.h>
#include "object.h"

int object_loop_function(int argc,char **argv) {
  object *obj = OBJECT(argv);
  printf("hey, in loopfunc for object %p\n",obj);
  obj->loopfunc(obj);
}

void object_init(object *obj,char *name,cothread_context *ctx) {
  obj->threadstate = cothread_create(ctx);
  cothread_setfunc(obj->threadstate,object_loop_function,0,(char **)obj);
  if (obj->threadstate == NULL) {
    fprintf(stderr,"sorry, couldn't init threadstate\n");
    exit(2);
  }
  obj->loopfunc = NULL;
  obj->name = malloc(strlen(name));
  memcpy(obj->name,name,strlen(name));
  obj->peer = NULL;
}

object *object_create(char *name,cothread_context *ctx) {
  object *obj = malloc(sizeof(object));

  if (obj == NULL) {
    printf("ack!\n");
    exit(2);
  }
  memset(obj,0,sizeof(object));
  object_init(obj,name,ctx);

  return obj;
}

void object_setloopfunc(object *obj,object_loopfunc func) {
  obj->loopfunc = func;
  fprintf(stderr,"setting object loopfunc to %p\n",func);
}

void object_setpeer(object *obj,object *peer) {
  obj->peer = peer;
  peer->peer = obj;
  printf("peered %p and %p\n",obj,peer);
}

void object_push(object *obj,char *buf) {
  obj->pen = buf;
  cothread_switch(obj->threadstate);
}

char *object_pull(object *obj) {
  char *buf,i=0;

  if (obj == NULL) fprintf(stderr,"obj is null\n");
  if (obj->peer == NULL) fprintf(stderr,"obj->peer is null\n");
  if (obj->peer->threadstate == NULL) fprintf(stderr,"obj->peer->threadstate is null\n");

  while (obj->pen == NULL)
    cothread_switch(obj->peer->threadstate),i++;
  buf = obj->pen;
  obj->pen = NULL;

  fprintf(stderr,"took %d switches to get %p from pen\n",i,buf);

  return buf;
}

void object_start(object *obj) {
  if (!obj->threadstate || !obj->loopfunc) {
    fprintf(stderr,"ack, not complete\n");
    fprintf(stderr,"obj->threadstate is %p, obj->loopfunc is %p\n",
            obj->threadstate,obj->loopfunc);
    exit(2);
  }
  cothread_switch(obj->threadstate);
  fprintf(stderr,"returned from cothread stuff into end of object_start()\n");
}
