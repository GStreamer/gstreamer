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
gmi_stream_free (GstMediaInfoStream * stream)
{
  if (stream->mime)
    g_free (stream->mime);
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
have_type_callback (GstElement * typefind, guint probability, GstCaps * type,
    GstMediaInfoPriv * priv)
{
  GstStructure *str;
  const gchar *mime;

  priv->type = gst_caps_copy (type);
  str = gst_caps_get_structure (type, 0);
  mime = gst_structure_get_name (str);
  GST_DEBUG ("caps %p, mime %s", type, mime);

  /* FIXME: this code doesn't yet work, test it later */
#ifdef DONTWORK
  if (strcmp (mime, "application/x-id3") == 0) {
    /* dig a little deeper */
    GST_DEBUG ("dealing with id3, digging deeper");
    gst_element_set_state (priv->pipeline, GST_STATE_READY);
    gst_element_unlink (priv->source, priv->typefind);
    g_assert (priv->decontainer == NULL);
    priv->decontainer = gst_element_factory_make ("id3tag", "decontainer");
    gst_bin_add (GST_BIN (priv->pipeline), priv->decontainer);
    if (priv->decontainer == NULL)
      /* FIXME: signal error */
      g_warning ("Couldn't create id3tag");
    if (!gst_element_link_many (priv->source, priv->decontainer, priv->typefind,
            NULL));
    g_warning ("Couldn't link in id3tag");

    if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
        == GST_STATE_FAILURE)
      g_warning ("Couldn't set to playing");
  }
#endif
}

void
deep_notify_callback (GObject * object, GstObject * origin,
    GParamSpec * pspec, GstMediaInfoPriv * priv)
{
  GValue value = { 0, };

  /* we only care about pad notifies */
  if (!GST_IS_PAD (origin))
    return;

  /*
     GST_DEBUG ("DEBUG: deep_notify: have notify of %s from object %s:%s !",
     pspec->name, gst_element_get_name (gst_pad_get_parent (GST_PAD (origin))),
     gst_object_get_name (origin));
   */
  else if (strcmp (pspec->name, "caps") == 0) {
    /* check if we're getting it from fakesink */
    if (GST_IS_PAD (origin) && GST_PAD_PARENT (origin) == priv->fakesink) {
      GST_DEBUG ("have caps on fakesink pad !");
      g_value_init (&value, pspec->value_type);
      g_object_get_property (G_OBJECT (origin), pspec->name, &value);
      priv->format = g_value_peek_pointer (&value);
      GST_DEBUG ("caps: %" GST_PTR_FORMAT, priv->format);
    } else
      GST_DEBUG ("ignoring caps on object %s:%s",
          gst_object_get_name (gst_object_get_parent (origin)),
          gst_object_get_name (origin));
  } else if (strcmp (pspec->name, "offset") == 0) {
    /* we REALLY ignore offsets, we hate them */
  }
  //else GST_DEBUG ("ignoring notify of %s", pspec->name);
}

typedef struct
{
  guint meta;
  guint encoded;
}
TagFlagScore;

static void
tag_flag_score (const GstTagList * list, const gchar * tag, gpointer user_data)
{
  TagFlagScore *score = (TagFlagScore *) user_data;
  GstTagFlag flag;

  flag = gst_tag_get_flag (tag);
  if (flag == GST_TAG_FLAG_META)
    score->meta++;
  if (flag == GST_TAG_FLAG_ENCODED)
    score->encoded++;
}

void
found_tag_callback (GObject * pipeline, GstElement * source, GstTagList * tags,
    GstMediaInfoPriv * priv)
{
  TagFlagScore score;

  score.meta = 0;
  score.encoded = 0;
  GST_DEBUG ("element %s found tag", GST_STR_NULL (GST_ELEMENT_NAME (source)));

  /* decide if it's likely to be metadata or streaminfo */
  /* FIXME: this is a hack, there must be a better way,
     but as long as elements can report both mixed we need to do this */

  gst_tag_list_foreach (tags, tag_flag_score, &score);

  if (score.meta > score.encoded) {
    GST_DEBUG ("found tags from decoder, adding them as metadata");
    priv->metadata = gst_tag_list_copy (tags);
  } else {
    GST_DEBUG ("found tags, adding them as streaminfo");
    priv->streaminfo = gst_tag_list_copy (tags);
  }
}

