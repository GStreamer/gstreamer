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

#define LIBFAME_BUFFER_SIZE (300 * 1024) 

/* elementfactory information */
static GstElementDetails gst_libfame_details = {
  "MPEG1 and MPEG4 video encoder using the libfame library",
  "Codec/Video/Encoder",
  "Uses libfame to encode MPEG video streams",
  VERSION,
  "(C) 1996, MPEG Software Simulation Group\n"
  "Thomas Vander Stichele <thomas@apestaart.org>",
  "(C) 2002",
};

/* Libfame signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_VERSION,
  ARG_FPS,
  ARG_QUALITY,
  ARG_PATTERN,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "libfame_sink_caps",
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
    "libfame_src_caps",
    "video/mpeg",
      "mpegversion", GST_PROPS_LIST (
	  GST_PROPS_INT (1), GST_PROPS_INT (4)),
      "systemstream", GST_PROPS_BOOLEAN (FALSE)
  )
);

static void	gst_libfame_class_init		(GstLibfameClass *klass);
static void	gst_libfame_init		(GstLibfame *libfame);
static void	gst_libfame_dispose		(GObject *object);

static void	gst_libfame_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_libfame_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static void	gst_libfame_chain		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
/*static guint gst_libfame_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_libfame_get_type (void)
{
  static GType libfame_type = 0;

  if (!libfame_type) {
    static const GTypeInfo libfame_info = {
      sizeof (GstLibfameClass),      
      NULL,
      NULL,
      (GClassInitFunc) gst_libfame_class_init,
      NULL,
      NULL,
      sizeof (GstLibfame),
      0,
      (GInstanceInitFunc) gst_libfame_init,
    };
    libfame_type = g_type_register_static (GST_TYPE_ELEMENT, 
	                                   "GstLibfame", &libfame_info, 0);
  }
  return libfame_type;
}

static void
gst_libfame_class_init (GstLibfameClass *klass)
{
  GObjectClass *gobject_class = NULL;
  GstElementClass *gstelement_class = NULL;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_libfame_set_property;
  gobject_class->get_property = gst_libfame_get_property;
  gobject_class->dispose = gst_libfame_dispose;

  g_object_class_install_property (gobject_class, ARG_VERSION,
    g_param_spec_int ("mpeg_version", "MPEG Version", 
	              "MPEG Codec Version (1 or 4)",
                      1, 4, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_FPS,
    g_param_spec_double ("frames_per_second", "Frames per second", 
	                 "Number of frames per second",
                        -G_MAXDOUBLE, G_MAXDOUBLE, 25.0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_QUALITY,
    g_param_spec_int ("quality", "Quality", 
	              "Percentage of quality of compression (versus size)",
                      0, 100, 75, G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property (gobject_class, ARG_PATTERN,
    g_param_spec_string ("pattern", "Pattern", 
	                 "Encoding pattern of I, P, and B frames",
                         "IPP", G_PARAM_READWRITE)); /* CHECKME */
}

static GstPadConnectReturn
gst_libfame_sinkconnect (GstPad *pad, GstCaps *caps)
{
  gint width, height;
  GstLibfame *libfame;

  libfame = GST_LIBFAME (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) 
    return GST_PAD_CONNECT_DELAYED;

  if (libfame->initialized) {
    GST_DEBUG(0, "error: libfame encoder already initialized !");
    return GST_PAD_CONNECT_REFUSED;
  }

  gst_caps_get_int (caps, "width", &width);
  gst_caps_get_int (caps, "height", &height);
  
  /* FIXME: do better error handling here */
  /* libfame requires width and height to be multiples of 16 */
  g_assert (width % 16 == 0);
  g_assert (height % 16 == 0);

  libfame->fp.width = width;
  libfame->fp.height = height;
  libfame->fp.coding = (const char *) libfame->pattern;

  /* FIXME: choose good parameters */
  libfame->fp.slices_per_frame = 1;
  libfame->fp.frames_per_sequence = 12; /* infinite */
  /* FIXME: 25 fps */
  libfame->fp.frame_rate_num = 25;
  libfame->fp.frame_rate_den = 1;

  /* FIXME: handle these properly */
  libfame->fp.shape_quality = 75;
  libfame->fp.search_range = 0;
  libfame->fp.verbose = 0;
  libfame->fp.profile = "mpeg1";
  libfame->fp.total_frames = 0;
  libfame->fp.retrieve_cb = NULL;

  fame_init (libfame->fc, &libfame->fp, libfame->buffer, libfame->buffer_size);

  g_print ("libfame: init done.\n");
  g_assert (libfame->fc != NULL); /* FIXME */
  libfame->initialized = TRUE;

  return GST_PAD_CONNECT_OK;
}

