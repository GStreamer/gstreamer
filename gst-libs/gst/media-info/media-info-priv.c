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

/* media-info-priv.c - handling of internal stuff */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>
#include "media-info.h"
#include "media-info-priv.h"


/* helper structs bits */
GstMediaInfoStream *
gmi_stream_new (void)
{
  GstMediaInfoStream *stream;

  stream = (GstMediaInfoStream *) g_malloc (sizeof (GstMediaInfoStream));

  stream->length_tracks = 0;
  stream->length_time = 0;
  stream->bitrate = 0;
  stream->seekable = FALSE;
  stream->path = NULL;
  stream->mime = NULL;
  stream->tracks = NULL;

  return stream;
}

void
gmi_stream_free (GstMediaInfoStream *stream)
{
  if (stream->mime) g_free (stream->mime);
  /* FIXME: free tracks */
  g_free (stream);
}

GstMediaInfoTrack *
gmi_track_new (void)
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

/**
 * private functions
 */

/* callbacks */
static void
have_type_callback (GstElement *typefind, GstCaps *type, GstMediaInfoPriv *priv)
{
  priv->type = type;
}

void
deep_notify_callback (GObject *object, GstObject *origin,
		      GParamSpec *pspec, GstMediaInfoPriv *priv)
{
  GValue value = { 0, };

  if (strcmp (pspec->name, "metadata") == 0)
  {
    GST_DEBUG ("DEBUG: deep_notify: have metadata !");
    g_value_init (&value, pspec->value_type);
    g_object_get_property (G_OBJECT (origin), pspec->name, &value);
    priv->metadata = g_value_peek_pointer (&value);
  }
  else if (strcmp (pspec->name, "caps") == 0)
  {
    /* check if we're getting it from the source we want it from */
    if (GST_IS_PAD (origin) && GST_PAD (origin) == priv->decoder_pad)
    {
      GST_DEBUG ("DEBUG: deep_notify: have caps on decoder_pad !");
      g_value_init (&value, pspec->value_type);
      g_object_get_property (G_OBJECT (origin), pspec->name, &value);
      priv->format = g_value_peek_pointer (&value);
    }
    else GST_DEBUG ("ignoring caps on object %s:%s",
		   gst_object_get_name (gst_object_get_parent (origin)),
		   gst_object_get_name (origin));
  }
  else if (strcmp (pspec->name, "offset") == 0)
  {
    /* we REALLY ignore offsets, we hate them */
  }
  else if (strcmp (pspec->name, "streaminfo") == 0)
  {
    GST_DEBUG ("deep_notify: have streaminfo !");
    g_value_init (&value, pspec->value_type);
    g_object_get_property (G_OBJECT (origin), pspec->name, &value);
    priv->streaminfo = g_value_peek_pointer (&value);
  }
   else GST_DEBUG ("ignoring notify of %s", pspec->name);
}

/* helpers */

/* reset info to a state where it can be used to query for media info
 * clear caps, metadata, and so on */
void
gmi_reset (GstMediaInfo *info)
{
  GstMediaInfoPriv *priv = info->priv;
  /* clear out some stuff */
  if (priv->format)
  {
    GMI_DEBUG ("unreffing priv->format, error before this ?\n");
    gst_caps_free (priv->format);
    priv->format = NULL;
  }
  if (priv->metadata)
  {
    GMI_DEBUG ("unreffing priv->metadata, error before this ?\n");
    gst_caps_free (priv->metadata);
    priv->metadata = NULL;
  }
  if (priv->stream)
  {
    GMI_DEBUG ("freeing priv->stream, error before this ?\n");
    g_free (priv->stream);
    priv->stream = NULL;
  }
  if (priv->location)
  {
    GMI_DEBUG ("freeing priv->location, error before this ?\n");
    g_free (priv->location);
    priv->location = NULL;
  }
  priv->flags = 0;
  priv->state = GST_MEDIA_INFO_STATE_NULL;
}

