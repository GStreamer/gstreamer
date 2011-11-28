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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-taginject
 *
 * Element that injects new metadata tags, but passes incomming data through
 * unmodified.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch audiotestsrc num-buffers=100 ! taginject tags="title=testsrc,artist=gstreamer" ! vorbisenc ! oggmux ! filesink location=test.ogg
 * ]| set title and artist
 * |[
 * gst-launch audiotestsrc num-buffers=100 ! taginject tags="keywords=\{\"testone\",\"audio\"\},title=\"audio testtone\"" ! vorbisenc ! oggmux ! filesink location=test.ogg
 * ]| set keywords and title demonstrating quoting of special chars and handling lists
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>

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
  PROP_TAGS = 1
};


#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_tag_inject_debug, "taginject", 0, "tag inject element");

GST_BOILERPLATE_FULL (GstTagInject, gst_tag_inject, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_tag_inject_finalize (GObject * object);
static void gst_tag_inject_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tag_inject_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_tag_inject_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_tag_inject_start (GstBaseTransform * trans);


static void
gst_tag_inject_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (gstelement_class,
      "TagInject",
      "Generic", "inject metadata tags", "Stefan Kost <ensonic@users.sf.net>");
  gst_element_class_add_static_pad_template (gstelement_class,
      &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class,
      &sinktemplate);
}

static void
gst_tag_inject_finalize (GObject * object)
{
  GstTagInject *self = GST_TAG_INJECT (object);

  if (self->tags) {
    gst_tag_list_free (self->tags);
    self->tags = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_tag_inject_class_init (GstTagInjectClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *gstbasetrans_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbasetrans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_tag_inject_set_property;
  gobject_class->get_property = gst_tag_inject_get_property;

  g_object_class_install_property (gobject_class, PROP_TAGS,
      g_param_spec_string ("tags", "taglist",
          "List of tags to inject into the target file",
          NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_tag_inject_finalize;

  gstbasetrans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_tag_inject_transform_ip);

  gstbasetrans_class->start = GST_DEBUG_FUNCPTR (gst_tag_inject_start);
}

static void
gst_tag_inject_init (GstTagInject * self, GstTagInjectClass * g_class)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (self);

  gst_base_transform_set_gap_aware (trans, TRUE);

  self->tags = NULL;
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
      gst_element_found_tags (GST_ELEMENT (trans),
          gst_tag_list_copy (self->tags));
    }
  }

  return GST_FLOW_OK;
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
      if (!(self->tags = gst_structure_from_string (structure, NULL))) {
        GST_WARNING ("unparsable taglist = '%s'", structure);
      }

      /* make sure that tags will be send */
      self->tags_sent = FALSE;
      g_free (structure);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tag_inject_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  /*GstTagInject *self = GST_TAG_INJECT (object); */

  switch (prop_id) {
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
