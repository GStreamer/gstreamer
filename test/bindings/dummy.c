#include <dummy.h>

Dummy *dummy_new() {
  Dummy *dummy;

  dummy = g_malloc(sizeof(Dummy));

  dummy->flags = 0;
  dummy->name = NULL;

  return dummy;
}

Dummy *dummy_new_with_name(gchar *name) {
  Dummy *dummy = dummy_new();

  dummy->name = g_strdup(name);

  return dummy;
}
