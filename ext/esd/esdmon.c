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

#include <gst/gst.h>
#include <esd.h>

#define GST_TYPE_ESDMON \
  (gst_esdmon_get_type())
#define GST_ESDMON(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ESDMON,GstEsdmon))
#define GST_ESDMON_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ESDMON,GstEsdmon))
#define GST_IS_ESDMON(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ESDMON))
#define GST_IS_ESDMON_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ESDMON))

typedef enum {
  GST_ESDMON_OPEN            = GST_ELEMENT_FLAG_LAST,
  GST_ESDMON_FLAG_LAST       = GST_ELEMENT_FLAG_LAST+2,
} GstEsdSrcFlags;

typedef struct _GstEsdmon GstEsdmon;
typedef struct _GstEsdmonClass GstEsdmonClass;

struct _GstEsdmon {
  GstElement element;

  GstPad *srcpad;

  gchar* host;

  int fd;

  gint depth;
  gint channels;
  gint frequency;

  guint64 basetime;
  guint64 samples_since_basetime;
  guint64 curoffset;
  guint64 bytes_per_read;
};

struct _GstEsdmonClass {
  GstElementClass parent_class;
};

GType gst_esdmon_get_type(void);

/* elementfactory information */
static GstElementDetails esdmon_details = {
  "Esound audio monitor",
  "Src/Esdmon",
  "Monitors audio from an esound server",
  VERSION,
  "Richard Boulton <richard-gst@tartarus.org>",
  "(C) 2002",
};

/* Signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_DEPTH,
  ARG_BYTESPERREAD,
  ARG_CUROFFSET,
  ARG_CHANNELS,
  ARG_RATE,
  ARG_HOST,
};

GST_PADTEMPLATE_FACTORY (src_factory,
  "src",				/* the name of the pads */
  GST_PAD_SRC,				/* type of the pad */
  GST_PAD_ALWAYS,			/* ALWAYS/SOMETIMES */
  GST_CAPS_NEW (
    "esdmon_src8",				/* the name of the caps */
    "audio/raw",				/* the mime type of the caps */
      /* Properties follow: */
      "format",       GST_PROPS_STRING ("int"),
        "law",        GST_PROPS_INT (0),
        "endianness", GST_PROPS_INT (G_BYTE_ORDER),
        "signed",     GST_PROPS_BOOLEAN (TRUE),
        "width",      GST_PROPS_INT (8),
	"depth",      GST_PROPS_INT (8),
	"rate",       GST_PROPS_INT_RANGE (8000, 96000),
     	"channels",   GST_PROPS_LIST (GST_PROPS_INT (1), GST_PROPS_INT (2))
  ),
  GST_CAPS_NEW (
    "esdmon_src16",				/* the name of the caps */
    "audio/raw",				/* the mime type of the caps */
      /* Properties follow: */
      "format",       GST_PROPS_STRING ("int"),
        "law",        GST_PROPS_INT (0),
        "endianness", GST_PROPS_INT (G_BYTE_ORDER),
        "signed",     GST_PROPS_BOOLEAN (TRUE),
        "width",      GST_PROPS_INT (16),
	"depth",      GST_PROPS_INT (16),
	"rate",       GST_PROPS_INT_RANGE (8000, 96000),
     	"channels",   GST_PROPS_LIST (GST_PROPS_INT (1), GST_PROPS_INT (2))
  )
);

static void			gst_esdmon_class_init	(GstEsdmonClass *klass);
static void			gst_esdmon_init		(GstEsdmon *esdmon);

static gboolean			gst_esdmon_open_audio	(GstEsdmon *src);
static void			gst_esdmon_close_audio	(GstEsdmon *src);
static GstElementStateReturn	gst_esdmon_change_state	(GstElement *element);
static gboolean			gst_esdmon_sync_parms	(GstEsdmon *esdmon);

static GstBuffer *		gst_esdmon_get		(GstPad *pad);

static void			gst_esdmon_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void			gst_esdmon_get_property	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

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
    esdmon_depths_type = g_enum_register_static("GstEsdmonDepths", esdmon_depths);
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
    esdmon_channels_type = g_enum_register_static("GstEsdmonChannels", esdmon_channels);
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
      sizeof(GstEsdmonClass),      NULL,
      NULL,
      (GClassInitFunc)gst_esdmon_class_init,
      NULL,
      NULL,
      sizeof(GstEsdmon),
      0,
      (GInstanceInitFunc)gst_esdmon_init,
    };
    esdmon_type = g_type_register_static(GST_TYPE_ELEMENT, "GstEsdmon", &esdmon_info, 0);
  }
  return esdmon_type;
}

