/* 
 * control-sources.c
 *
 * Generates a datafile for various control sources.
 *
 * Needs gnuplot for plotting.
 * plot "ctrl_interpolation.dat" using 1:2 with points title 'none', "" using 1:3 with points title 'linear', "" using 1:4 with points title 'cubic'
 * plot "ctrl_lfo.dat" using 1:2 with points title 'sine', "" using 1:3 with points title 'saw', "" using 1:4 with points title 'square', "" using 1:5 with points title 'triangle'
 */

#include <stdio.h>
#include <stdlib.h>

#include <gst/gst.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstlfocontrolsource.h>

/* local test element */

enum
{
  PROP_INT = 1,
  PROP_FLOAT,
  PROP_DOUBLE,
  PROP_BOOLEAN,
  PROP_COUNT
};

#define GST_TYPE_TEST_OBJ            (gst_test_obj_get_type ())
#define GST_TEST_OBJ(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TEST_OBJ, GstTestObj))
#define GST_TEST_OBJ_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TEST_OBJ, GstTestObjClass))
#define GST_IS_TEST_OBJ(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TEST_OBJ))
#define GST_IS_TEST_OBJ_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_TEST_OBJ))
#define GST_TEST_OBJ_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_TEST_OBJ, GstTestObjClass))

typedef struct _GstTestObj GstTestObj;
typedef struct _GstTestObjClass GstTestObjClass;

struct _GstTestObj
{
  GstElement parent;
  gint val_int;
  gfloat val_float;
  gdouble val_double;
  gboolean val_boolean;
};
struct _GstTestObjClass
{
  GstElementClass parent_class;
};

static GType gst_test_obj_get_type (void);

