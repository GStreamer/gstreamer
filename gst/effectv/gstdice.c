/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * dice.c: a 'dicing' effect
 *  copyright (c) 2001 Sam Mertens.  This code is subject to the provisions of
 *  the GNU Library Public License.
 *
 * I suppose this looks similar to PuzzleTV, but it's not. The screen is
 * divided into small squares, each of which is rotated either 0, 90, 180 or
 * 270 degrees.  The amount of rotation for each square is chosen at random.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/gstvideofilter.h>

#include <string.h>
#include <gst/gst.h>

#include <gst/video/video.h>

#define GST_TYPE_DICETV \
  (gst_dicetv_get_type())
#define GST_DICETV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DICETV,GstDiceTV))
#define GST_DICETV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DICETV,GstDiceTVClass))
#define GST_IS_DICETV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DICETV))
#define GST_IS_DICETV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DICETV))

typedef struct _GstDiceTV GstDiceTV;
typedef struct _GstDiceTVClass GstDiceTVClass;

#define DEFAULT_CUBE_BITS   4
#define MAX_CUBE_BITS       5
#define MIN_CUBE_BITS       0

typedef enum _dice_dir
{
  DICE_UP = 0,
  DICE_RIGHT = 1,
  DICE_DOWN = 2,
  DICE_LEFT = 3
}
DiceDir;

struct _GstDiceTV
{
  GstVideoFilter videofilter;

  gint width, height;
  gchar *dicemap;

  gint g_cube_bits;
  gint g_cube_size;
  gint g_map_height;
  gint g_map_width;
};

struct _GstDiceTVClass
{
  GstVideoFilterClass parent_class;
};

GType gst_dicetv_get_type (void);

static void gst_dicetv_create_map (GstDiceTV * filter);

static const GstElementDetails gst_dicetv_details =
GST_ELEMENT_DETAILS ("DiceTV effect",
    "Filter/Effect/Video",
    "'Dices' the screen up into many small squares",
    "Wim Taymans <wim.taymans@chello.be>");

static GstStaticPadTemplate gst_dicetv_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xBGR)
    );

static GstStaticPadTemplate gst_dicetv_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xBGR)
    );

static GstVideoFilterClass *parent_class = NULL;

enum
{
  ARG_0,
  ARG_CUBE_BITS
};

static gboolean
gst_dicetv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstDiceTV *filter = GST_DICETV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    g_free (filter->dicemap);
    filter->dicemap =
        (gchar *) g_malloc (filter->height * filter->width * sizeof (char));
    gst_dicetv_create_map (filter);
    ret = TRUE;
  }

  return ret;
}

static gboolean
gst_dicetv_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstDiceTV *filter;
  GstStructure *structure;
  gboolean ret = FALSE;
  gint width, height;

  filter = GST_DICETV (btrans);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    *size = width * height * 32 / 8;
    ret = TRUE;
    GST_DEBUG_OBJECT (filter, "our frame size is %d bytes (%dx%d)", *size,
        width, height);
  }

  return ret;
}

static unsigned int
fastrand (void)
{
  static unsigned int fastrand_val;

  return (fastrand_val = fastrand_val * 1103515245 + 12345);
}

