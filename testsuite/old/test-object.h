#include <glib-object.h>
#include <gst/gstevent.h>

/* TestObject */

typedef struct {
  GObject parent;
} TestObject;

typedef struct {
  GObjectClass parent_class;
  /* signals */
  void (*event) (TestObject *object, GstEvent *event);
} TestObjectClass;

#define TEST_TYPE_OBJECT            (test_object_get_type())
#define TEST_OBJECT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TEST_TYPE_OBJECT, TestObject))
#define TEST_OBJECT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TEST_TYPE_OBJECT, TestObjectClass))
#define TEST_IS_OBJECT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TEST_TYPE_OBJECT))
#define TEST_IS_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TEST_TYPE_OBJECT))
#define TEST_OBJECT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), TEST_TYPE_OBJECT, TestObjectClass))

GType test_object_get_type (void);
