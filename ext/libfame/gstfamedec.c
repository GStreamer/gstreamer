/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <fame.h>
#include <string.h>

#include "gstlibfame.h"

#define FAMEENC_BUFFER_SIZE (300 * 1024) 

/* elementfactory information */
static GstElementDetails gst_famedec_details = {
  "MPEG1 and MPEG4 video decoder using the famedec library",
  "Codec/Video/Encoder",
  "Uses famedec to decode MPEG video streams",
  VERSION,
  "famedec: (C) 2000-2001, Vivien Chappelier\n"
  "Thomas Vander Stichele <thomas@apestaart.org>",
  "(C) 2002",
};

static GQuark fame_object_name;


/* FameEnc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_VERSION,
  ARG_FRAMERATE,
  ARG_BITRATE,
  ARG_QUALITY,
  ARG_PATTERN,
  ARG_FAME_VERBOSE,
  ARG_BUFFER_SIZE,
  ARG_FRAMES_PER_SEQUENCE,
  /* dynamically generated properties start here */
  ARG_FAME_PROPS_START
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "famedec_sink_caps",
    "video/raw",
      "format",		GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
      "width",		GST_PROPS_INT_RANGE (16, 4096),
      "height",		GST_PROPS_INT_RANGE (16, 4096)
  )
)

GST_PAD_TEMPLATE_FACTORY (src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "famedec_src_caps",
    "video/mpeg",
      "mpegversion", GST_PROPS_LIST (
	  GST_PROPS_INT (1), GST_PROPS_INT (4)),
      "systemstream", GST_PROPS_BOOLEAN (FALSE)
  )
);

#define MAX_FRAME_RATES  16
typedef struct
{
  gint num;
  gint den;
} frame_rate_entry;

static const frame_rate_entry frame_rates[] =
{
  { 0, 0 },
  { 24000, 1001 },
  { 24, 1 },
  { 25, 1 },
  { 30000, 1001 },
  { 30, 1 },
  { 50, 1 },
  { 60000, 1001 },
  { 60, 1 },
  { 0, 0 },
  { 0, 0 },
  { 0, 0 },
  { 0, 0 },
  { 0, 0 },
  { 0, 0 },
  { 0, 0 },
};

static gint
framerate_to_index (num, den)
{
  gint i;
  
  for (i = 0; i < MAX_FRAME_RATES; i++) {
    if (frame_rates[i].num == num && frame_rates[i].den == den)
      return i;
  }
  return 0;
}

#define GST_TYPE_FAMEENC_FRAMERATE (gst_famedec_framerate_get_type())
static GType
gst_famedec_framerate_get_type(void) {
  static GType famedec_framerate_type = 0;
  static GEnumValue famedec_framerate[] = {
    {1, "1", "24000/1001 (23.97)"},
    {2, "2", "24"},
    {3, "3", "25"},
    {4, "4", "30000/1001 (29.97)"},
    {5, "5", "30"},
    {6, "6", "50"},
    {7, "7", "60000/1001 (59.94)"},
    {8, "8", "60"},
    {0, NULL, NULL},
  };
  if (!famedec_framerate_type) {
    famedec_framerate_type = g_enum_register_static("GstFameEncFrameRate", famedec_framerate);
  }
  return famedec_framerate_type;
}

static void	gst_famedec_class_init		(GstFameEncClass *klass);
static void	gst_famedec_init		(GstFameEnc *famedec);
static void	gst_famedec_dispose		(GObject *object);

