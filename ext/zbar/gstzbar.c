/* GStreamer
 * Copyright (C) 2009 Stefan Kost <ensonic@users.sf.net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-zbar
 *
 * Detect bar codes in the video streams and send them as element messages to
 * the #GstBus if .#GstZBar:message property is %TRUE.
 *
 * The element generate messages named
 * <classname>&quot;barcode&quot;</classname>. The structure containes these
 * fields:
 * <itemizedlist>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;timestamp&quot;</classname>:
 *   the timestamp of the buffer that triggered the message.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   gchar*
 *   <classname>&quot;type&quot;</classname>:
 *   the symbol type.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   gchar*
 *   <classname>&quot;symbol&quot;</classname>:
 *   the deteted bar code data.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   gint
 *   <classname>&quot;quality&quot;</classname>:
 *   an unscaled, relative quantity: larger values are better than smaller
 *   values.
 *   </para>
 * </listitem>
 * </itemizedlist>
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch -m v4l2src ! videoconvert ! zbar ! videoconvert ! xvimagesink
 * ]| This pipeline will detect barcodes and send them as messages.
 * |[
 * gst-launch -m v4l2src ! tee name=t ! queue ! videoconvert ! zbar ! fakesink t. ! queue ! xvimagesink
 * ]| Same as above, but running the filter on a branch to keep the display in color
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstzbar.h"

#include <string.h>
#include <math.h>

#include <gst/video/video.h>


GST_DEBUG_CATEGORY_STATIC (zbar_debug);
#define GST_CAT_DEFAULT zbar_debug

/* GstZBar signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_MESSAGE,
  PROP_CACHE
};

#define DEFAULT_CACHE    FALSE
#define DEFAULT_MESSAGE  TRUE

/* FIXME: add YVU9 and YUV9 once we have a GstVideoFormat for that */
#define ZBAR_YUV_CAPS \
    "{ Y800, I420, YV12, NV12, NV21, Y41B, Y42B }"

static GstStaticPadTemplate gst_zbar_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (ZBAR_YUV_CAPS))
    );

static GstStaticPadTemplate gst_zbar_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (ZBAR_YUV_CAPS))
    );

static void gst_zbar_finalize (GObject * object);
static void gst_zbar_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_zbar_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_zbar_start (GstBaseTransform * base);
static gboolean gst_zbar_stop (GstBaseTransform * base);

static GstFlowReturn gst_zbar_transform_frame_ip (GstVideoFilter * vfilter,
    GstVideoFrame * frame);

#define gst_zbar_parent_class parent_class
G_DEFINE_TYPE (GstZBar, gst_zbar, GST_TYPE_VIDEO_FILTER);

