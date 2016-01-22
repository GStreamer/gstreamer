/**
 * Gstreamer
 *
 * Copyright (c) 2012, Collabora Ltd.
 * Author: Thibault Saunier <thibault.saunier@collabora.com>
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

#include <string.h>
#include "media-descriptor.h"

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstValidateMediaDescriptor,
    gst_validate_media_descriptor, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VALIDATE_REPORTER, NULL));

#define GST_VALIDATE_MEDIA_DESCRIPTOR_GET_PRIVATE(o)\
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_VALIDATE_MEDIA_DESCRIPTOR, GstValidateMediaDescriptorPrivate))

static inline void
free_tagnode (GstValidateMediaGstValidateMediaGstValidateMediaTagNode * tagnode)
{
  g_free (tagnode->str_open);
  g_free (tagnode->str_close);
  if (tagnode->taglist)
    gst_tag_list_unref (tagnode->taglist);

  g_slice_free (GstValidateMediaGstValidateMediaGstValidateMediaTagNode,
      tagnode);
}

static inline void
    free_tagsnode
    (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaTagsNode *
    tagsnode)
{
  g_free (tagsnode->str_open);
  g_free (tagsnode->str_close);
  g_list_free_full (tagsnode->tags, (GDestroyNotify) free_tagnode);
  g_slice_free
      (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaTagsNode,
      tagsnode);
}

static inline void
free_framenode (GstValidateMediaGstValidateMediaGstValidateMediaFrameNode *
    framenode)
{
  g_free (framenode->str_open);
  g_free (framenode->str_close);

  if (framenode->buf)
    gst_buffer_unref (framenode->buf);

  g_slice_free (GstValidateMediaGstValidateMediaGstValidateMediaFrameNode,
      framenode);
}

static inline void
    free_streamnode
    (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
    * streamnode)
{
  if (streamnode->caps)
    gst_caps_unref (streamnode->caps);

  g_list_free_full (streamnode->frames, (GDestroyNotify) free_framenode);

  if (streamnode->pad)
    gst_object_unref (streamnode->pad);

  if (streamnode->tags)
    free_tagsnode (streamnode->tags);

  if (streamnode->padname)
    g_free (streamnode->padname);

  if (streamnode->id)
    g_free (streamnode->id);

  g_free (streamnode->str_open);
  g_free (streamnode->str_close);
  g_slice_free
      (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode,
      streamnode);
}

void gst_validate_filenode_free
    (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaFileNode *
    filenode)
{
  g_list_free_full (filenode->streams, (GDestroyNotify) free_streamnode);
  if (filenode->tags)
    free_tagsnode (filenode->tags);

  if (filenode->uri)
    g_free (filenode->uri);

  if (filenode->caps)
    gst_caps_unref (filenode->caps);

  g_free (filenode->str_open);
  g_free (filenode->str_close);

  g_slice_free
      (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaFileNode,
      filenode);
}

gboolean
    gst_validate_gst_validate_gst_validate_gst_validate_tag_node_compare
    (GstValidateMediaGstValidateMediaGstValidateMediaTagNode * tnode,
    const GstTagList * tlist)
{
  if (gst_structure_is_equal (GST_STRUCTURE (tlist),
          GST_STRUCTURE (tnode->taglist)) == FALSE) {
    return FALSE;
  }

  tnode->found = TRUE;

  return TRUE;
}

struct _GstValidateMediaDescriptorPrivate
{
  gpointer dummy;
};

enum
{
  PROP_0,
  PROP_RUNNER,
  PROP_LAST
};


static void
gst_validate_media_descriptor_dispose (GstValidateMediaDescriptor * self)
{
  G_OBJECT_CLASS (gst_validate_media_descriptor_parent_class)->dispose (G_OBJECT
      (self));
}

static void
gst_validate_media_descriptor_finalize (GstValidateMediaDescriptor * self)
{
  if (self->filenode)
    gst_validate_filenode_free (self->filenode);

  G_OBJECT_CLASS (gst_validate_media_descriptor_parent_class)->finalize
      (G_OBJECT (self));
}

static void
gst_validate_media_descriptor_init (GstValidateMediaDescriptor * self)
{
  self->filenode =
      g_slice_new0
      (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaFileNode);
}

static void
_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      gst_validate_reporter_set_runner (GST_VALIDATE_REPORTER (object),
          g_value_get_object (value));
      break;
    default:
      break;
  }
}

static void
_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      g_value_set_object (value,
          gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (object)));
      break;
    default:
      break;
  }
}

static void
gst_validate_media_descriptor_class_init (GstValidateMediaDescriptorClass *
    self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  g_type_class_add_private (self_class,
      sizeof (GstValidateMediaDescriptorPrivate));
  object_class->dispose =
      (void (*)(GObject * object)) gst_validate_media_descriptor_dispose;
  object_class->finalize =
      (void (*)(GObject * object)) gst_validate_media_descriptor_finalize;

  object_class->get_property = _get_property;
  object_class->set_property = _set_property;

  g_object_class_install_property (object_class, PROP_RUNNER,
      g_param_spec_object ("validate-runner", "VALIDATE Runner",
          "The Validate runner to " "report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

static gint
compare_tags (GstValidateMediaDescriptor * ref,
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode *
    rstream,
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode *
    cstream)
{
  gboolean found;
  GstValidateMediaGstValidateMediaGstValidateMediaTagNode *rtag, *ctag;
  GList *rtag_list, *ctag_list;
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaTagsNode
      * rtags, *ctags;

  rtags = rstream->tags;
  ctags = cstream->tags;
  if (!rtags && !ctags)
    return 1;
  else if (!rtags && ctags) {
    GList *taglist;
    GString *all_tags = g_string_new (NULL);

    for (taglist = ctags->tags; taglist; taglist = taglist->next) {
      gchar *stags =
          gst_tag_list_to_string ((
              (GstValidateMediaGstValidateMediaGstValidateMediaTagNode *)
              taglist->data)->taglist);

      g_string_append_printf (all_tags, "%s\n", stags);
      g_free (stags);
    }

    GST_VALIDATE_REPORT (ref, FILE_TAG_DETECTION_INCORRECT,
        "Reference descriptor for stream %s has NO tags"
        " but tags found: %s", rstream->id, all_tags->str);

    g_string_free (all_tags, TRUE);

    return 0;
  } else if (rtags && !ctags) {
    GList *taglist;
    GString *all_tags = g_string_new (NULL);

    for (taglist = rtags->tags; taglist; taglist = taglist->next) {
      gchar *stags =
          gst_tag_list_to_string ((
              (GstValidateMediaGstValidateMediaGstValidateMediaTagNode *)
              taglist->data)->taglist);

      g_string_append_printf (all_tags, "%s\n", stags);
      g_free (stags);
    }

    GST_VALIDATE_REPORT (ref, FILE_TAG_DETECTION_INCORRECT,
        "Reference descriptor for stream %s has tags:\n %s\n"
        " but NO tags found on the stream", rstream->id, all_tags->str);

    g_string_free (all_tags, TRUE);
    return 0;
  }

  for (rtag_list = rtags->tags; rtag_list; rtag_list = rtag_list->next) {
    rtag = rtag_list->data;
    found = FALSE;
    for (ctag_list = ctags->tags; ctag_list; ctag_list = ctag_list->next) {
      ctag = ctag_list->data;
      if (gst_tag_list_is_equal (rtag->taglist, ctag->taglist)) {
        found = TRUE;

        break;
      }
    }

    if (found == FALSE) {
      gchar *rtaglist = gst_tag_list_to_string (rtag->taglist);

      GST_VALIDATE_REPORT (ref, FILE_TAG_DETECTION_INCORRECT,
          "Reference descriptor for stream %s has tags %s"
          " but no equivalent taglist was found on the compared stream",
          rstream->id, rtaglist);
      g_free (rtaglist);

      return 0;
    }
  }

  return 1;
}

/* Workaround false warning caused by differnet file path */
static gboolean
stream_id_is_equal (const gchar * uri, const gchar * rid, const gchar * cid)
{
  gboolean is_file = g_str_has_prefix (uri, "file://");
  GChecksum *cs;
  const gchar *stream_id;

  /* Simple case it's the same */
  if (g_strcmp0 (rid, cid) == 0)
    return TRUE;

  /* If it's not from file, it should have been the same */
  if (!is_file)
    return FALSE;

  /* taken from basesrc, compute the reference stream-id */
  cs = g_checksum_new (G_CHECKSUM_SHA256);
  g_checksum_update (cs, (const guchar *) uri, strlen (uri));

  stream_id = g_checksum_get_string (cs);

  /* If the reference stream_id is the URI SHA256, that means we have a single
   * stream file (no demuxing), just assume it's the same id */
  if (g_strcmp0 (rid, stream_id) == 0) {
    g_checksum_free (cs);
    return TRUE;
  }

  /* It should always be prefixed with the SHA256, otherwise it likely means
   * that basesrc is no longer using a SHA256 checksum on the URI, and this
   * workaround will need to be fixed */
  if (!g_str_has_prefix (rid, stream_id)) {
    g_checksum_free (cs);
    return FALSE;
  }
  g_checksum_free (cs);

  /* we strip the IDS to the delimitor, and then compare */
  rid = strchr (rid, '/');
  cid = strchr (cid, '/');

  if (rid == NULL || cid == NULL)
    return FALSE;

  if (g_strcmp0 (rid, cid) == 0)
    return TRUE;

  return FALSE;
}

