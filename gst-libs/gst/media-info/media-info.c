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

#include <gst/gst.h>
#include <string.h>
#include "media-info.h"

static gboolean _gst_media_info_debug = TRUE;
#define GMI_DEBUG(format, args...) \
  { if (_gst_media_info_debug) { g_print ( format , ## args ); }}

struct GstMediaInfoPriv
{
  GstElement *pipeline;

  GstElement *typefind;

  GstCaps *type;
  GstPad *decoder_pad;			/* pad for querying decoded caps */
  GstPad *source_pad;			/* pad for querying encoded caps */

  GstCaps *format;
  GstCaps *metadata;
  GstCaps *streaminfo;

  GstElement *decoder;			/* will be != NULL during collection */
  gchar *source_element;		/* type of element used as source */
  GstElement *source;

  GHashTable *decoders;			/* a table of decoder GstElements */

  GstMediaInfoStream *stream;		/* total stream properties */
  char *cache;				/* location of cache */
};

static void 	gst_media_info_class_init 	(GstMediaInfoClass *klass);
static void 	gst_media_info_instance_init 	(GstMediaInfo *info);

static void	gst_media_info_get_property     (GObject *object, guint prop_id,
			     			 GValue *value, GParamSpec *pspec);


static void	gst_media_info_reset 		(GstMediaInfo *info);

static void	deep_notify_callback 		(GObject *object, GstObject *origin,
		      				GParamSpec *pspec, GstMediaInfo *info);

/* helper structs bits */
static GstMediaInfoStream *
gst_media_info_stream_new (void)
{
  GstMediaInfoStream *stream;

  stream = g_malloc (sizeof (GstMediaInfoStream));

  stream->length_tracks = 0;
  stream->length_time = 0;
  stream->bitrate = 0;
  stream->seekable = FALSE;
  stream->path = NULL;
  stream->mime = NULL;
  stream->tracks = NULL;

  return stream;
}

static void
gst_media_info_stream_free (GstMediaInfoStream *stream)
{
  if (stream->mime) g_free (stream->mime);
  /* FIXME: free tracks */
  g_free (stream);
}

static GstMediaInfoTrack *
gst_media_info_track_new (void)
{
  GstMediaInfoTrack *track;

  track = g_malloc (sizeof (GstMediaInfoTrack));

  track->metadata = NULL;
  track->streaminfo = NULL;
  track->format = NULL;
  track->length_time = 0;
  track->con_streams = NULL;

  return track;
}
/* GObject-y bits */

/* signal stuff */
enum
{
  MEDIA_INFO_SIGNAL,
  LAST_SIGNAL
};

static guint gst_media_info_signals[LAST_SIGNAL] = { 0 };

/* GError stuff */
/*
enum
{
  MEDIA_INFO_ERROR_FILE
};
*/
/* GError quark stuff */
/*
static GQuark
gst_media_info_error_quark (void)
{
  static GQuark quark = 0;
  if (quark == 0)
    quark = g_quark_from_static_string ("gst-media-info-error-quark");
  return quark;
}
*/
/*
 * GObject type stuff 
 */

enum
{
  PROP_SOURCE
};

static GObjectClass *parent_class = NULL;

GType
gst_media_info_get_type (void)
{
  static GType gst_media_info_type = 0;
  if (!gst_media_info_type)
  {
    static const GTypeInfo gst_media_info_info = {
      sizeof (GstMediaInfoClass),
      (GBaseInitFunc) NULL,
      (GBaseFinalizeFunc) NULL,
      (GClassInitFunc) gst_media_info_class_init,
      NULL, NULL,
      sizeof (GstMediaInfo),
      0,
      (GInstanceInitFunc) gst_media_info_instance_init,
      NULL
    };
    gst_media_info_type = g_type_register_static (G_TYPE_OBJECT, 
		                                  "GstMediaInfo",
		                                  &gst_media_info_info, 0); 
  }
  return gst_media_info_type;
}

static void
gst_media_info_class_init (GstMediaInfoClass *klass)
{
  GObjectClass *g_object_class = G_OBJECT_CLASS (klass);
  
  parent_class = g_type_class_ref (G_TYPE_OBJECT);
  //parent_class = g_type_class_peek_parent (klass);


  /*
  object_class->finalize = gst_media_info_finalize;
  object_class->dispose  = gst_media_info_dispose;
  */

  /*
  g_object_class->set_property = gst_media_info_set_property;
  */
  g_object_class->get_property = gst_media_info_get_property;

  klass->media_info_signal = NULL;

  gst_media_info_signals [MEDIA_INFO_SIGNAL] =
    g_signal_new ("media-info",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (GstMediaInfoClass, media_info_signal),
		  NULL, NULL,
		  gst_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
}

static void
gst_media_info_instance_init (GstMediaInfo *info)
{
  GstElement *source;

  info->priv = g_new0 (GstMediaInfoPriv, 1);

  info->priv->pipeline = gst_pipeline_new ("media-info");

  info->priv->typefind = gst_element_factory_make ("typefind", "typefind");
  if (!GST_IS_ELEMENT (info->priv->typefind))
	  /* FIXME */
    g_error ("Cannot create typefind element !");

  /* ref it so it never goes away on removal out of bins */
  gst_object_ref (GST_OBJECT (info->priv->typefind));

  /* use gnomevfssrc by default */
  source = gst_element_factory_make ("gnomevfssrc", "source");
  if (GST_IS_ELEMENT (source))
  {
    info->priv->source = source;
    info->priv->source_element = g_strdup ("gnomevfssrc");
    gst_bin_add (GST_BIN (info->priv->pipeline), info->priv->source);
  }
  else
  {
    info->priv->source = NULL;
    info->priv->source_element = NULL;
  }
  info->priv->decoder = NULL;
  info->priv->type = NULL;
  info->priv->format = NULL;
  info->priv->metadata = NULL;

  /* clear result pointers */
  info->priv->stream = NULL;

  /* set up decoder hash table */
  info->priv->decoders = g_hash_table_new (g_str_hash, g_str_equal);

  /* attach notify handler */
  g_signal_connect (G_OBJECT (info->priv->pipeline), "deep_notify",
		    G_CALLBACK (deep_notify_callback), info);
}

/* get/set */
static void
gst_media_info_get_property (GObject *object, guint prop_id,
			     GValue *value, GParamSpec *pspec)
{
  GstMediaInfo *info = GST_MEDIA_INFO (object);

  switch (prop_id)
  {
    case PROP_SOURCE:
      g_value_set_string (value, info->priv->source_element);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

GstMediaInfo *
gst_media_info_new (const gchar *source_element)
{
  GstMediaInfo *info = g_object_new (GST_MEDIA_INFO_TYPE, NULL);
  if (source_element)
    g_object_set (G_OBJECT (info), "source", source_element);

  return info;
}

/**
 * private functions 
 */

/* callbacks */
static void
have_type_callback (GstElement *typefind, GstCaps *type, GstMediaInfo *info)
{
  info->priv->type = type;
}

static void
deep_notify_callback (GObject *object, GstObject *origin,
		      GParamSpec *pspec, GstMediaInfo *info)
{
  GValue value = { 0, };

  if (strcmp (pspec->name, "metadata") == 0)
  {
    GMI_DEBUG("DEBUG: deep_notify: have metadata !\n");
    g_value_init (&value, pspec->value_type);
    g_object_get_property (G_OBJECT (origin), pspec->name, &value);
    info->priv->metadata = g_value_peek_pointer (&value);
  }
  else if (strcmp (pspec->name, "caps") == 0)
  {
    /* check if we're getting it from the source we want it from */
    if (GST_IS_PAD (origin) && GST_PAD (origin) == info->priv->decoder_pad)
    {
      GMI_DEBUG("DEBUG: deep_notify: have caps on decoder_pad !\n");
      g_value_init (&value, pspec->value_type);
      g_object_get_property (G_OBJECT (origin), pspec->name, &value);
      info->priv->format = g_value_peek_pointer (&value);
    }
    else GMI_DEBUG("DEBUG: igoring caps on object %s:%s\n",
		   gst_object_get_name (gst_object_get_parent (origin)),
		   gst_object_get_name (origin));
  }
  else if (strcmp (pspec->name, "offset") == 0)
  {
    /* we REALLY ignore offsets, we hate them */
  }
  else if (strcmp (pspec->name, "streaminfo") == 0)
  {
    GMI_DEBUG("DEBUG: deep_notify: have streaminfo !\n");
    g_value_init (&value, pspec->value_type);
    g_object_get_property (G_OBJECT (origin), pspec->name, &value);
    info->priv->streaminfo = g_value_peek_pointer (&value);
  }
   else GMI_DEBUG("DEBUG: ignoring notify of %s\n", pspec->name);
}

/* helpers */

/* reset info to a state where it can be used to query for media info
 * clear caps, metadata, and so on */
static void
gst_media_info_reset (GstMediaInfo *info)
{
  GstMediaInfoPriv *priv = info->priv;
  /* clear out some stuff */
  if (priv->format)
  {
    gst_caps_unref (priv->format);
    priv->format = NULL;
  }
  if (priv->metadata)
  {
    gst_caps_unref (priv->metadata);
    priv->metadata = NULL;
  }
  if (priv->stream)
  {
    g_free (priv->stream);
    priv->stream = NULL;
  }
}

/* seek to a track and reset metadata and streaminfo structs */
static gboolean
gst_media_info_seek_to_track (GstMediaInfo *info, long track)
{
  GstEvent *event;
  GstFormat track_format = 0;
  GstMediaInfoPriv *priv = info->priv;
  gboolean res;

  /* FIXME: consider more nicks as "track" */
  track_format = gst_format_get_by_nick ("logical_stream");
  if (track_format == 0) return FALSE;
  GMI_DEBUG("Track format: %d\n", track_format);
  
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING) 
		            == GST_STATE_FAILURE)
    g_warning ("Couldn't set to play");
  g_assert (GST_IS_PAD (info->priv->decoder_pad));
  event = gst_event_new_seek (track_format |
		              GST_SEEK_METHOD_SET |
			      GST_SEEK_FLAG_FLUSH,
			      track);
  res = gst_pad_send_event (info->priv->decoder_pad, event);
  g_assert (res);
  if (!res) 
  {
    g_warning ("seek to logical track failed");
    return FALSE;
  }
  /* clear structs because of the seek */
  if (priv->metadata)
  {
    gst_caps_unref (priv->metadata);
    priv->metadata = NULL;
  }
  if (priv->streaminfo)
  {
    gst_caps_unref (priv->streaminfo);
    priv->streaminfo = NULL;
  }
  return TRUE;
}

/* create a good decoder for this mime type */
/* FIXME: maybe make this more generic with a type, so it can be used
 * for taggers and other things as well */
GstElement *
gst_media_info_get_decoder (GstMediaInfo *info, const char *mime)
{
  GstElement *decoder;
  gchar *factory = NULL;

  /* check if we have an active codec element in the hash table for this */
  decoder = g_hash_table_lookup (info->priv->decoders, mime);
  if (decoder == NULL)
  {
    GMI_DEBUG("DEBUG: no decoder in table, inserting one\n");
    if (strcmp (mime, "application/x-ogg") == 0)
      factory = g_strdup ("vorbisfile");
    else if (strcmp (mime, "audio/x-mp3") == 0)
      factory = g_strdup ("mad");
    else if (strcmp (mime, "audio/x-wav") == 0)
      factory = g_strdup ("wavparse");

    if (factory == NULL)
      return NULL;

    GMI_DEBUG("DEBUG: using factory %s\n", factory);
    decoder = gst_element_factory_make (factory, "decoder");
    g_free (factory);

    if (decoder)
    {
      g_hash_table_insert (info->priv->decoders, g_strdup (mime), decoder);
      /* ref it so we don't lose it when removing from bin */
      g_object_ref (GST_OBJECT (decoder));
    }
  }
 
  return decoder;
}

/* find the type of a file and store it in the caps of the info
 * FIXME: we might better return GstCaps instead of storing them
 * internally */
static void
gst_media_info_find_type (GstMediaInfo *info, const char *location)
{
  GstMediaInfoPriv *priv = info->priv;

  /* clear vars that need clearing */
  if (priv->type)
  {
    gst_caps_unref (priv->type);
    priv->type = NULL;
  }

  GMI_DEBUG("DEBUG: gst_media_info_find_type: start\n");
  /* find out type */
  /* FIXME: we could move caps for typefind out of struct and
   * just use it through this function only */

  gst_bin_add (GST_BIN (priv->pipeline), priv->typefind);
  g_object_set (G_OBJECT (priv->source), "location", location, NULL);
  if (!gst_element_connect (priv->source, priv->typefind))
    g_warning ("Couldn't connect source and typefind\n");
  g_signal_connect (G_OBJECT (priv->typefind), "have-type",
		    G_CALLBACK (have_type_callback), info);
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING) 
		            == GST_STATE_FAILURE)
    g_warning ("Couldn't set to play");
  GMI_DEBUG("DEBUG: gst_media_info_find_type: iterating\n");
  while ((priv->type == NULL) && 
	 gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG("+");
  GMI_DEBUG("\n");

  /*clear up typefind */
  gst_element_set_state (priv->pipeline, GST_STATE_READY);
  gst_element_disconnect (priv->source, priv->typefind);
  gst_bin_remove (GST_BIN (priv->pipeline), priv->typefind);
}
/* get properties of complete physical stream 
 * and return them in pre-alloced stream struct */
static gboolean
gst_media_info_get_stream (GstMediaInfo *info, GstMediaInfoStream *stream)
{
  GstMediaInfoPriv *priv = info->priv;
  const GstFormat *formats;
  GstFormat track_format = 0;
  GstFormat format;
  gint64 value;
  gboolean res;
  glong bytes = 0;

  GMI_DEBUG("DEBUG:gst_media_info_get_stream start\n");

  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING) 
		            == GST_STATE_FAILURE)
    g_warning ("Couldn't set to play");
  /* iterate until caps are found */
  /* FIXME: this should be done through the plugin sending some signal
   * that it is ready for queries */
  while (gst_bin_iterate (GST_BIN (priv->pipeline)) &&
	 priv->format == NULL)
    ;
  if (gst_element_set_state (priv->pipeline, GST_STATE_PAUSED) 
		            == GST_STATE_FAILURE)
    g_warning ("Couldn't set to paused");
  if (priv->format == NULL)
  {
    GMI_DEBUG("DEBUG: gst_media_info_get_stream: couldn't get caps !");
    return FALSE;
  }

  /* find a format that matches the "track" concept */
  /* FIXME: this is used in vorbis, but we might have to loop when
   * more codecs have tracks */
  track_format = gst_format_get_by_nick ("logical_stream");

  /* get supported formats on decoder pad */
  formats = gst_pad_get_formats (priv->decoder_pad);
  while (*formats)
  {
    const GstFormatDefinition *definition;

    format = *formats;

    g_assert (GST_IS_PAD (priv->decoder_pad));
    res = gst_pad_query (priv->decoder_pad, GST_QUERY_TOTAL,
                         &format, &value);

    definition = gst_format_get_details (*formats);
    GMI_DEBUG("trying to figure out length for format %s\n", definition->nick);

    if (res)
    {
      switch (format)
      {
        case GST_FORMAT_TIME:
          stream->length_time = value;
          g_print ("  total %s: %lld\n", definition->nick, value);
	  break;
	default:
	  /* separation is necessary because track_format doesn't resolve to
	   * int */
	  if (format == track_format)
	  {
	    stream->length_tracks = value;
            g_print ("  total %s: %lld\n", definition->nick, value);
	  }
	  else
	    g_print ("warning: unhandled format %s\n", definition->nick);
      }
    }
    else
      GMI_DEBUG("query didn't return result for %s\n", definition->nick);

    formats++;
  }
  if (stream->length_tracks == 0) stream->length_tracks = 1;
  /* now get number of bytes from the sink pad to get the bitrate */
  format = GST_FORMAT_BYTES;
  g_assert (GST_IS_PAD (priv->source_pad));
  res = gst_pad_query (priv->source_pad, GST_QUERY_TOTAL,
                       &format, &value);
  if (!res) g_warning ("Failed to query on sink pad !");
  bytes = value;

  if (bytes)
  {
    double seconds = stream->length_time / GST_SECOND;
    double bits = bytes * 8;
    stream->bitrate = (long) (bits / seconds);
  }
  return TRUE;
}

/* find metadata encoded in media */
GstCaps *
gst_media_info_find_metadata (GstMediaInfo *info)
{
  GstMediaInfoPriv *priv = info->priv;
  GstCaps *metadata;

  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING) 
		            == GST_STATE_FAILURE)
    g_warning ("Couldn't set to play");
  GMI_DEBUG("DEBUG: gst_media_info_find_metadata: iterating\n");
  while ((priv->metadata == NULL) && 
	 gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG("+");
  GMI_DEBUG("\n");
  gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);
  metadata = priv->metadata;
  priv->metadata = NULL;
  return metadata;
}