void
error_callback (GObject * element, GstElement * source, GError * error,
    gchar * debug, GstMediaInfoPriv * priv)
{
  g_print ("ERROR: %s\n", error->message);
  g_error_free (error);
}

/* helpers */

/* General GError creation */
static void
gst_media_info_error_create (GError ** error, const gchar * message)
{
  /* check if caller wanted an error reported */
  if (error == NULL)
    return;

  *error = g_error_new (GST_MEDIA_INFO_ERROR, 0, message);
  return;
}

/* GError creation when element is missing */
static void
gst_media_info_error_element (const gchar * element, GError ** error)
{
  gchar *message;

  message = g_strdup_printf ("The %s element could not be found. "
      "This element is essential for reading. "
      "Please install the right plug-in and verify "
      "that it works by running 'gst-inspect %s'", element, element);
  gst_media_info_error_create (error, message);
  g_free (message);
  return;
}

/* initialise priv; done the first time */
gboolean
gmip_init (GstMediaInfoPriv * priv, GError ** error)
{
#define GST_MEDIA_INFO_MAKE_OR_ERROR(el, factory, name, error)  \
G_STMT_START {                                                  \
  el = gst_element_factory_make (factory, name);                \
  if (!GST_IS_ELEMENT (el))                                     \
  {                                                             \
    gst_media_info_error_element (factory, error);              \
    return FALSE;                                               \
  }                                                             \
} G_STMT_END
  /* create the typefind element and make sure it stays around by reffing */
  GST_MEDIA_INFO_MAKE_OR_ERROR (priv->typefind, "typefind", "typefind", error);
  gst_object_ref (GST_OBJECT (priv->typefind));

  /* create the fakesink element and make sure it stays around by reffing */
  GST_MEDIA_INFO_MAKE_OR_ERROR (priv->fakesink, "fakesink", "fakesink", error);
  gst_object_ref (GST_OBJECT (priv->fakesink));
  /* source element for media info reading */
  priv->source = NULL;
  priv->source_name = NULL;
  return TRUE;
}

/* called at the beginning of each use cycle */
/* reset info to a state where it can be used to query for media info */
void
gmip_reset (GstMediaInfoPriv * priv)
{

#define STRING_RESET(string)	\
G_STMT_START {			\
  if (string) g_free (string);	\
  string = NULL;		\
} G_STMT_END

  STRING_RESET (priv->pipeline_desc);
  STRING_RESET (priv->location);
#undef STRING_RESET

#define CAPS_RESET(target)		\
G_STMT_START {				\
  if (target) gst_caps_free (target);	\
  target = NULL;			\
} G_STMT_END
  CAPS_RESET (priv->type);
  CAPS_RESET (priv->format);
#undef CAPS_RESET

#define TAGS_RESET(target)		\
G_STMT_START {				\
  if (target)				\
    gst_tag_list_free (target);		\
  target = NULL;			\
} G_STMT_END
  TAGS_RESET (priv->metadata);
  TAGS_RESET (priv->streaminfo);
#undef TAGS_RESET

  if (priv->stream) {
    gmi_stream_free (priv->stream);
    priv->stream = NULL;
  }
  priv->flags = 0;
  priv->state = GST_MEDIA_INFO_STATE_NULL;

  priv->error = NULL;
}