/* seek to a track and reset metadata and streaminfo structs */
gboolean
gmi_seek_to_track (GstMediaInfo *info, long track)
{
  GstEvent *event;
  GstFormat track_format = 0;
  GstMediaInfoPriv *priv = info->priv;
  gboolean res;

  /* FIXME: consider more nicks as "track" */
  track_format = gst_format_get_by_nick ("logical_stream");
  if (track_format == 0) return FALSE;
  GST_DEBUG ("Track format: %d", track_format);

  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
		            == GST_STATE_FAILURE)
    g_warning ("Couldn't set to play");
  g_assert (GST_IS_PAD (info->priv->decoder_pad));
  event = gst_event_new_seek (track_format |
		              GST_SEEK_METHOD_SET |
			      GST_SEEK_FLAG_FLUSH,
			      track);
  res = gst_pad_send_event (info->priv->decoder_pad, event);
  if (!res)
  {
    g_warning ("seek to logical track on pad %s:%s failed",
               GST_DEBUG_PAD_NAME(info->priv->decoder_pad));
    return FALSE;
  }
  /* clear structs because of the seek */
  if (priv->metadata)
  {
    gst_caps_free (priv->metadata);
    priv->metadata = NULL;
  }
  if (priv->streaminfo)
  {
    gst_caps_free (priv->streaminfo);
    priv->streaminfo = NULL;
  }
  return TRUE;
}

/* create a good decoder for this mime type */
/* FIXME: maybe make this more generic with a type, so it can be used
 * for taggers and other things as well */
