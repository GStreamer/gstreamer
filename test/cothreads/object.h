#ifndef __OBJECT_H__
#define __OBJECT_H__

#include "cothreads.h"

#define OBJECT(obj) ((object*)(obj))

typedef struct _object object;

typedef void (*object_loopfunc)(object *obj);

struct _object {
  cothread_state *threadstate;
  object_loopfunc loopfunc;

  char *name;
  object *peer;

  void *pen;
};

void object_init(object *obj,char *name,cothread_context *ctx);
object *object_create(char *name,cothread_context *ctx);
void object_setloopfunc(object *obj,object_loopfunc func);
void object_setpeer(object *obj,object *peer);
void object_push(object *obj,char *buf);
char *object_pull(object *obj);
int object_loop_function(int argc,char **argv);

#endif /* __OBJECT_H__ */