/* seek to a track and reset metadata and streaminfo structs */
gboolean
gmi_seek_to_track (GstMediaInfo * info, long track)
{
  GstEvent *event;
  GstFormat track_format = 0;
  GstMediaInfoPriv *priv = info->priv;
  gboolean res;

  /* FIXME: consider more nicks as "track" */
  track_format = gst_format_get_by_nick ("logical_stream");
  if (track_format == 0)
    return FALSE;
  GST_DEBUG ("Track format: %d", track_format);

  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
      == GST_STATE_FAILURE)
    g_warning ("Couldn't set to play");
  g_assert (GST_IS_PAD (info->priv->decoder_pad));
  event = gst_event_new_seek (track_format |
      GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH, track);
  res = gst_pad_send_event (info->priv->decoder_pad, event);
  if (!res) {
    g_warning ("seek to logical track on pad %s:%s failed",
        GST_DEBUG_PAD_NAME (info->priv->decoder_pad));
    return FALSE;
  }
  /* clear structs because of the seek */
  if (priv->metadata) {
    gst_tag_list_free (priv->metadata);
    priv->metadata = NULL;
  }
  if (priv->streaminfo) {
    gst_tag_list_free (priv->streaminfo);
    priv->streaminfo = NULL;
  }
  return TRUE;
}

/* set the mime type on the media info getter */
gboolean
gmi_set_mime (GstMediaInfo * info, const char *mime)
{
  gchar *desc = NULL;
  GError *error = NULL;
  GstMediaInfoPriv *priv = info->priv;

  /* FIXME: please figure out proper mp3 mimetypes */
  if ((strcmp (mime, "application/x-ogg") == 0) ||
      (strcmp (mime, "application/ogg") == 0))
    desc =
        g_strdup_printf
        ("%s name=source ! oggdemux ! vorbisdec name=decoder ! fakesink name=sink",
        priv->source_name);
  else if ((strcmp (mime, "audio/mpeg") == 0)
      || (strcmp (mime, "audio/x-mp3") == 0)
      || (strcmp (mime, "audio/mp3") == 0)
      || (strcmp (mime, "application/x-id3") == 0)
      || (strcmp (mime, "audio/x-id3") == 0))
    desc =
        g_strdup_printf
        ("%s name=source ! id3tag ! mad name=decoder ! audio/x-raw-int ! fakesink name=sink",
        priv->source_name);
  else if ((strcmp (mime, "application/x-flac") == 0)
      || (strcmp (mime, "audio/x-flac") == 0))
    desc =
        g_strdup_printf
        ("%s name=source ! flacdec name=decoder ! audio/x-raw-int ! fakesink name=sink",
        priv->source_name);
  else if ((strcmp (mime, "audio/wav") == 0)
      || (strcmp (mime, "audio/x-wav") == 0))
    desc =
        g_strdup_printf
        ("%s ! wavparse name=decoder ! audio/x-raw-int ! fakesink name=sink",
        priv->source_name);
  else if (strcmp (mime, "audio/x-mod") == 0
      || strcmp (mime, "audio/x-s3m") == 0 || strcmp (mime, "audio/x-xm") == 0
      || strcmp (mime, "audio/x-it") == 0)
    desc =
        g_strdup_printf
        ("%s name=source ! modplug name=decoder ! audio/x-raw-int ! fakesink name=sink",
        priv->source_name);
  else
    return FALSE;

  GST_DEBUG ("using description %s", desc);
  priv->pipeline_desc = desc;
  priv->pipeline = gst_parse_launch (desc, &error);
  if (error) {
    g_warning ("Error parsing pipeline description: %s\n", error->message);
    g_error_free (error);
    return FALSE;
  }
  /* get a bunch of elements from the bin */
  priv->source = gst_bin_get_by_name (GST_BIN (priv->pipeline), "source");
  if (!GST_IS_ELEMENT (priv->source))
    g_error ("Could not create source element '%s'", priv->source_name);

  g_assert (GST_IS_ELEMENT (priv->source));
  g_object_set (G_OBJECT (priv->source), "location", priv->location, NULL);
  priv->decoder = gst_bin_get_by_name (GST_BIN (priv->pipeline), "decoder");
  g_assert (GST_IS_ELEMENT (priv->decoder));
  priv->fakesink = gst_bin_get_by_name (GST_BIN (priv->pipeline), "sink");
  g_assert (GST_IS_ELEMENT (priv->fakesink));

  /* get the "source " source pad */
  priv->source_pad = gst_element_get_pad (priv->source, "src");
  g_assert (GST_IS_PAD (priv->source_pad));
  /* get the "decoder" source pad */
  priv->decoder_pad = gst_element_get_pad (priv->decoder, "src");
  g_assert (GST_IS_PAD (priv->decoder_pad));
  GST_DEBUG ("decoder pad: %s:%s",
      gst_object_get_name (gst_object_get_parent (GST_OBJECT (priv->
                  decoder_pad))), gst_pad_get_name (priv->decoder_pad));

  /* attach notify handler */
  g_signal_connect (G_OBJECT (info->priv->pipeline), "deep_notify",
      G_CALLBACK (deep_notify_callback), info->priv);
  g_signal_connect (G_OBJECT (info->priv->pipeline), "found-tag",
      G_CALLBACK (found_tag_callback), info->priv);
  g_signal_connect (G_OBJECT (info->priv->pipeline), "error",
      G_CALLBACK (error_callback), info->priv);

  return TRUE;
}

