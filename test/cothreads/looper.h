#ifndef __LOOPER_H__
#define __LOOPER_H__

#include "object.h"

#define LOOPER(l) ((looper *)(l))

typedef struct _looper looper;
struct _looper {
  object object;

  int source;
};

void looper_init(looper *l,int source);
looper *looper_create(char *name,int source,cothread_context *ctx);

#endif /* __LOOPER_H__ */
