/* GStreamer
 * Copyright (C) <2001,2002> Richard Boulton <richard-gst@tartarus.org>
 *
 * Based on example.c:
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
#include "esdmon.h"
#include <esd.h>
#include <unistd.h>


/* elementfactory information */
static GstElementDetails esdmon_details = {
  "Esound audio monitor",
  "Source/Audio",
  "Monitors audio from an esound server",
  "Richard Boulton <richard-gst@tartarus.org>",
};

/* Signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DEPTH,
  ARG_BYTESPERREAD,
  ARG_CUROFFSET,
  ARG_CHANNELS,
  ARG_RATE,
  ARG_HOST,
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = [ 8000, 96000 ], "
        "channels = [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) FALSE, "
        "width = (int) 8, "
        "depth = (int) 8, " "rate = [ 8000, 96000 ], " "channels = [ 1, 2 ]")
    );

static void gst_esdmon_base_init (gpointer g_class);
static void gst_esdmon_class_init (gpointer g_class, gpointer class_data);
static void gst_esdmon_init (GTypeInstance * instance, gpointer g_class);

static gboolean gst_esdmon_open_audio (GstEsdmon * src);
static void gst_esdmon_close_audio (GstEsdmon * src);
static GstElementStateReturn gst_esdmon_change_state (GstElement * element);
static gboolean gst_esdmon_sync_parms (GstEsdmon * esdmon);

static GstData *gst_esdmon_get (GstPad * pad);

static void gst_esdmon_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_esdmon_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define GST_TYPE_ESDMON_DEPTHS (gst_esdmon_depths_get_type())
static GType
gst_esdmon_depths_get_type (void)
{
  static GType esdmon_depths_type = 0;
  static GEnumValue esdmon_depths[] = {
    {8, "8", "8 Bits"},
    {16, "16", "16 Bits"},
    {0, NULL, NULL},
  };

  if (!esdmon_depths_type) {
    esdmon_depths_type =
        g_enum_register_static ("GstEsdmonDepths", esdmon_depths);
  }
  return esdmon_depths_type;
}

#define GST_TYPE_ESDMON_CHANNELS (gst_esdmon_channels_get_type())
static GType
gst_esdmon_channels_get_type (void)
{
  static GType esdmon_channels_type = 0;
  static GEnumValue esdmon_channels[] = {
    {1, "1", "Mono"},
    {2, "2", "Stereo"},
    {0, NULL, NULL},
  };

  if (!esdmon_channels_type) {
    esdmon_channels_type =
        g_enum_register_static ("GstEsdmonChannels", esdmon_channels);
  }
  return esdmon_channels_type;
}


static GstElementClass *parent_class = NULL;

/*static guint gst_esdmon_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_esdmon_get_type (void)
{
  static GType esdmon_type = 0;

  if (!esdmon_type) {
    static const GTypeInfo esdmon_info = {
      sizeof (GstEsdmonClass),
      gst_esdmon_base_init,
      NULL,
      gst_esdmon_class_init,
      NULL,
      NULL,
      sizeof (GstEsdmon),
      0,
      gst_esdmon_init,
    };

    esdmon_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstEsdmon", &esdmon_info, 0);
  }
  return esdmon_type;
}

static void
gst_esdmon_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_set_details (element_class, &esdmon_details);
}

static void
gst_esdmon_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  g_object_class_install_property (gobject_class, ARG_BYTESPERREAD, g_param_spec_ulong ("bytes_per_read", "bytes_per_read", "bytes_per_read", 0, G_MAXULONG, 0, G_PARAM_READWRITE));    /* CHECKME */
  g_object_class_install_property (gobject_class, ARG_CUROFFSET, g_param_spec_ulong ("curoffset", "curoffset", "curoffset", 0, G_MAXULONG, 0, G_PARAM_READABLE));       /* CHECKME */
  g_object_class_install_property (gobject_class, ARG_DEPTH, g_param_spec_enum ("depth", "depth", "depth", GST_TYPE_ESDMON_DEPTHS, 16, G_PARAM_READWRITE));     /* CHECKME! */
  g_object_class_install_property (gobject_class, ARG_CHANNELS, g_param_spec_enum ("channels", "channels", "channels", GST_TYPE_ESDMON_CHANNELS, 2, G_PARAM_READWRITE));        /* CHECKME! */
  g_object_class_install_property (gobject_class, ARG_RATE, g_param_spec_int ("frequency", "frequency", "frequency", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));        /* CHECKME */
  g_object_class_install_property (gobject_class, ARG_HOST, g_param_spec_string ("host", "host", "host", NULL, G_PARAM_READWRITE));     /* CHECKME */

  gobject_class->set_property = gst_esdmon_set_property;
  gobject_class->get_property = gst_esdmon_get_property;

  gstelement_class->change_state = gst_esdmon_change_state;
}