/* clear the decoding pipeline */
void
gmi_clear_decoder (GstMediaInfo * info)
{
  if (info->priv->pipeline) {
    GST_DEBUG ("Unreffing pipeline");
    gst_object_unref (GST_OBJECT (info->priv->pipeline));
  }
  info->priv->pipeline = NULL;
}

/****
 *  typefind functions
 * find the type of a file and store it in the caps of the info
 * FIXME: we might better return GstCaps instead of storing them
 * internally */

/* prepare for typefind, move from NULL to TYPEFIND */
gboolean
gmip_find_type_pre (GstMediaInfoPriv * priv, GError ** error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  GST_DEBUG ("gmip_find_type_pre: start");
  /* find out type */
  /* FIXME: we could move caps for typefind out of struct and
   * just use it through this function only */

  priv->pipeline = gst_pipeline_new ("pipeline-typefind");
  if (!GST_IS_PIPELINE (priv->pipeline)) {
    gst_media_info_error_create (error, "Internal GStreamer error.");
    return FALSE;
  }
  gst_bin_add (GST_BIN (priv->pipeline), priv->typefind);
  GST_MEDIA_INFO_MAKE_OR_ERROR (priv->source, priv->source_name, "source",
      error);
  g_object_set (G_OBJECT (priv->source), "location", priv->location, NULL);
  gst_bin_add (GST_BIN (priv->pipeline), priv->source);
  if (!gst_element_link (priv->source, priv->typefind))
    g_warning ("Couldn't connect source and typefind\n");
  g_signal_connect (G_OBJECT (priv->typefind), "have-type",
      G_CALLBACK (have_type_callback), priv);
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
      == GST_STATE_FAILURE) {
    g_warning ("Couldn't set to play");
    return FALSE;
  }
  GST_DEBUG ("moving to STATE_TYPEFIND\n");
  priv->state = GST_MEDIA_INFO_STATE_TYPEFIND;
  return TRUE;
}

/* finish off typefind */
gboolean
gmip_find_type_post (GstMediaInfoPriv * priv)
{
  /*clear up typefind */
  gst_element_set_state (priv->pipeline, GST_STATE_READY);
  if (priv->decontainer) {
    gst_element_unlink (priv->source, priv->decontainer);
    gst_element_unlink (priv->decontainer, priv->typefind);
    gst_bin_remove (GST_BIN (priv->pipeline), priv->decontainer);
  } else {
    gst_element_unlink (priv->source, priv->typefind);
  }
  gst_bin_remove (GST_BIN (priv->pipeline), priv->typefind);

  if (priv->type == NULL) {
    g_warning ("iteration ended, type not found !\n");
    return FALSE;
  }
  GST_DEBUG ("moving to STATE_STREAM\n");
  priv->state = GST_MEDIA_INFO_STATE_STREAM;
  return TRUE;
}

