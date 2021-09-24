#include "test-object.h"

enum
{
  /* FILL ME */
  SIGNAL_EVENT,
  LAST_SIGNAL
};


static guint test_object_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (TestObject, test_object, G_TYPE_OBJECT);

static void
test_object_init (TestObject * self)
{
}

static void
test_object_class_init (TestObjectClass * klass)
{
  test_object_signals[SIGNAL_EVENT] =
      g_signal_new ("event", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (TestObjectClass, event), NULL, NULL,
      g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, GST_TYPE_EVENT);

}