/* find streaminfo found by decoder */
GstCaps *
gst_media_info_find_streaminfo (GstMediaInfo *info)
{
  GstMediaInfoPriv *priv = info->priv;
  GstCaps *streaminfo;
  GstFormat format, track_format;

  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING) 
		            == GST_STATE_FAILURE)
    g_warning ("Couldn't set to play");
  GMI_DEBUG("DEBUG: gst_media_info_find_streaminfo: iterating\n");
  while ((priv->streaminfo == NULL) && 
	 gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG("+");
  GMI_DEBUG("\n");
  gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);
  streaminfo = priv->streaminfo;
  priv->streaminfo = NULL;

  /* now add total length to this, and maybe even bitrate ? FIXME */
  track_format = gst_format_get_by_nick ("logical_stream");
  if (track_format == 0) 
  {
    g_print ("FIXME: implement getting length of whole track\n");
  }
  else
  {
    /* which one are we at ? */
    long track_num;
    gint64 value_start, value_end;
    gboolean res;

    res = gst_pad_query (priv->decoder_pad, GST_QUERY_POSITION,
		         &track_format, &value_start);
    if (res)
    {
      format = GST_FORMAT_TIME;
      track_num = value_start;
      GMI_DEBUG("DEBUG: we are currently at %ld\n", track_num);
      res = gst_pad_convert  (priv->decoder_pad,
		              track_format, track_num,
			      &format, &value_start);
      res &= gst_pad_convert (priv->decoder_pad,
			      track_format, track_num + 1,
                              &format, &value_end);
      if (res) 
      {
	GstPropsEntry *length;
        /* substract to get the length */
	GMI_DEBUG("DEBUG: start %lld, end %lld\n", value_start, value_end);
	value_end -= value_start;
	g_print ("DEBUG: length: %d\n", (int) value_end);
	length = gst_props_entry_new ("length", GST_PROPS_INT ((int) value_end));
	gst_props_add_entry (gst_caps_get_props (streaminfo), length);
      }
    }
  }
  
  return streaminfo;
}