static void
gst_test_obj_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GstTestObj *self = GST_TEST_OBJ (object);

  switch (property_id) {
    case PROP_INT:
      g_value_set_int (value, self->val_int);
      break;
    case PROP_FLOAT:
      g_value_set_float (value, self->val_float);
      break;
    case PROP_DOUBLE:
      g_value_set_double (value, self->val_double);
      break;
    case PROP_BOOLEAN:
      g_value_set_boolean (value, self->val_boolean);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_test_obj_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GstTestObj *self = GST_TEST_OBJ (object);

  switch (property_id) {
    case PROP_INT:
      self->val_int = g_value_get_int (value);
      GST_DEBUG ("test value int=%d", self->val_int);
      break;
    case PROP_FLOAT:
      self->val_float = g_value_get_float (value);
      GST_DEBUG ("test value float=%f", self->val_float);
      break;
    case PROP_DOUBLE:
      self->val_double = g_value_get_double (value);
      GST_DEBUG ("test value double=%f", self->val_double);
      break;
    case PROP_BOOLEAN:
      self->val_boolean = g_value_get_boolean (value);
      GST_DEBUG ("test value boolean=%d", self->val_boolean);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_test_obj_class_init (GstTestObjClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_test_obj_set_property;
  gobject_class->get_property = gst_test_obj_get_property;

  g_object_class_install_property (gobject_class, PROP_INT,
      g_param_spec_int ("int",
          "int prop",
          "int number parameter for the TEST_OBJ",
          0, 100, 0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_FLOAT,
      g_param_spec_float ("float",
          "float prop",
          "float number parameter for the TEST_OBJ",
          0.0, 100.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_DOUBLE,
      g_param_spec_double ("double",
          "double prop",
          "double number parameter for the TEST_OBJ",
          0.0, 100.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_BOOLEAN,
      g_param_spec_boolean ("boolean",
          "boolean prop",
          "boolean parameter for the TEST_OBJ",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
}

static void
gst_test_obj_base_init (GstTestObjClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "test object for unit tests",
      "Test", "Use in unit tests", "Stefan Sauer <ensonic@users.sf.net>");
}

static GType
gst_test_obj_get_type (void)
{
  static volatile gsize TEST_OBJ_type = 0;

  if (g_once_init_enter (&TEST_OBJ_type)) {
    GType type;
    static const GTypeInfo info = {
      (guint16) sizeof (GstTestObjClass),
      (GBaseInitFunc) gst_test_obj_base_init,   // base_init
      NULL,                     // base_finalize
      (GClassInitFunc) gst_test_obj_class_init, // class_init
      NULL,                     // class_finalize
      NULL,                     // class_data
      (guint16) sizeof (GstTestObj),
      0,                        // n_preallocs
      NULL,                     // instance_init
      NULL                      // value_table
    };
    type = g_type_register_static (GST_TYPE_ELEMENT, "GstTestObj", &info, 0);
    g_once_init_leave (&TEST_OBJ_type, type);
  }
  return TEST_OBJ_type;
}

static void
test_interpolation (void)
{
  GstObject *e;
  GstInterpolationControlSource *ics;
  GstTimedValueControlSource *tvcs;
  GstControlSource *cs;
  gint t, i1, i2, i3;
  FILE *f;

  e = (GstObject *) gst_element_factory_make ("testobj", NULL);

  ics = gst_interpolation_control_source_new ();
  tvcs = (GstTimedValueControlSource *) ics;
  cs = (GstControlSource *) ics;

  gst_object_set_control_source (e, "int", cs);

  gst_timed_value_control_source_set (tvcs, 0 * GST_SECOND, 0.0);
  gst_timed_value_control_source_set (tvcs, 10 * GST_SECOND, 1.0);
  gst_timed_value_control_source_set (tvcs, 20 * GST_SECOND, 0.5);
  gst_timed_value_control_source_set (tvcs, 30 * GST_SECOND, 0.2);

  if (!(f = fopen ("ctrl_interpolation.dat", "w")))
    exit (-1);
  fprintf (f, "# Time None Linear Cubic\n");

  for (t = 0; t < 40; t++) {
    g_object_set (ics, "mode", GST_INTERPOLATION_MODE_NONE, NULL);
    gst_object_sync_values (e, t * GST_SECOND);
    i1 = GST_TEST_OBJ (e)->val_int;

    g_object_set (ics, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);
    gst_object_sync_values (e, t * GST_SECOND);
    i2 = GST_TEST_OBJ (e)->val_int;

    g_object_set (ics, "mode", GST_INTERPOLATION_MODE_CUBIC, NULL);
    gst_object_sync_values (e, t * GST_SECOND);
    i3 = GST_TEST_OBJ (e)->val_int;

    fprintf (f, "%d %d %d %d\n", t, i1, i2, i3);
  }

  fclose (f);

  gst_object_unref (ics);
  gst_object_unref (e);
}

static void
test_lfo (void)
{
  GstObject *e;
  GstLFOControlSource *lfocs;
  GstControlSource *cs;
  gint t, i1, i2, i3, i4;
  FILE *f;

  e = (GstObject *) gst_element_factory_make ("testobj", NULL);

  lfocs = gst_lfo_control_source_new ();
  cs = (GstControlSource *) lfocs;

  gst_object_set_control_source (e, "int", cs);

  g_object_set (lfocs,
      "frequency", (gdouble) 0.05,
      "timeshift", (GstClockTime) 0,
      "amplitude", (gdouble) 0.5, "offset", (gdouble) 0.5, NULL);

  if (!(f = fopen ("ctrl_lfo.dat", "w")))
    exit (-1);
  fprintf (f, "# Time Sine Saw Square Triangle\n");

  for (t = 0; t < 40; t++) {
    g_object_set (lfocs, "waveform", GST_LFO_WAVEFORM_SINE, NULL);
    gst_object_sync_values (e, t * GST_SECOND);
    i1 = GST_TEST_OBJ (e)->val_int;

    g_object_set (lfocs, "waveform", GST_LFO_WAVEFORM_SAW, NULL);
    gst_object_sync_values (e, t * GST_SECOND);
    i2 = GST_TEST_OBJ (e)->val_int;

    g_object_set (lfocs, "waveform", GST_LFO_WAVEFORM_SQUARE, NULL);
    gst_object_sync_values (e, t * GST_SECOND);
    i3 = GST_TEST_OBJ (e)->val_int;

    g_object_set (lfocs, "waveform", GST_LFO_WAVEFORM_TRIANGLE, NULL);
    gst_object_sync_values (e, t * GST_SECOND);
    i4 = GST_TEST_OBJ (e)->val_int;

    fprintf (f, "%d %d %d %d %d\n", t, i1, i2, i3, i4);
  }

  fclose (f);

  gst_object_unref (lfocs);
  gst_object_unref (e);
}


gint
main (gint argc, gchar ** argv)
{
  gst_init (&argc, &argv);

  gst_element_register (NULL, "testobj", GST_RANK_NONE, GST_TYPE_TEST_OBJ);

  test_interpolation ();
  test_lfo ();

  return 0;
}