GstElement *
gmi_get_decoder (GstMediaInfo *info, const char *mime)
{
  GstElement *decoder;
  gchar *factory = NULL;

  /* check if we have an active codec element in the hash table for this */
  decoder = g_hash_table_lookup (info->priv->decoders, mime);
  if (decoder == NULL)
  {
    GST_DEBUG ("no decoder in table, inserting one");
    /* FIXME: please figure out proper mp3 mimetypes */
    if ((strcmp (mime, "application/x-ogg") == 0) ||
        (strcmp (mime, "application/ogg") == 0))
      factory = g_strdup ("vorbisfile");
    else if ((strcmp (mime, "audio/mpeg") == 0) ||
             (strcmp (mime, "audio/x-mp3") == 0) ||
             (strcmp (mime, "audio/mp3") == 0) ||
	     (strcmp (mime, "audio/x-id3") == 0))
      factory = g_strdup ("mad");
    else if (strcmp (mime, "application/x-flac") == 0)
      factory = g_strdup ("flacdec");
    else if (strcmp (mime, "audio/x-wav") == 0)
      factory = g_strdup ("wavparse");
    else if (strcmp (mime, "audio/x-mod") == 0 ||
	     strcmp (mime, "audio/x-s3m") == 0 ||
             strcmp (mime, "audio/x-xm") == 0 ||
	     strcmp (mime, "audio/x-it") == 0)
      factory = g_strdup ("modplug");

    if (factory == NULL)
      return NULL;

    GST_DEBUG ("using factory %s", factory);
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

/* set the decoder to be used for decoding
 * install callback handlers
 */
void
gmi_set_decoder (GstMediaInfo *info, GstElement *decoder)
{
  GstMediaInfoPriv *priv = info->priv;

  /* set up pipeline and connect signal handlers */
  priv->decoder = decoder;
  gst_bin_add (GST_BIN (priv->pipeline), decoder);
  if (!gst_element_link (priv->source, decoder))
    g_warning ("Couldn't connect source and decoder\n");
  /* FIXME: we should be connecting to ALL possible src pads */
  if (!(priv->decoder_pad = gst_element_get_pad (decoder, "src")))
    g_warning ("Couldn't get decoder pad\n");
  if (!(priv->source_pad = gst_element_get_pad (priv->source, "src")))
    g_warning ("Couldn't get source pad\n");
}

/* clear the decoder (if it was set)
 */
void
gmi_clear_decoder (GstMediaInfo *info)
{
  if (info->priv->decoder)
  {
    /* clear up decoder */
	  /* FIXME: shouldn't need to set state here */
    gst_element_set_state (info->priv->pipeline, GST_STATE_READY);
    gst_element_unlink (info->priv->source, info->priv->decoder);
    gst_bin_remove (GST_BIN (info->priv->pipeline), info->priv->decoder);
    info->priv->decoder = NULL;
  }
}

/****
 *  typefind functions
 * find the type of a file and store it in the caps of the info
 * FIXME: we might better return GstCaps instead of storing them
 * internally */

/* prepare for typefind, move from NULL to TYPEFIND */
gboolean
gmip_find_type_pre (GstMediaInfoPriv *priv)
{
  /* clear vars that need clearing */
  if (priv->type)
  {
    /* we don't need to unref, this is done inside gsttypefind.c
       gst_caps_free (priv->type);
     */
    priv->type = NULL;
  }

  GST_DEBUG ("gmip_find_type_pre: start");
  /* find out type */
  /* FIXME: we could move caps for typefind out of struct and
   * just use it through this function only */

  gst_bin_add (GST_BIN (priv->pipeline), priv->typefind);
  g_object_set (G_OBJECT (priv->source), "location", priv->location, NULL);
  if (!gst_element_link (priv->source, priv->typefind))
    g_warning ("Couldn't connect source and typefind\n");
  g_signal_connect (G_OBJECT (priv->typefind), "have-type",
		    G_CALLBACK (have_type_callback), priv);
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
		            == GST_STATE_FAILURE)
  {
    g_warning ("Couldn't set to play");
    return FALSE;
  }
  priv->state = GST_MEDIA_INFO_STATE_TYPEFIND;
  return TRUE;
}

/* finish off typefind */
gboolean
gmip_find_type_post (GstMediaInfoPriv *priv)
{
  /*clear up typefind */
  gst_element_set_state (priv->pipeline, GST_STATE_READY);
  gst_element_unlink (priv->source, priv->typefind);
  gst_bin_remove (GST_BIN (priv->pipeline), priv->typefind);

  if (priv->type == NULL)
  {
    g_warning ("iteration ended, type not found !\n");
    return FALSE;
  }
  priv->state = GST_MEDIA_INFO_STATE_STREAM;
  return TRUE;
}

/* complete version */
gboolean
gmip_find_type (GstMediaInfoPriv *priv)
{
  if (!gmip_find_type_pre (priv))
    return FALSE;
  GST_DEBUG ("gmip_find_type: iterating");
  while ((priv->type == NULL) &&
	 gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG("+");
  GMI_DEBUG("\n");
  return gmip_find_type_post (priv);
}

/* FIXME: why not have these functions work on priv types ? */
gboolean
gmip_find_stream_pre (GstMediaInfoPriv *priv)
{
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
		            == GST_STATE_FAILURE)
  {
    g_warning ("Couldn't set to play");
    return FALSE;
  }
  priv->state = GST_MEDIA_INFO_STATE_STREAM;
  return TRUE;
}

gboolean
gmip_find_stream_post (GstMediaInfoPriv *priv)
{
  GstMediaInfoStream *stream = priv->stream;
  const GstFormat *formats;
  GstFormat track_format = 0;
  GstFormat format;
  gint64 value;
  gboolean res;
  glong bytes = 0;


  GST_DEBUG ("gmip_find_stream_post: start");
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
    definition = gst_format_get_details (*formats);
    GST_DEBUG ("trying to figure out length for format %s", definition->nick);

    res = gst_pad_query (priv->decoder_pad, GST_QUERY_TOTAL,
                         &format, &value);

    if (res)
    {
      switch (format)
      {
        case GST_FORMAT_TIME:
          stream->length_time = value;
          GST_DEBUG ("  total %s: %lld", definition->nick, value);
	  break;
	case GST_FORMAT_DEFAULT:
	case GST_FORMAT_BYTES:
	  break;
	default:
	  /* separation is necessary because track_format doesn't resolve to
	   * int */
	  if (format == track_format)
	  {
	    stream->length_tracks = value;
            GST_DEBUG ("  total %s: %lld", definition->nick, value);
	  }
	  else
	    GST_DEBUG ("unhandled format %s", definition->nick);
      }
    }
    else
      GST_DEBUG ("query didn't return result for %s", definition->nick);

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
  GST_DEBUG ("bitrate calc: bytes gotten: %ld", bytes);

  if (bytes)
  {
    double seconds = (double) stream->length_time / GST_SECOND;
    double bits = bytes * 8;
    stream->bitrate = (long) (bits / seconds);
  }
  priv->state = GST_MEDIA_INFO_STATE_METADATA;	/* metadata of first track */
  return TRUE;
}

/* get properties of complete physical stream
 * and return them in pre-alloced stream struct in priv->stream */
gboolean
gmip_find_stream (GstMediaInfoPriv *priv)
{
  GST_DEBUG ("mip_find_stream start");

  gmip_find_stream_pre (priv);
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
    GMI_DEBUG("gmip_find_stream: couldn't get caps !");
    return FALSE;
  }
  return gmip_find_stream_post (priv);
}