static void	gst_famedec_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_famedec_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static void	gst_famedec_chain		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
/*static guint gst_famedec_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_famedec_get_type (void)
{
  static GType famedec_type = 0;

  if (!famedec_type) {
    static const GTypeInfo famedec_info = {
      sizeof (GstFameEncClass),      
      NULL,
      NULL,
      (GClassInitFunc) gst_famedec_class_init,
      NULL,
      NULL,
      sizeof (GstFameEnc),
      0,
      (GInstanceInitFunc) gst_famedec_init,
    };
    famedec_type = g_type_register_static (GST_TYPE_ELEMENT, 
	                                   "GstFameEnc", &famedec_info, 0);
  }
  return famedec_type;
}

static int
gst_famedec_item_compare (fame_list_t *item1, fame_list_t *item2)
{
  return strcmp (item1->type, item2->type);
}

static void
gst_famedec_class_init (GstFameEncClass *klass)
{
  GObjectClass *gobject_class = NULL;
  GstElementClass *gstelement_class = NULL;
  fame_context_t *context;
  fame_list_t *walk;
  GList *props = NULL, *props_walk;
  gint current_prop = ARG_FAME_PROPS_START;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_famedec_set_property;
  gobject_class->get_property = gst_famedec_get_property;
  gobject_class->dispose = gst_famedec_dispose;

  fame_object_name = g_quark_from_string ("GstFameObjectName");

  context = fame_open ();
  g_assert (context);

  /* first sort the list */
  walk = context->type_list;
  while (walk) {
    props = g_list_insert_sorted (props, walk, (GCompareFunc)gst_famedec_item_compare);
    walk = walk->next;
  }

  props_walk = props;
  while (props_walk) {
    GArray *array;
    const gchar *current_type;
    gint current_len;
    gint current_value;
    fame_object_t *current_default;
    gint default_index;

    walk = (fame_list_t *)props_walk->data;
    array = g_array_new (TRUE, FALSE, sizeof (GEnumValue));

    current_type = walk->type;
    current_value = 0;
    current_len = strlen (walk->type);
    current_default = fame_get_object (context, current_type);
    default_index = 1;

    do {
      if (strstr (walk->type, "/")) {
	GEnumValue value;

	if (current_default == walk->item) 
          default_index = current_value;

	value.value = current_value++;
	value.value_name = g_strdup (walk->type);
	value.value_nick = g_strdup (walk->item->name);
	
	g_array_append_val (array, value);
      }

      props_walk = g_list_next (props_walk);
      if (props_walk)
	walk = (fame_list_t *)props_walk->data;

    } while (props_walk && !strncmp (walk->type, current_type, current_len));

    if (array->len > 0) {
      GType type;
      GParamSpec *pspec;
      
      type = g_enum_register_static (g_strdup_printf ("GstFameEnc_%s", current_type), (GEnumValue *)array->data);

      pspec = g_param_spec_enum (current_type, current_type, g_strdup_printf ("The FAME \"%s\" object", current_type),
                           type, default_index, G_PARAM_READWRITE);

      g_param_spec_set_qdata (pspec, fame_object_name, (gpointer) current_type);
      
      g_object_class_install_property (G_OBJECT_CLASS (klass), current_prop++, pspec);
    }
  }

  g_object_class_install_property (gobject_class, ARG_FRAMERATE,
    g_param_spec_enum ("framerate", "Frame Rate", "Number of frames per second",
                       GST_TYPE_FAMEENC_FRAMERATE, 3, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BITRATE,
    g_param_spec_int ("bitrate", "Bitrate", "Target bitrate (0 = VBR)",
                      0, 5000000, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_QUALITY,
    g_param_spec_int ("quality", "Quality", "Percentage of quality of compression (versus size)",
                      0, 100, 75, G_PARAM_READWRITE)); 
  g_object_class_install_property (gobject_class, ARG_PATTERN,
    g_param_spec_string ("pattern", "Pattern", "Encoding pattern of I, P, and B frames",
                         "IPPPPPPPPPPP", G_PARAM_READWRITE)); 
  g_object_class_install_property (gobject_class, ARG_FRAMES_PER_SEQUENCE,
    g_param_spec_int ("frames_per_sequence", "Frames Per Sequdece", 
	              "The number of frames in one sequence",
                      1, G_MAXINT, 12, G_PARAM_READWRITE)); 
  g_object_class_install_property (gobject_class, ARG_FAME_VERBOSE,
    g_param_spec_boolean ("fame_verbose", "Fame Verbose", "Make FAME produce verbose output",
                         FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BUFFER_SIZE,
    g_param_spec_int ("buffer_size", "Buffer Size", "Set the decoding output buffer size",
                      0, 1024*1024, FAMEENC_BUFFER_SIZE, G_PARAM_READWRITE)); 
}

static GstPadConnectReturn
gst_famedec_sinkconnect (GstPad *pad, GstCaps *caps)
{
  gint width, height;
  GstFameEnc *famedec;

  famedec = GST_FAMEENC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) 
    return GST_PAD_CONNECT_DELAYED;

  if (famedec->initialized) {
    GST_DEBUG(0, "error: famedec decoder already initialized !");
    return GST_PAD_CONNECT_REFUSED;
  }

  gst_caps_get_int (caps, "width", &width);
  gst_caps_get_int (caps, "height", &height);
  
  /* famedec requires width and height to be multiples of 16 */
  if (width % 16 != 0 || height % 16 != 0)
    return GST_PAD_CONNECT_REFUSED;

  famedec->fp.width = width;
  famedec->fp.height = height;
  famedec->fp.coding = (const char *) famedec->pattern;

  /* FIXME: choose good parameters */
  famedec->fp.slices_per_frame = 1;

  /* FIXME: handle these properly */
  famedec->fp.shape_quality = 75;
  famedec->fp.search_range = 0;
  famedec->fp.total_frames = 0;
  famedec->fp.retrieve_cb = NULL;

  fame_init (famedec->fc, &famedec->fp, famedec->buffer, famedec->buffer_size);

  famedec->initialized = TRUE;

  return GST_PAD_CONNECT_OK;
}

static void
gst_famedec_init (GstFameEnc *famedec)
{
  g_assert (famedec != NULL);
  g_assert (GST_IS_FAMEENC (famedec));

  /* open famedec */
  famedec->fc = fame_open ();
  g_assert (famedec->fc != NULL);

  /* create the sink and src pads */
  famedec->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (famedec), famedec->sinkpad);
  gst_pad_set_chain_function (famedec->sinkpad, gst_famedec_chain);
  gst_pad_set_connect_function (famedec->sinkpad, gst_famedec_sinkconnect);

  famedec->srcpad = gst_pad_new_from_template (
                      GST_PAD_TEMPLATE_GET (src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (famedec), famedec->srcpad);
  /* FIXME: set some more handler functions here */

  famedec->verbose = FALSE;

  /* reset the initial video state */
  famedec->fp.width = -1;
  famedec->fp.height = -1;
  famedec->initialized = FALSE;

  /* defaults */
  famedec->fp.bitrate = 0;
  famedec->fp.quality = 75;
  famedec->fp.frame_rate_num = 25;
  famedec->fp.frame_rate_den = 1; /* avoid floating point exceptions */
  famedec->fp.frames_per_sequence = 12; 

  famedec->pattern = g_strdup ("IPPPPPPPPPP");

  /* allocate space for the buffer */
  famedec->buffer_size = FAMEENC_BUFFER_SIZE; /* FIXME */
  famedec->buffer = (unsigned char *) g_malloc (famedec->buffer_size);
}

static void
gst_famedec_dispose (GObject *object)
{
  GstFameEnc *famedec = GST_FAMEENC (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  g_free (famedec->buffer);
}

static void
gst_famedec_chain (GstPad *pad, GstBuffer *buf)
{
  GstFameEnc *famedec;
  guchar *data;
  gulong size;
  gint frame_size;
  gint length;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (GST_IS_BUFFER (buf));

  famedec = GST_FAMEENC (gst_pad_get_parent (pad));

  data = (guchar *) GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG (0,"gst_famedec_chain: got buffer of %ld bytes in '%s'", 
	     size, GST_OBJECT_NAME (famedec));

  /* the data contains the three planes side by side, with size w * h, w * h /4,
   * w * h / 4 */
  famedec->fy.w = famedec->fp.width;
  famedec->fy.h = famedec->fp.height;

  frame_size = famedec->fp.width * famedec->fp.height;

  famedec->fy.p = 0; 
  famedec->fy.y = data;
  famedec->fy.u = data + frame_size;
  famedec->fy.v = famedec->fy.u + (frame_size >> 2);

  fame_start_frame (famedec->fc, &famedec->fy, NULL);

  while ((length = fame_encode_slice (famedec->fc)) != 0) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new ();

    /* FIXME: safeguard, remove me when a better way is found */
    if (length > FAMEENC_BUFFER_SIZE)
      g_warning ("FAMEENC_BUFFER_SIZE is defined too low, decoded slice has size %d !\n", length);

    GST_BUFFER_SIZE (outbuf) = length;
    GST_BUFFER_DATA (outbuf) = g_malloc (length);
    memcpy (GST_BUFFER_DATA(outbuf), famedec->buffer, length);

    GST_DEBUG (0,"gst_famedec_chain: pushing buffer of size %d",
               GST_BUFFER_SIZE(outbuf));

    gst_pad_push (famedec->srcpad, outbuf);
  }

  fame_end_frame (famedec->fc, NULL); 

  gst_buffer_unref(buf);
}

static void
gst_famedec_set_property (GObject *object, guint prop_id, 
	                  const GValue *value, GParamSpec *pspec)
{
  GstFameEnc *famedec;

  g_return_if_fail (GST_IS_FAMEENC (object));
  famedec = GST_FAMEENC (object);

  if (famedec->initialized) {
    GST_DEBUG(0, "error: famedec decoder already initialized, cannot set properties !");
    return;
  }

  switch (prop_id) {
    case ARG_FRAMERATE:
    {
      gint index = g_value_get_enum (value);

      famedec->fp.frame_rate_num = frame_rates[index].num;
      famedec->fp.frame_rate_den = frame_rates[index].den;
      break;
    }
    case ARG_BITRATE:
      famedec->fp.bitrate = g_value_get_int (value);
      break;
    case ARG_QUALITY:
      famedec->fp.quality = CLAMP (g_value_get_int (value), 0, 100);
      break;
    case ARG_PATTERN:
      g_free (famedec->pattern);
      famedec->pattern = g_strdup (g_value_get_string (value));
      break;
    case ARG_FAME_VERBOSE:
      famedec->verbose = g_value_get_boolean (value);
      break;
    case ARG_BUFFER_SIZE:
      famedec->buffer_size = g_value_get_int (value);
      break;
    case ARG_FRAMES_PER_SEQUENCE:
      famedec->fp.frames_per_sequence = g_value_get_int (value);
      break;
    default:
      if (prop_id >= ARG_FAME_PROPS_START) {
	gchar *name;
	gint index = g_value_get_enum (value);
	GEnumValue *values;

	values = G_ENUM_CLASS (g_type_class_ref (pspec->value_type))->values;
	name = (gchar *) g_param_spec_get_qdata (pspec, fame_object_name);
	
        fame_register (famedec->fc, name, fame_get_object (famedec->fc, values[index].value_name));
      }
      else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_famedec_get_property (GObject *object, guint prop_id, 
	                  GValue *value, GParamSpec *pspec)
{
  GstFameEnc *famedec;

  g_return_if_fail (GST_IS_FAMEENC (object));
  famedec = GST_FAMEENC (object);

  switch (prop_id) {
    case ARG_FRAMERATE:
    {
      gint index = framerate_to_index (famedec->fp.frame_rate_num, 
		                       famedec->fp.frame_rate_den);
      g_value_set_enum (value, index);
      break;
    }
    case ARG_BITRATE:
      g_value_set_int (value, famedec->fp.bitrate);
      break;
    case ARG_QUALITY:
      g_value_set_int (value, famedec->fp.quality);
      break;
    case ARG_PATTERN:
      g_value_set_string (value, g_strdup (famedec->pattern));
      break;
    case ARG_FAME_VERBOSE:
      g_value_set_boolean (value, famedec->verbose);
      break;
    case ARG_BUFFER_SIZE:
      g_value_set_int (value, famedec->buffer_size);
      break;
    case ARG_FRAMES_PER_SEQUENCE:
      g_value_set_int (value, famedec->fp.frames_per_sequence);
      break;
    default:
      if (prop_id >= ARG_FAME_PROPS_START) {
	gchar *name;
	gint index = 0;
	GEnumValue *values;
	fame_object_t *f_object;

	values = G_ENUM_CLASS (g_type_class_ref (pspec->value_type))->values;
	name = (gchar *) g_param_spec_get_qdata (pspec, fame_object_name);
	
	f_object = fame_get_object (famedec->fc, name);

	while (values[index].value_name) {
	  if (!strcmp (values[index].value_nick, f_object->name)) {
            g_value_set_enum (value, index);
	    return;
	  }
	  index++;
	}
      }
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_famedec_plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the famedec element */
  factory = gst_element_factory_new ("famedec", GST_TYPE_FAMEENC,
                                     &gst_famedec_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, 
      GST_PAD_TEMPLATE_GET (sink_template_factory));
  gst_element_factory_add_pad_template (factory, 
      GST_PAD_TEMPLATE_GET (src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

