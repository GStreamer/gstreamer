#include <unistd.h>
#include <gst/gst.h>

#define GST_TYPE_TEST               (gst_test_get_type ())
#define GST_TEST(test)              (G_TYPE_CHECK_INSTANCE_CAST ((test), GST_TYPE_TEST, GstTest))
#define GST_IS_TEST(test)           (G_TYPE_CHECK_INSTANCE_TYPE ((test), GST_TYPE_TEST))
#define GST_TEST_CLASS(tclass)      (G_TYPE_CHECK_CLASS_CAST ((tclass), GST_TYPE_TEST, GstTestClass))
#define GST_IS_TEST_CLASS(tclass)   (G_TYPE_CHECK_CLASS_TYPE ((tclass), GST_TYPE_TEST))
#define GST_TEST_GET_CLASS(test)    (G_TYPE_INSTANCE_GET_CLASS ((test), GST_TYPE_TEST, GstTestClass))

typedef struct _GstTest GstTest;
typedef struct _GstTestClass GstTestClass;

struct _GstTest
{
  GstObject object;
};

struct _GstTestClass
{
  GstObjectClass parent_class;

  void (*test_signal1) (GstTest * test, gint an_int);
  void (*test_signal2) (GstTest * test, gint an_int);
};

static GType gst_test_get_type (void);

/* Element signals and args */
enum
{
  TEST_SIGNAL1,
  TEST_SIGNAL2,
  /* add more above */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_TEST_PROP
};

static void gst_test_class_init (GstTestClass * klass);
static void gst_test_init (GstTest * test);
static void gst_test_dispose (GObject * object);

static void signal2_handler (GstTest * test, gint anint);

static void gst_test_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_test_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstObjectClass *parent_class = NULL;

static guint gst_test_signals[LAST_SIGNAL] = { 0 };

static GType
gst_test_get_type (void)
{
  static GType test_type = 0;

  if (!test_type) {
    static const GTypeInfo test_info = {
      sizeof (GstTestClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_test_class_init,
      NULL,
      NULL,
      sizeof (GstTest),
      0,
      (GInstanceInitFunc) gst_test_init,
      NULL
    };

    test_type = g_type_register_static (GST_TYPE_OBJECT, "GstTest",
        &test_info, 0);
  }
  return test_type;
}

static void
gst_test_class_init (GstTestClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstobject_class = GST_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  if (!g_thread_supported ())
    g_thread_init (NULL);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_test_dispose);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_test_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_test_get_property);

  gst_test_signals[TEST_SIGNAL1] =
      g_signal_new ("test-signal1", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstTestClass, test_signal1), NULL,
      NULL, gst_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
  gst_test_signals[TEST_SIGNAL2] =
      g_signal_new ("test-signal2", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstTestClass, test_signal2), NULL,
      NULL, gst_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  g_object_class_install_property (gobject_class, ARG_TEST_PROP,
      g_param_spec_int ("test-prop", "Test Prop", "Test property",
          0, 1, 0, G_PARAM_READWRITE));

  klass->test_signal2 = signal2_handler;
}

static void
gst_test_init (GstTest * test)
{
}

static void
gst_test_dispose (GObject * object)
{
  GstTest *test;

  test = GST_TEST (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_test_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTest *test;

  test = GST_TEST (object);

  switch (prop_id) {
    case ARG_TEST_PROP:
      g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_test_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTest *test;

  test = GST_TEST (object);

  switch (prop_id) {
    case ARG_TEST_PROP:
      g_value_set_int (value, 0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_test_do_signal1 (GstTest * test)
{
  g_signal_emit (G_OBJECT (test), gst_test_signals[TEST_SIGNAL1], 0, 0);
}

static void
signal2_handler (GstTest * test, gint anint)
{
}

static void
gst_test_do_signal2 (GstTest * test)
{
  g_signal_emit (G_OBJECT (test), gst_test_signals[TEST_SIGNAL2], 0, 0);
}

static void
gst_test_do_prop (GstTest * test)
{
  g_object_notify (G_OBJECT (test), "test-prop");
}

static gpointer
run_thread (GstTest * test)
{
  gint i = 0;

  while (TRUE) {
    if (TESTNUM == 1)
      gst_test_do_signal1 (test);
    if (TESTNUM == 2)
      gst_test_do_signal2 (test);
    if (TESTNUM == 3)
      gst_test_do_prop (test);
    if ((i++ % 10000) == 0) {
      g_print (".");
      g_usleep (1);             /* context switch */
    }
  }

  return NULL;
}

int
main (int argc, char **argv)
{
  gint i;
  GstTest *test1, *test2;

  gst_init (&argc, &argv);

  test1 = g_object_new (GST_TYPE_TEST, NULL);
  test2 = g_object_new (GST_TYPE_TEST, NULL);

  for (i = 0; i < 20; i++) {
    g_thread_create ((GThreadFunc) run_thread, test1, TRUE, NULL);
    g_thread_create ((GThreadFunc) run_thread, test2, TRUE, NULL);
  }
  sleep (5);

  return 0;
}