/* find metadata encoded in media and store in priv->metadata */
gboolean
gmip_find_track_metadata_pre (GstMediaInfoPriv *priv)
{
  /* FIXME: this is a hack to set max allowed iterations for metadata
   * querying - we should make gst smarter by itself instead */
  priv->metadata_iters = 0;
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
		            == GST_STATE_FAILURE)
  {
    g_warning ("Couldn't set to play");
    return FALSE;
  }
  return TRUE;
}

gboolean
gmip_find_track_metadata_post (GstMediaInfoPriv *priv)
{
  if (gst_element_set_state (priv->pipeline, GST_STATE_PAUSED)
		            == GST_STATE_FAILURE)
    return FALSE;
  priv->current_track->metadata = priv->metadata;
  priv->metadata = NULL;
  return TRUE;
}

gboolean
gmip_find_track_metadata (GstMediaInfoPriv *priv)
{
  gmip_find_track_metadata_pre (priv);
  GST_DEBUG ("gmip_find_metadata: iterating");
  while ((priv->metadata == NULL) &&
	 gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG("+");
  GMI_DEBUG("\n");
  gmip_find_track_metadata_post (priv);

  return TRUE;
}

/* find streaminfo found by decoder and store in priv->streaminfo */
/* FIXME: this is an exact copy, so reuse this functioin instead */
gboolean
gmip_find_track_streaminfo_pre (GstMediaInfoPriv *priv)
{
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
		            == GST_STATE_FAILURE)
  {
    g_warning ("Couldn't set to play");
    return FALSE;
  }
  return TRUE;
}

gboolean
gmip_find_track_streaminfo_post (GstMediaInfoPriv *priv)
{
  GstFormat format, track_format;

  gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);

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
      GST_DEBUG ("we are currently at %ld", track_num);
      res = gst_pad_convert  (priv->decoder_pad,
		              track_format, track_num,
			      &format, &value_start);
      res &= gst_pad_convert (priv->decoder_pad,
			      track_format, track_num + 1,
                              &format, &value_end);
      if (res)
      {
        /* substract to get the length */
	GST_DEBUG ("start %lld, end %lld", value_start, value_end);
	value_end -= value_start;
	/* FIXME: check units; this is in seconds */

	gst_caps_set_simple (priv->streaminfo,
	    "length", G_TYPE_INT, (int) (value_end / 1E6), NULL);
      }
    }
  }
  priv->current_track->streaminfo = priv->streaminfo;
  priv->streaminfo = NULL;

  return TRUE;
}

gboolean
gmip_find_track_streaminfo (GstMediaInfoPriv *priv)
{
  gmip_find_track_streaminfo_pre (priv);
  GST_DEBUG ("DEBUG: gmip_find_streaminfo: iterating");
  while ((priv->streaminfo == NULL) &&
	 gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG("+");
  GMI_DEBUG("\n");
  gmip_find_track_streaminfo_post (priv);

  return TRUE;
}

/* find format found by decoder and store in priv->format */
gboolean
gmip_find_track_format_pre (GstMediaInfoPriv *priv)
{
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
		            == GST_STATE_FAILURE)
  {
    g_warning ("Couldn't set to play");
    return FALSE;
  }
  return TRUE;
}

gboolean
gmip_find_track_format_post (GstMediaInfoPriv *priv)
{
  if (gst_element_set_state (priv->pipeline, GST_STATE_PAUSED)
		            == GST_STATE_FAILURE)
    return FALSE;
  priv->current_track->format = priv->format;
  priv->format = NULL;
  return TRUE;
}

gboolean
gmip_find_track_format (GstMediaInfoPriv *priv)
{
  gmip_find_track_format_pre (priv);
  GST_DEBUG ("DEBUG: gmip_find_format: iterating");
  while ((priv->format == NULL) &&
	 gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG("+");
  GMI_DEBUG("\n");
  gmip_find_track_format_post (priv);

  return TRUE;
}


