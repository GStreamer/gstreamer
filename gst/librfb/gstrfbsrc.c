/* GStreamer
 * Copyright (C) <2007> Thijs Vermeir <thijsvermeir@gmail.com>
 * Copyright (C) <2006> Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>
 * Copyright (C) <2004> David A. Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrfbsrc.h"

#include <gst/video/video.h>

#include <string.h>
#include <stdlib.h>
#ifdef HAVE_X11
#include <X11/Xlib.h>
#endif

enum
{
  ARG_0,
  ARG_HOST,
  ARG_PORT,
  ARG_VERSION,
  ARG_PASSWORD,
  ARG_OFFSET_X,
  ARG_OFFSET_Y,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_INCREMENTAL,
  ARG_USE_COPYRECT,
  ARG_SHARED,
  ARG_VIEWONLY
};

GST_DEBUG_CATEGORY_STATIC (rfbsrc_debug);
GST_DEBUG_CATEGORY (rfbdecoder_debug);
#define GST_CAT_DEFAULT rfbsrc_debug

static GstStaticPadTemplate gst_rfb_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) [1, 255], "
        "depth = (int) [1, 255], "
        "endianness = (int) [1234, 4321], "
        "red_mask = (int) [min, max], "
        "green_mask = (int) [min, max], "
        "blue_mask = (int) [min, max], "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (fraction) 0/1")
    );

static void gst_rfb_src_dispose (GObject * object);
static void gst_rfb_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rfb_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rfb_src_start (GstBaseSrc * bsrc);
static gboolean gst_rfb_src_stop (GstBaseSrc * bsrc);
static gboolean gst_rfb_src_event (GstBaseSrc * bsrc, GstEvent * event);
static GstFlowReturn gst_rfb_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);

GST_BOILERPLATE (GstRfbSrc, gst_rfb_src, GstPushSrc, GST_TYPE_PUSH_SRC);

static void
gst_rfb_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rfb_src_template);

  gst_element_class_set_details_simple (element_class, "Rfb source",
      "Source/Video",
      "Creates a rfb video stream",
      "David A. Schleef <ds@schleef.org>, "
      "Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>, "
      "Thijs Vermeir <thijsvermeir@gmail.com>");
}

static void
gst_rfb_src_class_init (GstRfbSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  GST_DEBUG_CATEGORY_INIT (rfbsrc_debug, "rfbsrc", 0, "rfb src element");
  GST_DEBUG_CATEGORY_INIT (rfbdecoder_debug, "rfbdecoder", 0, "rfb decoder");

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->dispose = gst_rfb_src_dispose;
  gobject_class->set_property = gst_rfb_src_set_property;
  gobject_class->get_property = gst_rfb_src_get_property;

  g_object_class_install_property (gobject_class, ARG_HOST,
      g_param_spec_string ("host", "Host to connect to", "Host to connect to",
          "127.0.0.1", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_PORT,
      g_param_spec_int ("port", "Port", "Port",
          1, 65535, 5900, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_VERSION,
      g_param_spec_string ("version", "RFB protocol version",
          "RFB protocol version", "3.3",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_PASSWORD,
      g_param_spec_string ("password", "Password for authentication",
          "Password for authentication", "",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_OFFSET_X,
      g_param_spec_int ("offset-x", "x offset for screen scrapping",
          "x offset for screen scrapping", 0, 65535, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_OFFSET_Y,
      g_param_spec_int ("offset-y", "y offset for screen scrapping",
          "y offset for screen scrapping", 0, 65535, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_WIDTH,
      g_param_spec_int ("width", "width of screen", "width of screen", 0, 65535,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_HEIGHT,
      g_param_spec_int ("height", "height of screen", "height of screen", 0,
          65535, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_INCREMENTAL,
      g_param_spec_boolean ("incremental", "Incremental updates",
          "Incremental updates", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_USE_COPYRECT,
      g_param_spec_boolean ("use-copyrect", "Use copyrect encoding",
          "Use copyrect encoding", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_SHARED,
      g_param_spec_boolean ("shared", "Share desktop with other clients",
          "Share desktop with other clients", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_VIEWONLY,
      g_param_spec_boolean ("view-only", "Only view the desktop",
          "only view the desktop", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_rfb_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_rfb_src_stop);
  gstbasesrc_class->event = GST_DEBUG_FUNCPTR (gst_rfb_src_event);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_rfb_src_create);
}

static void
gst_rfb_src_init (GstRfbSrc * src, GstRfbSrcClass * klass)
{
  GstBaseSrc *bsrc = GST_BASE_SRC (src);

  gst_pad_use_fixed_caps (GST_BASE_SRC_PAD (bsrc));
  gst_base_src_set_live (bsrc, TRUE);
  gst_base_src_set_format (bsrc, GST_FORMAT_TIME);

  src->host = g_strdup ("127.0.0.1");
  src->port = 5900;
  src->version_major = 3;
  src->version_minor = 3;

  src->incremental_update = TRUE;

  src->view_only = FALSE;

  src->decoder = rfb_decoder_new ();

}

static void
gst_rfb_src_dispose (GObject * object)
{
  GstRfbSrc *src = GST_RFB_SRC (object);

  g_free (src->host);
  if (src->decoder) {
    rfb_decoder_free (src->decoder);
    g_free (src->decoder);
    src->decoder = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_rfb_property_set_version (GstRfbSrc * src, gchar * value)
{
  gchar *major;
  gchar *minor;

  g_return_if_fail (src != NULL);
  g_return_if_fail (value != NULL);

  major = g_strdup (value);
  minor = g_strrstr (value, ".");

  g_return_if_fail (minor != NULL);

  *minor++ = 0;

  g_return_if_fail (g_ascii_isdigit (*major) == TRUE);
  g_return_if_fail (g_ascii_isdigit (*minor) == TRUE);

  src->version_major = g_ascii_digit_value (*major);
  src->version_minor = g_ascii_digit_value (*minor);

  GST_DEBUG ("Version major : %d", src->version_major);
  GST_DEBUG ("Version minor : %d", src->version_minor);

  g_free (major);
  g_free (value);
}

static gchar *
gst_rfb_property_get_version (GstRfbSrc * src)
{
  return g_strdup_printf ("%d.%d", src->version_major, src->version_minor);
}

static void
gst_rfb_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRfbSrc *src = GST_RFB_SRC (object);

  switch (prop_id) {
    case ARG_HOST:
      src->host = g_strdup (g_value_get_string (value));
      break;
    case ARG_PORT:
      src->port = g_value_get_int (value);
      break;
    case ARG_VERSION:
      gst_rfb_property_set_version (src, g_strdup (g_value_get_string (value)));
      break;
    case ARG_PASSWORD:
      g_free (src->decoder->password);
      src->decoder->password = g_strdup (g_value_get_string (value));
      break;
    case ARG_OFFSET_X:
      src->decoder->offset_x = g_value_get_int (value);
      break;
    case ARG_OFFSET_Y:
      src->decoder->offset_y = g_value_get_int (value);
      break;
    case ARG_WIDTH:
      src->decoder->rect_width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      src->decoder->rect_height = g_value_get_int (value);
      break;
    case ARG_INCREMENTAL:
      src->incremental_update = g_value_get_boolean (value);
      break;
    case ARG_USE_COPYRECT:
      src->decoder->use_copyrect = g_value_get_boolean (value);
      break;
    case ARG_SHARED:
      src->decoder->shared_flag = g_value_get_boolean (value);
      break;
    case ARG_VIEWONLY:
      src->view_only = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_rfb_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRfbSrc *src = GST_RFB_SRC (object);
  gchar *version;

  switch (prop_id) {
    case ARG_HOST:
      g_value_set_string (value, src->host);
      break;
    case ARG_PORT:
      g_value_set_int (value, src->port);
      break;
    case ARG_VERSION:
      version = gst_rfb_property_get_version (src);
      g_value_set_string (value, version);
      g_free (version);
      break;
    case ARG_OFFSET_X:
      g_value_set_int (value, src->decoder->offset_x);
      break;
    case ARG_OFFSET_Y:
      g_value_set_int (value, src->decoder->offset_y);
      break;
    case ARG_WIDTH:
      g_value_set_int (value, src->decoder->rect_width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, src->decoder->rect_height);
      break;
    case ARG_INCREMENTAL:
      g_value_set_boolean (value, src->incremental_update);
      break;
    case ARG_USE_COPYRECT:
      g_value_set_boolean (value, src->decoder->use_copyrect);
      break;
    case ARG_SHARED:
      g_value_set_boolean (value, src->decoder->shared_flag);
      break;
    case ARG_VIEWONLY:
      g_value_set_boolean (value, src->view_only);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_rfb_src_start (GstBaseSrc * bsrc)
{
  GstRfbSrc *src = GST_RFB_SRC (bsrc);
  RfbDecoder *decoder;
  GstCaps *caps;
  guint32 red_mask, green_mask, blue_mask;

  decoder = src->decoder;

  GST_DEBUG_OBJECT (src, "connecting to host %s on port %d",
      src->host, src->port);
  if (!rfb_decoder_connect_tcp (decoder, src->host, src->port)) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Could not connect to host %s on port %d", src->host, src->port));
    rfb_decoder_free (decoder);
    return FALSE;
  }

  while (!decoder->inited) {
    rfb_decoder_iterate (decoder);
  }

  decoder->rect_width =
      (decoder->rect_width ? decoder->rect_width : decoder->width);
  decoder->rect_height =
      (decoder->rect_height ? decoder->rect_height : decoder->height);

  g_object_set (bsrc, "blocksize",
      src->decoder->width * src->decoder->height * (decoder->bpp / 8), NULL);

  decoder->frame = g_malloc (bsrc->blocksize);
  if (decoder->use_copyrect) {
    decoder->prev_frame = g_malloc (bsrc->blocksize);
  }
  decoder->decoder_private = src;

  /* calculate some many used values */
  decoder->bytespp = decoder->bpp / 8;
  decoder->line_size = decoder->rect_width * decoder->bytespp;

  GST_DEBUG_OBJECT (src, "setting caps width to %d and height to %d",
      decoder->rect_width, decoder->rect_height);

  caps =
      gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc)));

  red_mask = decoder->red_max << decoder->red_shift;
  green_mask = decoder->green_max << decoder->green_shift;
  blue_mask = decoder->blue_max << decoder->blue_shift;

  gst_caps_set_simple (caps, "width", G_TYPE_INT, decoder->rect_width,
      "height", G_TYPE_INT, decoder->rect_height,
      "bpp", G_TYPE_INT, decoder->bpp,
      "depth", G_TYPE_INT, decoder->depth,
      "endianness", G_TYPE_INT, G_BIG_ENDIAN,
      "red_mask", G_TYPE_INT, GST_READ_UINT32_BE (&red_mask),
      "green_mask", G_TYPE_INT, GST_READ_UINT32_BE (&green_mask),
      "blue_mask", G_TYPE_INT, GST_READ_UINT32_BE (&blue_mask), NULL);

  gst_pad_set_caps (GST_BASE_SRC_PAD (bsrc), caps);
  gst_caps_unref (caps);

  return TRUE;
}