static gboolean
compare_frames (GstValidateMediaDescriptor * ref,
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode *
    rstream, GstValidateMediaGstValidateMediaGstValidateMediaFrameNode * rframe,
    GstValidateMediaGstValidateMediaGstValidateMediaFrameNode * cframe)
{
  if (rframe->id != cframe->id) {
    GST_VALIDATE_REPORT (ref, FILE_FRAMES_INCORRECT,
        "Stream frame %s ids mismatch: %" G_GUINT64_FORMAT " != %"
        G_GUINT64_FORMAT, rstream->id, rframe->id, cframe->id);
    return FALSE;
  }
#define CHECK_FRAME_FIELD(fieldname, format) \
  if (rframe->fieldname != cframe->fieldname) { \
    GST_VALIDATE_REPORT (ref, FILE_FRAMES_INCORRECT, \
        "Stream %s frames with id %" G_GUINT64_FORMAT " have " #fieldname \
        " mismatch. Expected " format ", got " format, rstream->id, \
        rframe->id, rframe->fieldname, cframe->fieldname); \
    return FALSE; \
  }

  CHECK_FRAME_FIELD (pts, "%" G_GUINT64_FORMAT);
  CHECK_FRAME_FIELD (dts, "%" G_GUINT64_FORMAT);
  CHECK_FRAME_FIELD (duration, "%" G_GUINT64_FORMAT);
  CHECK_FRAME_FIELD (running_time, "%" G_GUINT64_FORMAT);
  CHECK_FRAME_FIELD (offset, "%" G_GUINT64_FORMAT);
  CHECK_FRAME_FIELD (offset_end, "%" G_GUINT64_FORMAT);
  CHECK_FRAME_FIELD (is_keyframe, "%d");

  return TRUE;
}