static void
gst_esdmon_init (GTypeInstance * instance, gpointer g_class)
{
  GstEsdmon *esdmon = GST_ESDMON (instance);

  esdmon->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_GET_CLASS (esdmon), "src"), "src");
  gst_pad_set_get_function (esdmon->srcpad, gst_esdmon_get);
  gst_pad_use_explicit_caps (esdmon->srcpad);
  gst_element_add_pad (GST_ELEMENT (esdmon), esdmon->srcpad);

  esdmon->fd = -1;
  /* FIXME: get default from somewhere better than just putting them inline. */
  esdmon->depth = 16;
  esdmon->channels = 2;
  esdmon->frequency = 44100;
  esdmon->host = NULL;
  esdmon->bytes_per_read = 4096;
  esdmon->curoffset = 0;
  esdmon->basetime = 0;
  esdmon->samples_since_basetime = 0;
}

static gboolean
gst_esdmon_sync_parms (GstEsdmon * esdmon)
{
  g_return_val_if_fail (esdmon != NULL, FALSE);
  g_return_val_if_fail (GST_IS_ESDMON (esdmon), FALSE);

  if (esdmon->fd == -1)
    return TRUE;

  /* Need to set fd to use new parameters: only way to do this is to reopen. */
  gst_esdmon_close_audio (esdmon);
  return gst_esdmon_open_audio (esdmon);
}

static GstData *
gst_esdmon_get (GstPad * pad)
{
  GstEsdmon *esdmon;
  GstBuffer *buf;
  glong readbytes;
  glong readsamples;

  g_return_val_if_fail (pad != NULL, NULL);
  esdmon = GST_ESDMON (gst_pad_get_parent (pad));

  GST_DEBUG ("attempting to read something from esdmon");

  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);

  GST_BUFFER_DATA (buf) = (gpointer) g_malloc (esdmon->bytes_per_read);

  readbytes = read (esdmon->fd, GST_BUFFER_DATA (buf), esdmon->bytes_per_read);

  if (readbytes == 0) {
    gst_element_set_eos (GST_ELEMENT (esdmon));
    return NULL;
  }
  if (!GST_PAD_CAPS (pad)) {
    gint sign = (esdmon->depth == 8 ? FALSE : TRUE);

    /* set caps on src pad */
    if (gst_pad_set_explicit_caps (esdmon->srcpad,
            gst_caps_new_simple ("audio/x-raw-int",
                "endianness", G_TYPE_INT, G_BYTE_ORDER,
                "signed", G_TYPE_BOOLEAN, sign,
                "width", G_TYPE_INT, esdmon->depth,
                "depth", G_TYPE_INT, esdmon->depth,
                "rate", G_TYPE_INT, esdmon->frequency,
                "channels", G_TYPE_INT, esdmon->channels)) <= 0) {
      GST_ELEMENT_ERROR (esdmon, CORE, NEGOTIATION, (NULL), (NULL));
      return NULL;
    }
  }

  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = esdmon->curoffset;
  GST_BUFFER_TIMESTAMP (buf) = esdmon->basetime +
      esdmon->samples_since_basetime * GST_SECOND / esdmon->frequency;

  esdmon->curoffset += readbytes;
  readsamples = readbytes / esdmon->channels;
  if (esdmon->depth == 16)
    readsamples /= 2;
  esdmon->samples_since_basetime += readsamples;

  GST_DEBUG ("pushed buffer from esdmon of %ld bytes, timestamp %"
      G_GINT64_FORMAT, readbytes, GST_BUFFER_TIMESTAMP (buf));
  return GST_DATA (buf);
}