static gboolean
gst_rfb_src_stop (GstBaseSrc * bsrc)
{
  GstRfbSrc *src = GST_RFB_SRC (bsrc);

  src->decoder->fd = -1;

  if (src->decoder->frame) {
    g_free (src->decoder->frame);
    src->decoder->frame = NULL;
  }

  if (src->decoder->prev_frame) {
    g_free (src->decoder->prev_frame);
    src->decoder->prev_frame = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_rfb_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstRfbSrc *src = GST_RFB_SRC (psrc);
  RfbDecoder *decoder = src->decoder;
  gulong newsize;
  GstFlowReturn ret;

  rfb_decoder_send_update_request (decoder, src->incremental_update,
      decoder->offset_x, decoder->offset_y, decoder->rect_width,
      decoder->rect_height);

  while (decoder->state != NULL) {
    rfb_decoder_iterate (decoder);
  }

  newsize = GST_BASE_SRC (psrc)->blocksize;

  /* Create the buffer. */
  ret = gst_pad_alloc_buffer (GST_BASE_SRC_PAD (GST_BASE_SRC (psrc)),
      GST_BUFFER_OFFSET_NONE, newsize,
      GST_PAD_CAPS (GST_BASE_SRC_PAD (GST_BASE_SRC (psrc))), outbuf);

  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    return GST_FLOW_ERROR;
  }

  memcpy (GST_BUFFER_DATA (*outbuf), decoder->frame, newsize);
  GST_BUFFER_SIZE (*outbuf) = newsize;
  GST_BUFFER_TIMESTAMP (*outbuf) =
      gst_clock_get_time (GST_ELEMENT_CLOCK (src)) -
      GST_ELEMENT_CAST (src)->base_time;

  return GST_FLOW_OK;
}