/* find format found by decoder */
GstCaps *
gst_media_info_find_format (GstMediaInfo *info)
{
  GstMediaInfoPriv *priv = info->priv;
  GstCaps *format;

  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING) 
		            == GST_STATE_FAILURE)
    g_warning ("Couldn't set to play");
  GMI_DEBUG("DEBUG: gst_media_info_find_format: iterating\n");
  while ((priv->format == NULL) && 
	 gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG("+");
  GMI_DEBUG("\n");
  gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);
  format = priv->format;
  priv->format = NULL;
  return format;
}

/* clear the decoder
 * (if it was set)
 */
static void
gst_media_info_clear_decoder (GstMediaInfo *info)
{
  if (info->priv->decoder)
  {
    /* clear up decoder */
	  /* FIXME: shouldn't need to set state here */
    gst_element_set_state (info->priv->pipeline, GST_STATE_READY);
    gst_element_disconnect (info->priv->source, info->priv->decoder);
    gst_bin_remove (GST_BIN (info->priv->pipeline), info->priv->decoder);
    info->priv->decoder = NULL;
  }
}

/* set the decoder to be used for decoding
 * install callback handlers
 */

static void
gst_media_info_set_decoder (GstMediaInfo *info, GstElement *decoder)
{
  GstMediaInfoPriv *priv = info->priv;

  /* set up pipeline and connect signal handlers */
  priv->decoder = decoder;
  gst_bin_add (GST_BIN (priv->pipeline), decoder);
  if (!gst_element_connect (priv->source, decoder))
    g_warning ("Couldn't connect source and decoder\n");
  /* FIXME: we should be connecting to ALL possible src pads */
  if (!(priv->decoder_pad = gst_element_get_pad (decoder, "src")))
    g_warning ("Couldn't get decoder pad\n");
  if (!(priv->source_pad = gst_element_get_pad (priv->source, "src")))
    g_warning ("Couldn't get source pad\n");
}

