#ifndef __DUMMY_H__
#define __DUMMY_H__

#include <glib.h>

#define DUMMY(dummy) ((Dummy *)(dummy))

typedef struct _Dummy Dummy;

struct _Dummy {
  gint flags;

  gchar *name;
};

Dummy *dummy_new();
Dummy *dummy_new_with_name(gchar *name);

#endif /* __DUMMY_H__ */
