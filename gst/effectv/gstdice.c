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
#include <string.h>
#include <gst/gst.h>
#include "gsteffectv.h"

#define GST_TYPE_DICETV \
  (gst_dicetv_get_type())
#define GST_DICETV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DICETV,GstDiceTV))
#define GST_DICETV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstDiceTV))
#define GST_IS_DICETV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DICETV))
#define GST_IS_DICETV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DICETV))

typedef struct _GstDiceTV GstDiceTV;
typedef struct _GstDiceTVClass GstDiceTVClass;

#define DEFAULT_CUBE_BITS   4
#define MAX_CUBE_BITS       5
#define MIN_CUBE_BITS       0

typedef enum _dice_dir 
{
  DICE_UP 	= 0,
  DICE_RIGHT 	= 1,
  DICE_DOWN 	= 2,
  DICE_LEFT 	= 3
} DiceDir;

struct _GstDiceTV
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gchar* dicemap;

  gint g_cube_bits;
  gint g_cube_size;
  gint g_map_height;
  gint g_map_width;
};

struct _GstDiceTVClass
{
  GstElementClass parent_class;

  void (*reset) (GstElement *element);
};

/* elementfactory information */
static GstElementDetails gst_dicetv_details = GST_ELEMENT_DETAILS (
  "DiceTV",
  "Filter/Effect/Video",
  "'Dices' the screen up into many small squares",
  "Wim Taymans <wim.taymans@chello.be>"
);


/* Filter signals and args */
enum
{
  /* FILL ME */
  RESET_SIGNAL,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_CUBE_BITS,
};

static void     gst_dicetv_base_init		(gpointer g_class);   
static void 	gst_dicetv_class_init 		(GstDiceTVClass * klass);
static void 	gst_dicetv_init 		(GstDiceTV * filter);

static void 	gst_dicetv_reset_handler 	(GstElement *elem);
static void 	gst_dicetv_create_map 		(GstDiceTV *filter);

static void 	gst_dicetv_set_property 	(GObject * object, guint prop_id,
					  	 const GValue * value, GParamSpec * pspec);
static void 	gst_dicetv_get_property 	(GObject * object, guint prop_id,
					  	 GValue * value, GParamSpec * pspec);

static void 	gst_dicetv_chain 		(GstPad * pad, GstData *_data);

static GstElementClass *parent_class = NULL;
static guint gst_dicetv_signals[LAST_SIGNAL] = { 0 };

GType gst_dicetv_get_type (void)
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

    dicetv_type = g_type_register_static (GST_TYPE_ELEMENT, "GstDiceTV", &dicetv_info, 0);
  }
  return dicetv_type;
}

static void
gst_dicetv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_effectv_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_effectv_sink_template));
 
  gst_element_class_set_details (element_class, &gst_dicetv_details);
}

static void
gst_dicetv_class_init (GstDiceTVClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_dicetv_signals[RESET_SIGNAL] =
    g_signal_new ("reset",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstDiceTVClass, reset),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  klass->reset = gst_dicetv_reset_handler;
	
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CUBE_BITS,
    g_param_spec_int ("square_bits","Square Bits","The size of the Squares",
                      MIN_CUBE_BITS, MAX_CUBE_BITS, DEFAULT_CUBE_BITS, G_PARAM_READWRITE));

  gobject_class->set_property = gst_dicetv_set_property;
  gobject_class->get_property = gst_dicetv_get_property;
}

static GstPadLinkReturn
gst_dicetv_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstDiceTV *filter;
  GstStructure *structure;

  filter = GST_DICETV (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &filter->width);
  gst_structure_get_int (structure, "height", &filter->height);

  g_free (filter->dicemap);
  filter->dicemap = (gchar *) g_malloc (filter->height * filter->width * sizeof(char));
  gst_dicetv_create_map (filter);

  return gst_pad_try_set_caps (filter->srcpad, caps);
}

static void
gst_dicetv_init (GstDiceTV * filter)
{
  filter->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_effectv_sink_template), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_dicetv_chain);
  gst_pad_set_link_function (filter->sinkpad, gst_dicetv_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_effectv_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->dicemap = NULL;
  filter->g_cube_bits = DEFAULT_CUBE_BITS;
  filter->g_cube_size = 0;
  filter->g_map_height = 0;
  filter->g_map_width = 0;
}

static void
gst_dicetv_reset_handler (GstElement *element)
{
  GstDiceTV *filter = GST_DICETV (element);

  gst_dicetv_create_map (filter);
}


static unsigned int
fastrand (void)
{   
  static unsigned int fastrand_val;

  return (fastrand_val = fastrand_val * 1103515245 + 12345);
}

static void 
gst_dicetv_draw (GstDiceTV *filter, guint32 *src, guint32 *dest)
{
  gint i;
  gint map_x, map_y, map_i;
  gint base;
  gint dx, dy, di;
  gint video_width = filter->width;
  gint g_cube_bits = filter->g_cube_bits;
  gint g_cube_size = filter->g_cube_size;
    
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
}

static void 
gst_dicetv_create_map (GstDiceTV *filter)
{
  gint x, y, i;
    
  filter->g_map_height = filter->height >> filter->g_cube_bits;
  filter->g_map_width = filter->width >> filter->g_cube_bits;
  filter->g_cube_size = 1 << filter->g_cube_bits;

  i = 0;

  for (y = 0; y < filter->g_map_height; y++) {
    for(x = 0; x < filter->g_map_width; x++) {
      // dicemap[i] = ((i + y) & 0x3); /* Up, Down, Left or Right */
      filter->dicemap[i] = (fastrand() >> 24) & 0x03;
      i++;
    }
  }
}

static void
gst_dicetv_chain (GstPad * pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstDiceTV *filter;
  guint32 *src, *dest;
  GstBuffer *outbuf;

  filter = GST_DICETV (gst_pad_get_parent (pad));

  src = (guint32 *) GST_BUFFER_DATA (buf);

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = (filter->width * filter->height * sizeof(guint32));
  dest = (guint32 *) GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  gst_dicetv_draw (filter, src, dest);
  
  gst_buffer_unref (buf);

  gst_pad_push (filter->srcpad, GST_DATA (outbuf));
}

static void
gst_dicetv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstDiceTV *filter;

  /* it's not null if we got it, but it might not be ours */
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
gst_dicetv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstDiceTV *filter;

  /* it's not null if we got it, but it might not be ours */
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