/* complete version */
gboolean
gmip_find_type (GstMediaInfoPriv * priv, GError ** error)
{
  if (!gmip_find_type_pre (priv, error))
    return FALSE;
  GST_DEBUG ("gmip_find_type: iterating");
  while ((priv->type == NULL) && gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG ("+");
  GMI_DEBUG ("\n");
  return gmip_find_type_post (priv);
}

/* FIXME: why not have these functions work on priv types ? */
gboolean
gmip_find_stream_pre (GstMediaInfoPriv * priv)
{
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
      == GST_STATE_FAILURE) {
    g_warning ("Couldn't set to play");
    return FALSE;
  }
  priv->state = GST_MEDIA_INFO_STATE_STREAM;
  return TRUE;
}

gboolean
gmip_find_stream_post (GstMediaInfoPriv * priv)
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
  while (*formats) {
    const GstFormatDefinition *definition;

    format = *formats;

    g_assert (GST_IS_PAD (priv->decoder_pad));
    definition = gst_format_get_details (*formats);
    GST_DEBUG ("trying to figure out length for format %s", definition->nick);

    res = gst_pad_query (priv->decoder_pad, GST_QUERY_TOTAL, &format, &value);

    if (res) {
      switch (format) {
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
          if (format == track_format) {
            stream->length_tracks = value;
            GST_DEBUG ("  total %s: %lld", definition->nick, value);
          } else
            GST_DEBUG ("unhandled format %s", definition->nick);
      }
    } else
      GST_DEBUG ("query didn't return result for %s", definition->nick);

    formats++;
  }
  if (stream->length_tracks == 0)
    stream->length_tracks = 1;

  /* now get number of bytes from the sink pad to get the bitrate */
  format = GST_FORMAT_BYTES;
  g_assert (GST_IS_PAD (priv->source_pad));
  res = gst_pad_query (priv->source_pad, GST_QUERY_TOTAL, &format, &value);
  if (!res)
    g_warning ("Failed to query on sink pad !");
  bytes = value;
  GST_DEBUG ("bitrate calc: bytes gotten: %ld", bytes);

  if (bytes) {
    double seconds = (double) stream->length_time / GST_SECOND;
    double bits = bytes * 8;

    stream->bitrate = (long) (bits / seconds);
  }
  GST_DEBUG ("moving to STATE_METADATA\n");
  priv->state = GST_MEDIA_INFO_STATE_METADATA;  /* metadata of first track */
  return TRUE;
}

/* get properties of complete physical stream
 * and return them in pre-alloced stream struct in priv->stream */
gboolean
gmip_find_stream (GstMediaInfoPriv * priv)
{
  GST_DEBUG ("mip_find_stream start");

  gmip_find_stream_pre (priv);
  /* iterate until caps are found */
  /* FIXME: this should be done through the plugin sending some signal
   * that it is ready for queries */
  while (gst_bin_iterate (GST_BIN (priv->pipeline)) && priv->format == NULL);
  if (gst_element_set_state (priv->pipeline, GST_STATE_PAUSED)
      == GST_STATE_FAILURE)
    g_warning ("Couldn't set to paused");

  if (priv->format == NULL) {
    GMI_DEBUG ("gmip_find_stream: couldn't get caps !");
    return FALSE;
  }
  return gmip_find_stream_post (priv);
}

/* find metadata encoded in media and store in priv->metadata */
gboolean
gmip_find_track_metadata_pre (GstMediaInfoPriv * priv)
{
  /* FIXME: this is a hack to set max allowed iterations for metadata
   * querying - we should make gst smarter by itself instead */
  priv->metadata_iters = 0;
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
      == GST_STATE_FAILURE) {
    g_warning ("Couldn't set to play");
    return FALSE;
  }
  return TRUE;
}