static void
gst_esdmon_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstEsdmon *esdmon;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_ESDMON (object));
  esdmon = GST_ESDMON (object);

  switch (prop_id) {
    case ARG_BYTESPERREAD:
      esdmon->bytes_per_read = g_value_get_ulong (value);
      /* No need to sync params - will just happen on next read. */
      break;
    case ARG_DEPTH:
      esdmon->depth = g_value_get_enum (value);
      gst_esdmon_sync_parms (esdmon);
      break;
    case ARG_CHANNELS:
      esdmon->channels = g_value_get_enum (value);
      gst_esdmon_sync_parms (esdmon);
      break;
    case ARG_RATE:
      /* Preserve the timestamps */
      esdmon->basetime =
          esdmon->samples_since_basetime * GST_SECOND / esdmon->frequency;
      esdmon->samples_since_basetime = 0;

      /* Set the new frequency */
      esdmon->frequency = g_value_get_int (value);
      gst_esdmon_sync_parms (esdmon);
      break;
    case ARG_HOST:
      if (esdmon->host != NULL)
        g_free (esdmon->host);
      if (g_value_get_string (value) == NULL)
        esdmon->host = NULL;
      else
        esdmon->host = g_strdup (g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void
gst_esdmon_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstEsdmon *esdmon;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_ESDMON (object));
  esdmon = GST_ESDMON (object);

  switch (prop_id) {
    case ARG_BYTESPERREAD:
      g_value_set_ulong (value, esdmon->bytes_per_read);
      break;
    case ARG_CUROFFSET:
      g_value_set_ulong (value, esdmon->curoffset);
      break;
    case ARG_DEPTH:
      g_value_set_enum (value, esdmon->depth);
      break;
    case ARG_CHANNELS:
      g_value_set_enum (value, esdmon->channels);
      break;
    case ARG_RATE:
      g_value_set_int (value, esdmon->frequency);
      break;
    case ARG_HOST:
      g_value_set_string (value, esdmon->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_esdmon_factory_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "esdmon", GST_RANK_NONE, GST_TYPE_ESDMON))
    return FALSE;

  return TRUE;
}

static gboolean
gst_esdmon_open_audio (GstEsdmon * src)
{
  /* Name used by esound for this connection. */
  const char *connname = "GStreamer";

  /* Bitmap describing audio format. */
  esd_format_t esdformat = ESD_STREAM | ESD_PLAY;

  g_return_val_if_fail (src->fd == -1, FALSE);

  if (src->depth == 16)
    esdformat |= ESD_BITS16;
  else if (src->depth == 8)
    esdformat |= ESD_BITS8;
  else {
    GST_DEBUG ("esdmon: invalid bit depth (%d)", src->depth);
    return FALSE;
  }

  if (src->channels == 2)
    esdformat |= ESD_STEREO;
  else if (src->channels == 1)
    esdformat |= ESD_MONO;
  else {
    GST_DEBUG ("esdmon: invalid number of channels (%d)", src->channels);
    return FALSE;
  }

  GST_DEBUG ("esdmon: attempting to open connection to esound server");
  src->fd = esd_monitor_stream (esdformat, src->frequency, src->host, connname);
  if (src->fd < 0) {
    GST_DEBUG ("esdmon: can't open connection to esound server");
    return FALSE;
  }

  GST_FLAG_SET (src, GST_ESDMON_OPEN);

  return TRUE;
}

static void
gst_esdmon_close_audio (GstEsdmon * src)
{
  if (src->fd < 0)
    return;

  close (src->fd);
  src->fd = -1;

  GST_FLAG_UNSET (src, GST_ESDMON_OPEN);

  GST_DEBUG ("esdmon: closed sound device");
}

static GstElementStateReturn
gst_esdmon_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_ESDMON (element), FALSE);

  /* if going down into NULL state, close the fd if it's open */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_ESDMON_OPEN))
      gst_esdmon_close_audio (GST_ESDMON (element));
    /* otherwise (READY or higher) we need to open the fd */
  } else {
    if (!GST_FLAG_IS_SET (element, GST_ESDMON_OPEN)) {
      if (!gst_esdmon_open_audio (GST_ESDMON (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  return GST_STATE_SUCCESS;
}