static void
gst_esdmon_class_init (GstEsdmonClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BYTESPERREAD,
    g_param_spec_ulong("bytes_per_read","bytes_per_read","bytes_per_read",
                       0,G_MAXULONG,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CUROFFSET,
    g_param_spec_ulong("curoffset","curoffset","curoffset",
                       0,G_MAXULONG,0,G_PARAM_READABLE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEPTH,
    g_param_spec_enum("depth","depth","depth",
                      GST_TYPE_ESDMON_DEPTHS,16,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHANNELS,
    g_param_spec_enum("channels","channels","channels",
                      GST_TYPE_ESDMON_CHANNELS,2,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_RATE,
    g_param_spec_int("frequency","frequency","frequency",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HOST,
    g_param_spec_string("host","host","host",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_esdmon_set_property;
  gobject_class->get_property = gst_esdmon_get_property;

  gstelement_class->change_state = gst_esdmon_change_state;
}

static void
gst_esdmon_init(GstEsdmon *esdmon)
{
  esdmon->srcpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (src_factory), "src");
  gst_pad_set_get_function(esdmon->srcpad, gst_esdmon_get);
  gst_element_add_pad(GST_ELEMENT(esdmon), esdmon->srcpad);

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
gst_esdmon_sync_parms (GstEsdmon *esdmon)
{
  g_return_val_if_fail (esdmon != NULL, FALSE);
  g_return_val_if_fail (GST_IS_ESDMON (esdmon), FALSE);

  if (esdmon->fd == -1) return TRUE;

  /* Need to set fd to use new parameters: only way to do this is to reopen. */
  gst_esdmon_close_audio (esdmon);
  return gst_esdmon_open_audio (esdmon);
}

static GstBuffer *
gst_esdmon_get (GstPad *pad)
{
  GstEsdmon *esdmon;
  GstBuffer *buf;
  glong readbytes;
  glong readsamples;

  g_return_val_if_fail (pad != NULL, NULL);
  esdmon = GST_ESDMON(gst_pad_get_parent (pad));

  GST_DEBUG (GST_CAT_PLUGIN_INFO, "attempting to read something from esdmon");

  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);

  GST_BUFFER_DATA (buf) = (gpointer)g_malloc (esdmon->bytes_per_read);

  readbytes = read (esdmon->fd,
		    GST_BUFFER_DATA (buf),
                    esdmon->bytes_per_read);

  if (readbytes == 0) {
      gst_element_set_eos (GST_ELEMENT (esdmon));
      return NULL;
  }
  if (!GST_PAD_CAPS (pad)) {
    /* set caps on src pad */
    if (!gst_pad_try_set_caps (esdmon->srcpad,
                    GST_CAPS_NEW (
                      "oss_src",
                      "audio/raw",
                        "format",       GST_PROPS_STRING ("int"),
                          "law",        GST_PROPS_INT (0),              /*FIXME */
                          "endianness", GST_PROPS_INT (G_BYTE_ORDER),   /*FIXME */
                          "signed",     GST_PROPS_BOOLEAN (TRUE),       /*FIXME */
                          "width",      GST_PROPS_INT (esdmon->depth),
                          "depth",      GST_PROPS_INT (esdmon->depth),
                          "rate",       GST_PROPS_INT (esdmon->frequency),
                          "channels",   GST_PROPS_INT (esdmon->channels)
                   )))
    {
      gst_element_error (GST_ELEMENT (esdmon), "could not set caps");
      return NULL;
    }
  }

  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = esdmon->curoffset;
  GST_BUFFER_TIMESTAMP (buf) = esdmon->basetime +
	  esdmon->samples_since_basetime * 1000000LL / esdmon->frequency;

  esdmon->curoffset += readbytes;
  readsamples = readbytes / esdmon->channels;
  if (esdmon->depth == 16) readsamples /= 2;
  esdmon->samples_since_basetime += readsamples;

  GST_DEBUG (GST_CAT_PLUGIN_INFO, "pushed buffer from esdmon of %ld bytes, timestamp %lld", readbytes, GST_BUFFER_TIMESTAMP (buf));
  return buf;
}

static void
gst_esdmon_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstEsdmon *esdmon;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ESDMON(object));
  esdmon = GST_ESDMON(object);

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
      esdmon->basetime = esdmon->samples_since_basetime * 1000000LL / esdmon->frequency;
      esdmon->samples_since_basetime = 0;

      /* Set the new frequency */
      esdmon->frequency = g_value_get_int (value);
      gst_esdmon_sync_parms (esdmon);
      break;
    case ARG_HOST:
      if (esdmon->host != NULL) g_free(esdmon->host);
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
gst_esdmon_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstEsdmon *esdmon;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ESDMON(object));
  esdmon = GST_ESDMON(object);

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

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new("esdmon", GST_TYPE_ESDMON,
				   &esdmon_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate(factory, GST_PADTEMPLATE_GET (src_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "esdmon",
  plugin_init
};

static gboolean
gst_esdmon_open_audio (GstEsdmon *src)
{
  /* Name used by esound for this connection. */
  const char * connname = "GStreamer";

  /* Bitmap describing audio format. */
  esd_format_t esdformat = ESD_STREAM | ESD_PLAY;

  g_return_val_if_fail (src->fd == -1, FALSE);

  if (src->depth == 16) esdformat |= ESD_BITS16;
  else if (src->depth == 8) esdformat |= ESD_BITS8;
  else {
    GST_DEBUG (0, "esdmon: invalid bit depth (%d)", src->depth);
    return FALSE;
  }

  if (src->channels == 2) esdformat |= ESD_STEREO;
  else if (src->channels == 1) esdformat |= ESD_MONO;
  else {
    GST_DEBUG (0, "esdmon: invalid number of channels (%d)", src->channels);
    return FALSE;
  }

  GST_DEBUG (0, "esdmon: attempting to open connection to esound server");
  src->fd = esd_monitor_stream(esdformat, src->frequency, src->host, connname);
  if ( src->fd < 0 ) {
    GST_DEBUG (0, "esdmon: can't open connection to esound server");
    return FALSE;
  }

  GST_FLAG_SET (src, GST_ESDMON_OPEN);

  return TRUE;
}

static void
gst_esdmon_close_audio (GstEsdmon *src)
{
  if (src->fd < 0) return;

  close(src->fd);
  src->fd = -1;

  GST_FLAG_UNSET (src, GST_ESDMON_OPEN);

  GST_DEBUG (0, "esdmon: closed sound device");
}

static GstElementStateReturn
gst_esdmon_change_state (GstElement *element)
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

