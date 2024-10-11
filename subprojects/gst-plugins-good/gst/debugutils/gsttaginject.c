/* GStreamer
 * Copyright (C) 2008 Stefan Kost <ensonic@users.sf.net>
 *
 * gsttaginject.c:
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
 * SECTION:element-taginject
 * @title: taginject
 *
 * Element that injects new metadata tags, but passes incoming data through
 * unmodified.
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 audiotestsrc num-buffers=100 ! taginject tags="title=testsrc,artist=gstreamer" ! vorbisenc ! oggmux ! filesink location=test.ogg
 * ]| set title and artist
 * |[
 * gst-launch-1.0 audiotestsrc num-buffers=100 ! taginject tags="keywords=\{\"testone\",\"audio\"\},title=\"audio\ testtone\"" ! vorbisenc ! oggmux ! filesink location=test.ogg
 * ]| set keywords and title demonstrating quoting of special chars and handling lists
 * |[
 * gst-launch-1.0.exe audiotestsrc num-buffers=500 ! taginject tags="title=MyTitle,artist=MyArtist,album=MyAlbum,genre=MyGenre" scope=global ! qtmux ! filesink location=test.m4a
 * ]| set title, artist, album and genre. set scope as global
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>

#include "gstdebugutilselements.h"
#include "gsttaginject.h"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_tag_inject_debug);
#define GST_CAT_DEFAULT gst_tag_inject_debug

enum
{
  PROP_TAGS = 1,
  PROP_SCOPE,
  PROP_MERGE_MODE
};


#define gst_tag_inject_parent_class parent_class
G_DEFINE_TYPE (GstTagInject, gst_tag_inject, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE (taginject, "taginject",
    GST_RANK_NONE, gst_tag_inject_get_type ());

static void gst_tag_inject_finalize (GObject * object);
static void gst_tag_inject_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tag_inject_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_tag_inject_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_tag_inject_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_tag_inject_start (GstBaseTransform * trans);


static void
gst_tag_inject_finalize (GObject * object)
{
  GstTagInject *self = GST_TAG_INJECT (object);

  if (self->tags) {
    gst_tag_list_unref (self->tags);
    self->tags = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_tag_inject_class_init (GstTagInjectClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetrans_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasetrans_class = GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_tag_inject_debug, "taginject", 0,
      "tag inject element");

  gobject_class->set_property = gst_tag_inject_set_property;
  gobject_class->get_property = gst_tag_inject_get_property;

  g_object_class_install_property (gobject_class, PROP_TAGS,
      g_param_spec_string ("tags", "taglist",
          "List of tags to inject into the target file",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * taginject:scope:
   *
   * Scope of tags to inject (stream | global).
   *
   * Since: 1.24
   **/
  g_object_class_install_property (gobject_class, PROP_SCOPE,
      g_param_spec_enum ("scope", "Scope",
          "Scope of tags to inject (stream | global)",
          GST_TYPE_TAG_SCOPE, GST_TAG_SCOPE_STREAM,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * taginject:merge-mode:
   *
   * Merge mode to merge tags from this element with upstream tags.
   *
   * Since: 1.26
   **/
  g_object_class_install_property (gobject_class, PROP_MERGE_MODE,
      g_param_spec_enum ("merge-mode", "Merge Mode",
          "Merge mode to merge tags from this element with upstream tags",
          GST_TYPE_TAG_MERGE_MODE, GST_TAG_MERGE_REPLACE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_tag_inject_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "TagInject",
      "Generic", "inject metadata tags", "Stefan Kost <ensonic@users.sf.net>");
  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gstbasetrans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_tag_inject_transform_ip);
  gstbasetrans_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_tag_inject_sink_event);

  gstbasetrans_class->start = GST_DEBUG_FUNCPTR (gst_tag_inject_start);
}

static void
gst_tag_inject_init (GstTagInject * self)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (self);

  gst_base_transform_set_gap_aware (trans, TRUE);

  self->tags = NULL;
  self->tags_scope = GST_TAG_SCOPE_STREAM;
  self->merge_mode = GST_TAG_MERGE_REPLACE;
}

static GstFlowReturn
gst_tag_inject_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstTagInject *self = GST_TAG_INJECT (trans);

  if (G_UNLIKELY (!self->tags_sent)) {
    self->tags_sent = TRUE;
    /* send tags */
    if (self->tags && !gst_tag_list_is_empty (self->tags)) {
      GST_DEBUG ("tag event :%" GST_PTR_FORMAT, self->tags);
      gst_pad_push_event (GST_BASE_TRANSFORM_SRC_PAD (trans),
          gst_event_new_tag (gst_tag_list_ref (self->tags)));
    }
  }

  return GST_FLOW_OK;
}

static gboolean
gst_tag_inject_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstTagInject *self = GST_TAG_INJECT (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *tags;

      gst_event_parse_tag (event, &tags);
      if (gst_tag_list_get_scope (tags) == self->tags_scope) {
        GstTagList *new_tags;
        guint32 seqnum = gst_event_get_seqnum (event);

        new_tags = gst_tag_list_merge (tags, self->tags, self->merge_mode);
        gst_tag_list_set_scope (new_tags, self->tags_scope);
        gst_event_unref (event);
        event = gst_event_new_tag (new_tags);
        gst_event_set_seqnum (event, seqnum);

        self->tags_sent = TRUE;
      }
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static void
gst_tag_inject_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTagInject *self = GST_TAG_INJECT (object);

  switch (prop_id) {
    case PROP_TAGS:{
      gchar *structure =
          g_strdup_printf ("taglist,%s", g_value_get_string (value));
      if (!(self->tags = gst_tag_list_new_from_string (structure))) {
        GST_WARNING ("unparsable taglist = '%s'", structure);
      } else {
        gst_tag_list_set_scope (self->tags, self->tags_scope);
      }

      /* make sure that tags will be send */
      self->tags_sent = FALSE;
      g_free (structure);
      break;
    }
    case PROP_SCOPE:
      self->tags_scope = g_value_get_enum (value);
      if (self->tags)
        gst_tag_list_set_scope (self->tags, self->tags_scope);
      break;
    case PROP_MERGE_MODE:
      self->merge_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tag_inject_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTagInject *self = GST_TAG_INJECT (object);

  switch (prop_id) {
    case PROP_TAGS:
      g_value_take_string (value,
          self->tags ? gst_tag_list_to_string (self->tags) : NULL);
      break;
    case PROP_SCOPE:
      g_value_set_enum (value, self->tags_scope);
      break;
    case PROP_MERGE_MODE:
      g_value_set_enum (value, self->merge_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_tag_inject_start (GstBaseTransform * trans)
{
  GstTagInject *self = GST_TAG_INJECT (trans);

  /* we need to sent tags _transform_ip() once */
  self->tags_sent = FALSE;

  return TRUE;
}