static void
gst_libfame_init (GstLibfame *libfame)
{
  g_assert (libfame != NULL);
  g_assert (GST_IS_LIBFAME (libfame));

  /* open libfame */
  libfame->fc = fame_open ();
  g_assert (libfame->fc != NULL);

  /* create the sink and src pads */
  libfame->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (libfame), libfame->sinkpad);
  gst_pad_set_chain_function (libfame->sinkpad, gst_libfame_chain);
  gst_pad_set_connect_function (libfame->sinkpad, gst_libfame_sinkconnect);

  libfame->srcpad = gst_pad_new_from_template (
                      GST_PAD_TEMPLATE_GET (src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (libfame), libfame->srcpad);
  /* FIXME: set some more handler functions here */

  /* reset the initial video state */
  libfame->fp.width = -1;
  libfame->fp.height = -1;
  libfame->initialized = FALSE;

  /* defaults */
  libfame->fp.quality = 75;
  libfame->fp.frame_rate_num = 25;
  libfame->fp.frame_rate_den = 1; /* avoid floating point exceptions */
  libfame->fp.profile = g_strdup ("mpeg1");

  libfame->pattern = g_strdup ("IPP");

  /* allocate space for the buffer */
  libfame->buffer_size = LIBFAME_BUFFER_SIZE; /* FIXME */
  libfame->buffer = (unsigned char *) g_malloc (libfame->buffer_size);
}

static void
gst_libfame_dispose (GObject *object)
{
  GstLibfame *libfame = GST_LIBFAME (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  g_free (libfame->buffer);
}

static void
gst_libfame_chain (GstPad *pad, GstBuffer *buf)
{
  GstLibfame *libfame;
  guchar *data;
  gulong size;
  gint frame_size;
  gint length;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (GST_IS_BUFFER (buf));

  libfame = GST_LIBFAME (gst_pad_get_parent (pad));

  data = (guchar *) GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG (0,"gst_libfame_chain: got buffer of %ld bytes in '%s'", 
	     size, GST_OBJECT_NAME (libfame));

  /* the data contains the three planes side by side, with size w * h, w * h /4,
   * w * h / 4 */
  libfame->fy.w = libfame->fp.width;
  libfame->fy.h = libfame->fp.height;

  frame_size = libfame->fp.width * libfame->fp.height;

  libfame->fy.p = 0; /* FIXME: is this pointing to previous data ? */
  libfame->fy.y = data;
  libfame->fy.u = data + frame_size;
  libfame->fy.v = libfame->fy.u + (frame_size >> 2);

  fame_start_frame (libfame->fc, &libfame->fy, NULL);

  while ((length = fame_encode_slice (libfame->fc)) != 0) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new ();

    /* FIXME: safeguard, remove me when a better way is found */
    if (length > LIBFAME_BUFFER_SIZE)
      g_warning ("LIBFAME_BUFFER_SIZE is defined too low, encoded slice has size %d !\n", length);

    GST_BUFFER_SIZE (outbuf) = length;
    GST_BUFFER_DATA (outbuf) = g_malloc (length);
    memcpy (GST_BUFFER_DATA(outbuf), libfame->buffer, length);

    GST_DEBUG (0,"gst_libfame_chain: pushing buffer of size %d",
               GST_BUFFER_SIZE(outbuf));

    gst_pad_push (libfame->srcpad, outbuf);
  }

  fame_end_frame (libfame->fc, NULL); /* FIXME: get stats */

  gst_buffer_unref(buf);
}

static void
gst_libfame_set_property (GObject *object, guint prop_id, 
	                  const GValue *value, GParamSpec *pspec)
{
  GstLibfame *src;

  g_return_if_fail (GST_IS_LIBFAME (object));
  src = GST_LIBFAME (object);

  if (src->initialized) {
    GST_DEBUG(0, "error: libfame encoder already initialized, cannot set properties !");
    return;
  }

  switch (prop_id) {
    case ARG_VERSION:
    {
      gint version = g_value_get_int (value);

      if (version != 1 && version != 4) {
	g_warning ("libfame: only use MPEG version 1 or 4 !");
      }
      break;
    }
    case ARG_FPS:
      /* FIXME: we could do a much better job of finding num and den here */
      src->fp.frame_rate_num = g_value_get_double (value);
      src->fp.frame_rate_den = 1;
      gst_info ("libfame: setting framerate for encoding to %f (%d/%d)\n", 
		  (float) src->fp.frame_rate_num / src->fp.frame_rate_den,
		  src->fp.frame_rate_num, src->fp.frame_rate_den);
      break;
    case ARG_QUALITY:
      src->fp.quality = CLAMP (g_value_get_int (value), 0, 100);
      break;
    case ARG_PATTERN:
      g_free (src->pattern);
      src->pattern = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_libfame_get_property (GObject *object, guint prop_id, 
	                  GValue *value, GParamSpec *pspec)
{
  GstLibfame *src;

  g_return_if_fail (GST_IS_LIBFAME (object));
  src = GST_LIBFAME (object);

  switch (prop_id) {
    case ARG_FPS:
      g_value_set_double (value, (src->fp.frame_rate_num / 
		                  src->fp.frame_rate_den));
      break;
    case ARG_QUALITY:
      g_value_set_int (value, src->fp.quality);
      break;
    case ARG_PATTERN:
      g_value_set_string (value, g_strdup (src->pattern));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the libfame element */
  factory = gst_element_factory_new ("libfame", GST_TYPE_LIBFAME,
                                     &gst_libfame_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, 
      GST_PAD_TEMPLATE_GET (sink_template_factory));
  gst_element_factory_add_pad_template (factory, 
      GST_PAD_TEMPLATE_GET (src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "libfame",
  plugin_init
};