static gboolean
gst_rfb_src_event (GstBaseSrc * bsrc, GstEvent * event)
{
  GstRfbSrc *src = GST_RFB_SRC (bsrc);
  gdouble x, y;
  gint button;
  GstStructure *structure;
  const gchar *event_type;
  gboolean key_event, key_press;

  key_event = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:

      /* if in view_only mode ignore the navigation event */
      if (src->view_only)
        break;

      structure = event->structure;
      event_type = gst_structure_get_string (structure, "event");

      if (strcmp (event_type, "key-press") == 0) {
        key_event = key_press = TRUE;
      } else if (strcmp (event_type, "key-release") == 0) {
        key_event = TRUE;
        key_press = FALSE;
      }

      if (key_event) {
#ifdef HAVE_X11
        const gchar *key;
        KeySym key_sym;

        key = gst_structure_get_string (structure, "key");
        key_sym = XStringToKeysym (key);

        if (key_sym != NoSymbol)
          rfb_decoder_send_key_event (src->decoder, key_sym, key_press);
#endif
        break;
      }

      gst_structure_get_double (structure, "pointer_x", &x);
      gst_structure_get_double (structure, "pointer_y", &y);
      gst_structure_get_int (structure, "button", &button);

      /* we need to take care of the offset's */
      x += src->decoder->offset_x;
      y += src->decoder->offset_y;

      if (strcmp (event_type, "mouse-move") == 0) {
        GST_LOG_OBJECT (src, "sending mouse-move event "
            "button_mask=%d, x=%d, y=%d", src->button_mask, (gint) x, (gint) y);
        rfb_decoder_send_pointer_event (src->decoder, src->button_mask,
            (gint) x, (gint) y);
      } else if (strcmp (event_type, "mouse-button-release") == 0) {
        src->button_mask &= ~(1 << (button - 1));
        GST_LOG_OBJECT (src, "sending mouse-button-release event "
            "button_mask=%d, x=%d, y=%d", src->button_mask, (gint) x, (gint) y);
        rfb_decoder_send_pointer_event (src->decoder, src->button_mask,
            (gint) x, (gint) y);
      } else if (strcmp (event_type, "mouse-button-press") == 0) {
        src->button_mask |= (1 << (button - 1));
        GST_LOG_OBJECT (src, "sending mouse-button-press event "
            "button_mask=%d, x=%d, y=%d", src->button_mask, (gint) x, (gint) y);
        rfb_decoder_send_pointer_event (src->decoder, src->button_mask,
            (gint) x, (gint) y);
      }
      break;
    default:
      break;
  }

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rfbsrc", GST_RANK_NONE,
      GST_TYPE_RFB_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rfbsrc",
    "Connects to a VNC server and decodes RFB stream",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