gboolean
gmip_find_track_metadata_post (GstMediaInfoPriv * priv)
{
  if (gst_element_set_state (priv->pipeline, GST_STATE_PAUSED)
      == GST_STATE_FAILURE)
    return FALSE;
  priv->current_track->metadata = priv->metadata;
  priv->metadata = NULL;
  return TRUE;
}

gboolean
gmip_find_track_metadata (GstMediaInfoPriv * priv)
{
  gmip_find_track_metadata_pre (priv);
  GST_DEBUG ("gmip_find_metadata: iterating");
  while ((priv->metadata == NULL) && gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG ("+");
  GMI_DEBUG ("\n");
  gmip_find_track_metadata_post (priv);

  return TRUE;
}

/* find streaminfo found by decoder and store in priv->streaminfo */
/* FIXME: this is an exact copy, so reuse this function instead */
gboolean
gmip_find_track_streaminfo_pre (GstMediaInfoPriv * priv)
{
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
      == GST_STATE_FAILURE) {
    g_warning ("Couldn't set to play");
    return FALSE;
  }
  return TRUE;
}

gboolean
gmip_find_track_streaminfo_post (GstMediaInfoPriv * priv)
{
  GstFormat format, track_format;

  gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);

  /* now add total length to this, and maybe even bitrate ? FIXME */
  track_format = gst_format_get_by_nick ("logical_stream");
  if (track_format == 0) {
    g_print ("FIXME: implement getting length of whole track\n");
  } else {
    /* which one are we at ? */
    long track_num;
    gint64 value_start, value_end;
    gboolean res;

    res = gst_pad_query (priv->decoder_pad, GST_QUERY_POSITION,
        &track_format, &value_start);
    if (res) {
      format = GST_FORMAT_TIME;
      track_num = value_start;
      GST_DEBUG ("we are currently at %ld", track_num);
      res = gst_pad_convert (priv->decoder_pad,
          track_format, track_num, &format, &value_start);
      res &= gst_pad_convert (priv->decoder_pad,
          track_format, track_num + 1, &format, &value_end);
      if (res) {
        /* substract to get the length */
        GST_DEBUG ("start %lld, end %lld", value_start, value_end);
        value_end -= value_start;
        /* FIXME: check units; this is in seconds */

        gst_tag_list_add (priv->streaminfo, GST_TAG_MERGE_REPLACE,
            GST_TAG_DURATION, (int) (value_end / 1E6), NULL);
      }
    }
  }
  priv->current_track->streaminfo = priv->streaminfo;
  priv->streaminfo = NULL;

  return TRUE;
}

gboolean
gmip_find_track_streaminfo (GstMediaInfoPriv * priv)
{
  gmip_find_track_streaminfo_pre (priv);
  GST_DEBUG ("DEBUG: gmip_find_streaminfo: iterating");
  while ((priv->streaminfo == NULL) &&
      gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG ("+");
  GMI_DEBUG ("\n");
  gmip_find_track_streaminfo_post (priv);

  return TRUE;
}

/* find format found by decoder and store in priv->format */
gboolean
gmip_find_track_format_pre (GstMediaInfoPriv * priv)
{
  if (gst_element_set_state (priv->pipeline, GST_STATE_PLAYING)
      == GST_STATE_FAILURE) {
    g_warning ("Couldn't set to play");
    return FALSE;
  }
  return TRUE;
}

gboolean
gmip_find_track_format_post (GstMediaInfoPriv * priv)
{
  if (gst_element_set_state (priv->pipeline, GST_STATE_PAUSED)
      == GST_STATE_FAILURE)
    return FALSE;
  priv->current_track->format = priv->format;
  priv->format = NULL;
  return TRUE;
}

gboolean
gmip_find_track_format (GstMediaInfoPriv * priv)
{
  gmip_find_track_format_pre (priv);
  GST_DEBUG ("DEBUG: gmip_find_format: iterating");
  while ((priv->format == NULL) && gst_bin_iterate (GST_BIN (priv->pipeline)))
    GMI_DEBUG ("+");
  GMI_DEBUG ("\n");
  gmip_find_track_format_post (priv);

  return TRUE;
}