static GstFlowReturn
gst_dicetv_transform (GstBaseTransform * trans, GstBuffer * in, GstBuffer * out)
{
  GstDiceTV *filter;
  guint32 *src, *dest;
  gint i, map_x, map_y, map_i, base, dx, dy, di;
  gint video_width, g_cube_bits, g_cube_size;
  GstFlowReturn ret = GST_FLOW_OK;

  filter = GST_DICETV (trans);
  src = (guint32 *) GST_BUFFER_DATA (in);
  dest = (guint32 *) GST_BUFFER_DATA (out);

  gst_buffer_copy_metadata (out, in, GST_BUFFER_COPY_TIMESTAMPS);

  video_width = filter->width;
  g_cube_bits = filter->g_cube_bits;
  g_cube_size = filter->g_cube_size;

  map_i = 0;
  for (map_y = 0; map_y < filter->g_map_height; map_y++) {
    for (map_x = 0; map_x < filter->g_map_width; map_x++) {
      base = (map_y << g_cube_bits) * video_width + (map_x << g_cube_bits);

      switch (filter->dicemap[map_i]) {
        case DICE_UP:
          for (dy = 0; dy < g_cube_size; dy++) {
            i = base + dy * video_width;
            for (dx = 0; dx < g_cube_size; dx++) {
              dest[i] = src[i];
              i++;
            }
          }
          break;
        case DICE_LEFT:
          for (dy = 0; dy < g_cube_size; dy++) {
            i = base + dy * video_width;

            for (dx = 0; dx < g_cube_size; dx++) {
              di = base + (dx * video_width) + (g_cube_size - dy - 1);
              dest[di] = src[i];
              i++;
            }
          }
          break;
        case DICE_DOWN:
          for (dy = 0; dy < g_cube_size; dy++) {
            di = base + dy * video_width;
            i = base + (g_cube_size - dy - 1) * video_width + g_cube_size;
            for (dx = 0; dx < g_cube_size; dx++) {
              i--;
              dest[di] = src[i];
              di++;
            }
          }
          break;
        case DICE_RIGHT:
          for (dy = 0; dy < g_cube_size; dy++) {
            i = base + (dy * video_width);
            for (dx = 0; dx < g_cube_size; dx++) {
              di = base + dy + (g_cube_size - dx - 1) * video_width;
              dest[di] = src[i];
              i++;
            }
          }
          break;
        default:
          g_assert_not_reached ();
          break;
      }
      map_i++;
    }
  }

  return ret;
}

static void
gst_dicetv_create_map (GstDiceTV * filter)
{
  gint x, y, i;

  filter->g_map_height = filter->height >> filter->g_cube_bits;
  filter->g_map_width = filter->width >> filter->g_cube_bits;
  filter->g_cube_size = 1 << filter->g_cube_bits;

  i = 0;

  for (y = 0; y < filter->g_map_height; y++) {
    for (x = 0; x < filter->g_map_width; x++) {
      // dicemap[i] = ((i + y) & 0x3); /* Up, Down, Left or Right */
      filter->dicemap[i] = (fastrand () >> 24) & 0x03;
      i++;
    }
  }
}

static void
gst_dicetv_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstDiceTV *filter;

  g_return_if_fail (GST_IS_DICETV (object));

  filter = GST_DICETV (object);

  switch (prop_id) {
    case ARG_CUBE_BITS:
      filter->g_cube_bits = g_value_get_int (value);
      gst_dicetv_create_map (filter);
    default:
      break;
  }
}

static void
gst_dicetv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDiceTV *filter;

  g_return_if_fail (GST_IS_DICETV (object));

  filter = GST_DICETV (object);

  switch (prop_id) {
    case ARG_CUBE_BITS:
      g_value_set_int (value, filter->g_cube_bits);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dicetv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_dicetv_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dicetv_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dicetv_src_template));
}

static void
gst_dicetv_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_dicetv_set_property;
  gobject_class->get_property = gst_dicetv_get_property;

  g_object_class_install_property (gobject_class, ARG_CUBE_BITS,
      g_param_spec_int ("square_bits", "Square Bits", "The size of the Squares",
          MIN_CUBE_BITS, MAX_CUBE_BITS, DEFAULT_CUBE_BITS, G_PARAM_READWRITE));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_dicetv_set_caps);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_dicetv_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_dicetv_transform);
}

static void
gst_dicetv_init (GTypeInstance * instance, gpointer g_class)
{
  GstDiceTV *filter = GST_DICETV (instance);

  filter->dicemap = NULL;
  filter->g_cube_bits = DEFAULT_CUBE_BITS;
  filter->g_cube_size = 0;
  filter->g_map_height = 0;
  filter->g_map_width = 0;
}

GType
gst_dicetv_get_type (void)
{
  static GType dicetv_type = 0;

  if (!dicetv_type) {
    static const GTypeInfo dicetv_info = {
      sizeof (GstDiceTVClass),
      gst_dicetv_base_init,
      NULL,
      (GClassInitFunc) gst_dicetv_class_init,
      NULL,
      NULL,
      sizeof (GstDiceTV),
      0,
      (GInstanceInitFunc) gst_dicetv_init,
    };

    dicetv_type =
        g_type_register_static (GST_TYPE_VIDEO_FILTER, "GstDiceTV",
        &dicetv_info, 0);
  }
  return dicetv_type;
}