static void
gst_zbar_class_init (GstZBarClass * g_class)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *trans_class;
  GstVideoFilterClass *vfilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  gstelement_class = GST_ELEMENT_CLASS (g_class);
  trans_class = GST_BASE_TRANSFORM_CLASS (g_class);
  vfilter_class = GST_VIDEO_FILTER_CLASS (g_class);

  gobject_class->set_property = gst_zbar_set_property;
  gobject_class->get_property = gst_zbar_get_property;
  gobject_class->finalize = gst_zbar_finalize;

  g_object_class_install_property (gobject_class, PROP_MESSAGE,
      g_param_spec_boolean ("message", "message",
          "Post a barcode message for each detected code",
          DEFAULT_MESSAGE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CACHE,
      g_param_spec_boolean ("cache", "cache",
          "Enable or disable the inter-image result cache",
          DEFAULT_CACHE,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "Barcode detector",
      "Filter/Analyzer/Video",
      "Detect bar codes in the video streams",
      "Stefan Kost <ensonic@users.sf.net>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_zbar_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_zbar_src_template));

  trans_class->start = GST_DEBUG_FUNCPTR (gst_zbar_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_zbar_stop);
  trans_class->transform_ip_on_passthrough = FALSE;

  vfilter_class->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_zbar_transform_frame_ip);
}

static void
gst_zbar_init (GstZBar * zbar)
{
  zbar->cache = DEFAULT_CACHE;
  zbar->message = DEFAULT_MESSAGE;

  zbar->scanner = zbar_image_scanner_create ();
}

static void
gst_zbar_finalize (GObject * object)
{
  GstZBar *zbar = GST_ZBAR (object);

  zbar_image_scanner_destroy (zbar->scanner);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_zbar_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstZBar *zbar;

  g_return_if_fail (GST_IS_ZBAR (object));
  zbar = GST_ZBAR (object);

  switch (prop_id) {
    case PROP_CACHE:
      zbar->cache = g_value_get_boolean (value);
      break;
    case PROP_MESSAGE:
      zbar->message = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_zbar_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstZBar *zbar;

  g_return_if_fail (GST_IS_ZBAR (object));
  zbar = GST_ZBAR (object);

  switch (prop_id) {
    case PROP_CACHE:
      g_value_set_boolean (value, zbar->cache);
      break;
    case PROP_MESSAGE:
      g_value_set_boolean (value, zbar->message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_zbar_transform_frame_ip (GstVideoFilter * vfilter, GstVideoFrame * frame)
{
  GstZBar *zbar = GST_ZBAR (vfilter);
  gpointer data;
  gint stride, height;
  zbar_image_t *image;
  const zbar_symbol_t *symbol;
  int n;

  image = zbar_image_create ();

  /* all formats we support start with an 8-bit Y plane. zbar doesn't need
   * to know about the chroma plane(s) */
  data = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  stride = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0);
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  zbar_image_set_format (image, GST_MAKE_FOURCC ('Y', '8', '0', '0'));
  zbar_image_set_size (image, stride, height);
  zbar_image_set_data (image, (gpointer) data, stride * height, NULL);

  /* scan the image for barcodes */
  n = zbar_scan_image (zbar->scanner, image);
  if (n == 0)
    goto out;

  /* extract results */
  symbol = zbar_image_first_symbol (image);
  for (; symbol; symbol = zbar_symbol_next (symbol)) {
    zbar_symbol_type_t typ = zbar_symbol_get_type (symbol);
    const char *data = zbar_symbol_get_data (symbol);
    gint quality = zbar_symbol_get_quality (symbol);

    GST_DEBUG_OBJECT (zbar, "decoded %s symbol \"%s\" at quality %d",
        zbar_get_symbol_name (typ), data, quality);

    if (zbar->cache && zbar_symbol_get_count (symbol) != 0)
      continue;

    if (zbar->message) {
      GstMessage *m;
      GstStructure *s;

      /* post a message */
      s = gst_structure_new ("barcode",
          "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (frame->buffer),
          "type", G_TYPE_STRING, zbar_get_symbol_name (typ),
          "symbol", G_TYPE_STRING, data, "quality", G_TYPE_INT, quality, NULL);
      m = gst_message_new_element (GST_OBJECT (zbar), s);
      gst_element_post_message (GST_ELEMENT (zbar), m);
    }
  }

out:
  /* clean up */
  zbar_image_scanner_recycle_image (zbar->scanner, image);
  zbar_image_destroy (image);

  return GST_FLOW_OK;
}

static gboolean
gst_zbar_start (GstBaseTransform * base)
{
  GstZBar *zbar = GST_ZBAR (base);

  /* start the cache if enabled (e.g. for filtering dupes) */
  zbar_image_scanner_enable_cache (zbar->scanner, zbar->cache);

  return TRUE;
}

static gboolean
gst_zbar_stop (GstBaseTransform * base)
{
  GstZBar *zbar = GST_ZBAR (base);

  /* stop the cache if enabled (e.g. for filtering dupes) */
  zbar_image_scanner_enable_cache (zbar->scanner, zbar->cache);

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (zbar_debug, "zbar", 0, "zbar");

  return gst_element_register (plugin, "zbar", GST_RANK_NONE, GST_TYPE_ZBAR);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    zbar,
    "zbar barcode scanner",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