static gboolean
compare_frames_list (GstValidateMediaDescriptor * ref,
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode *
    rstream,
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode *
    cstream)
{
  GList *rframes, *cframes;

  if (g_list_length (rstream->frames) != g_list_length (cstream->frames)) {
    GST_VALIDATE_REPORT (ref, FILE_FRAMES_INCORRECT,
        "Stream reference has %i frames, compared one has %i frames",
        g_list_length (rstream->frames), g_list_length (cstream->frames));
    return FALSE;
  }

  for (rframes = rstream->frames, cframes = cstream->frames; rframes;
      rframes = g_list_next (rframes), cframes = g_list_next (cframes)) {
    GstValidateMediaGstValidateMediaGstValidateMediaFrameNode *rframe, *cframe;

    if (cframes == NULL) {
      /* The list was checked to be of the same size */
      g_assert_not_reached ();
      return FALSE;
    }

    rframe = rframes->data;
    cframe = cframes->data;

    if (!compare_frames (ref, rstream, rframe, cframe)) {
      return FALSE;
    }
  }

  return TRUE;
}

/*  Return -1 if not found 1 if OK 0 if an error occured */
static gint
compare_streams (GstValidateMediaDescriptor * ref,
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode *
    rstream,
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode *
    cstream)
{
  if (stream_id_is_equal (ref->filenode->uri, rstream->id, cstream->id)) {
    if (!gst_caps_is_equal (rstream->caps, cstream->caps)) {
      gchar *rcaps = gst_caps_to_string (rstream->caps),
          *ccaps = gst_caps_to_string (cstream->caps);
      GST_VALIDATE_REPORT (ref, FILE_PROFILE_INCORRECT,
          "Reference descriptor for stream %s has caps: %s"
          " but compared stream %s has caps: %s",
          rstream->id, rcaps, cstream->id, ccaps);
      g_free (rcaps);
      g_free (ccaps);
      return 0;
    }

    /* We ignore the return value on purpose as this is not critical */
    compare_tags (ref, rstream, cstream);

    if (compare_frames_list (ref, rstream, cstream))
      return 1;
    return 0;
  }

  return -1;
}