/**
 * public methods 
 */
gboolean
gst_media_info_set_source (GstMediaInfo *info, const char *source)
{
  GstElement *src;
  src = gst_element_factory_make (source, "new-source");
  if  (!GST_IS_ELEMENT (src))
    return FALSE;

  if (info->priv->source)
  {
    /* this also unrefs the element */
    gst_bin_remove (GST_BIN (info->priv->pipeline), info->priv->source);
    if (info->priv->source_element)
    {
      g_free (info->priv->source_element);
      info->priv->source_element = NULL;
    }
  }
  g_object_set (G_OBJECT (src), "name", "source", NULL);
  gst_bin_add (GST_BIN (info->priv->pipeline), src);
  info->priv->source = src;
  info->priv->source_element = g_strdup (source);

  return TRUE;
}

GstMediaInfoStream *
gst_media_info_read (GstMediaInfo *info, const char *location, guint16 flags)
{
  GstMediaInfoPriv *priv = info->priv;
  GstElement *decoder = NULL;
  const gchar *mime;
  GstMediaInfoStream *stream = NULL;
  int i;
  
  GMI_DEBUG("DEBUG: gst_media_info_read: start\n");
  gst_media_info_reset (info);		/* reset all structs */
  /* find out type */
  /* FIXME: we could move caps for typefind out of struct and
   * just use it through this function only */
  gst_media_info_find_type (info, location);

  if (priv->type == NULL)
  {
    /* iteration ended, still don't have type, ugh */
    g_warning ("iteration ended, type not found !\n");
    return NULL;
  }
  stream = gst_media_info_stream_new ();
  mime = gst_caps_get_mime (priv->type);
  if (flags & GST_MEDIA_INFO_MIME)
    stream->mime = g_strdup (mime);
  stream->path = g_strdup (location);
  GMI_DEBUG("mime type: %s\n", mime);
  decoder = gst_media_info_get_decoder (info, mime);

 /* if it's NULL, then that's a sign we can't decode it */
  if (decoder == NULL)
  {
    g_warning ("Can't find a decoder for type %s\n", mime);
    gst_media_info_stream_free (stream);
    return NULL;
  }
  /* install this decoder in the pipeline */
  gst_media_info_set_decoder (info, decoder);

  /* collect total stream properties */
  gst_media_info_get_stream (info, stream);

  /* if we have multiple tracks, loop over them; if not, just get
   * metadata and return it */
  GMI_DEBUG("DEBUG: num tracks %ld\n", stream->length_tracks);
  for (i = 0; i < stream->length_tracks; ++i)
  {
    GstMediaInfoTrack *track = gst_media_info_track_new ();
    if (i > 0)
    {
      GMI_DEBUG("seeking to track %d\n", i);
      gst_media_info_seek_to_track (info, i);
    }
    if (flags & GST_MEDIA_INFO_METADATA)
      track->metadata = gst_media_info_find_metadata (info);
    if (flags & GST_MEDIA_INFO_STREAMINFO)
      track->streaminfo = gst_media_info_find_streaminfo (info);
    if (flags & GST_MEDIA_INFO_FORMAT)
      track->format = gst_media_info_find_format (info);
    stream->tracks = g_list_append (stream->tracks, track);
  }

  gst_media_info_clear_decoder (info);
  /* please return it */
  return stream;
}


/*
 * FIXME: reset ?
gboolean	gst_media_info_write	(GstMediaInfo *media_info,
                                         const char *location,
					 GstCaps *media_info);
					 */
					 