gboolean
gst_validate_media_descriptors_compare (GstValidateMediaDescriptor * ref,
    GstValidateMediaDescriptor * compared)
{
  GList *rstream_list;
  GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaFileNode
      * rfilenode = ref->filenode, *cfilenode = compared->filenode;

  if (rfilenode->duration != cfilenode->duration) {
    GST_VALIDATE_REPORT (ref, FILE_DURATION_INCORRECT,
        "Duration %" GST_TIME_FORMAT " is different from the reference %"
        GST_TIME_FORMAT, GST_TIME_ARGS (cfilenode->duration),
        GST_TIME_ARGS (rfilenode->duration));
  }

  if (rfilenode->seekable != cfilenode->seekable) {
    GST_VALIDATE_REPORT (ref, FILE_SEEKABLE_INCORRECT,
        "File known as %s but is reported %s now",
        rfilenode->seekable ? "seekable" : "not seekable",
        cfilenode->seekable ? "seekable" : "not seekable");
  }

  if (g_list_length (rfilenode->streams) != g_list_length (cfilenode->streams)) {
    GST_VALIDATE_REPORT (ref, FILE_PROFILE_INCORRECT,
        "Reference descriptor has %i streams != compared which has %i streams",
        g_list_length (rfilenode->streams), g_list_length (cfilenode->streams));

    return FALSE;
  }


  for (rstream_list = rfilenode->streams; rstream_list;
      rstream_list = rstream_list->next) {
    GList *cstream_list;
    gint sfound = -1;

    for (cstream_list = cfilenode->streams; cstream_list;
        cstream_list = cstream_list->next) {

      sfound = compare_streams (ref, rstream_list->data, cstream_list->data);
      if (sfound == 0) {
        return FALSE;
      } else if (sfound == 1) {
        break;
      }
    }

    if (sfound == -1) {
      GST_VALIDATE_REPORT (ref, FILE_PROFILE_INCORRECT,
          "Could not find stream %s in the compared descriptor",
          ((GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode *) rstream_list->data)->id);

      return FALSE;
    }
  }

  return TRUE;
}

gboolean
gst_validate_media_descriptor_detects_frames (GstValidateMediaDescriptor * self)
{
  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR (self), FALSE);
  g_return_val_if_fail (self->filenode, FALSE);

  return self->filenode->frame_detection;
}

gboolean
gst_validate_media_descriptor_get_buffers (GstValidateMediaDescriptor * self,
    GstPad * pad, GCompareFunc compare_func, GList ** bufs)
{
  GList *tmpstream, *tmpframe;
  gboolean check = (pad == NULL), ret = FALSE;
  GstCaps *pad_caps = gst_pad_get_current_caps (pad);

  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR (self), FALSE);
  g_return_val_if_fail (self->filenode, FALSE);

  for (tmpstream = self->filenode->streams;
      tmpstream; tmpstream = tmpstream->next) {
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        * streamnode =
        (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        *) tmpstream->data;

    if (pad && streamnode->pad == pad)
      check = TRUE;

    if (!streamnode->pad && gst_caps_is_subset (pad_caps, streamnode->caps)) {
      check = TRUE;
    }

    if (check) {
      ret = TRUE;
      for (tmpframe = streamnode->frames; tmpframe; tmpframe = tmpframe->next) {
        if (compare_func)
          *bufs =
              g_list_insert_sorted (*bufs,
              gst_buffer_ref ((
                      (GstValidateMediaGstValidateMediaGstValidateMediaFrameNode
                          *) tmpframe->data)->buf), compare_func);
        else
          *bufs =
              g_list_prepend (*bufs,
              gst_buffer_ref ((
                      (GstValidateMediaGstValidateMediaGstValidateMediaFrameNode
                          *) tmpframe->data)->buf));
      }

      if (pad != NULL)
        goto done;
    }
  }


done:

  if (compare_func == NULL)
    *bufs = g_list_reverse (*bufs);

  gst_caps_unref (pad_caps);
  return ret;
}

gboolean
gst_validate_media_descriptor_has_frame_info (GstValidateMediaDescriptor * self)
{
  GList *tmpstream;

  for (tmpstream = self->filenode->streams;
      tmpstream; tmpstream = tmpstream->next) {
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        * streamnode =
        (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        *) tmpstream->data;

    if (g_list_length (streamnode->frames))
      return TRUE;
  }

  return FALSE;
}

GstClockTime
gst_validate_media_descriptor_get_duration (GstValidateMediaDescriptor * self)
{
  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR (self), FALSE);
  g_return_val_if_fail (self->filenode, FALSE);

  return self->filenode->duration;
}

gboolean
gst_validate_media_descriptor_get_seekable (GstValidateMediaDescriptor * self)
{
  g_return_val_if_fail (GST_IS_VALIDATE_MEDIA_DESCRIPTOR (self), FALSE);
  g_return_val_if_fail (self->filenode, FALSE);

  return self->filenode->seekable;
}

GList *
gst_validate_media_descriptor_get_pads (GstValidateMediaDescriptor * self)
{
  GList *ret = NULL, *tmp;

  for (tmp = self->filenode->streams; tmp; tmp = tmp->next) {
    GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        * snode =
        (GstValidateMediaGstValidateMediaGstValidateMediaGstValidateMediaStreamNode
        *) tmp->data;
    ret = g_list_append (ret, gst_pad_new (snode->padname, GST_PAD_UNKNOWN));
  }

  return ret;
}
