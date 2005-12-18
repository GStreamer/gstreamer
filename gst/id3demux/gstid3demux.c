/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* Copyright 2005 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2003-2004 Benjamin Otte <otte@gnome.org>
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
#include <gst/gst.h>
#include <gst/gst-i18n-plugin.h>

#include "gstid3demux.h"
#include "id3tags.h"

enum
{
  ARG_0,
  ARG_PREFER_V1
};

/* Require at least 4kB of data before we attempt typefind. 
 * Seems a decent value based on test files
 * 40kB is massive overkill for the maximum, I think, but it 
 * doesn't do any harm */
#define ID3_TYPE_FIND_MIN_SIZE 4096
#define ID3_TYPE_FIND_MAX_SIZE 40960

GST_DEBUG_CATEGORY (id3demux_debug);
#define GST_CAT_DEFAULT (id3demux_debug)

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-id3")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY")
    );

static void gst_id3demux_class_init (GstID3DemuxClass * klass);
static void gst_id3demux_base_init (GstID3DemuxClass * klass);
static void gst_id3demux_init (GstID3Demux * id3demux);
static void gst_id3demux_dispose (GObject * object);

static void gst_id3demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_id3demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_id3demux_chain (GstPad * pad, GstBuffer * buf);

static gboolean gst_id3demux_src_activate_pull (GstPad * pad, gboolean active);
static GstFlowReturn gst_id3demux_read_range (GstID3Demux * id3demux,
    guint64 offset, guint length, GstBuffer ** buffer);

static gboolean gst_id3demux_src_checkgetrange (GstPad * srcpad);
static GstFlowReturn gst_id3demux_src_getrange (GstPad * srcpad,
    guint64 offset, guint length, GstBuffer ** buffer);

static gboolean gst_id3demux_add_srcpad (GstID3Demux * id3demux,
    GstCaps * new_caps);
static gboolean gst_id3demux_remove_srcpad (GstID3Demux * id3demux);

static gboolean gst_id3demux_srcpad_event (GstPad * pad, GstEvent * event);
static gboolean gst_id3demux_sink_activate (GstPad * sinkpad);
static GstStateChangeReturn gst_id3demux_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_id3demux_pad_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_id3demux_get_query_types (GstPad * pad);
static gboolean id3demux_get_upstream_size (GstID3Demux * id3demux);
static GstCaps *gst_id3demux_do_typefind (GstID3Demux * id3demux,
    GstBuffer * buffer);
static void gst_id3demux_send_tag_event (GstID3Demux * id3demux);

static GstElementClass *parent_class = NULL;

GType
gst_gst_id3demux_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstID3DemuxClass),
      (GBaseInitFunc) gst_id3demux_base_init,
      NULL,
      (GClassInitFunc) gst_id3demux_class_init,
      NULL,
      NULL,
      sizeof (GstID3Demux),
      0,
      (GInstanceInitFunc) gst_id3demux_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstID3Demux", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_id3demux_base_init (GstID3DemuxClass * klass)
{
  static GstElementDetails plugin_details = {
    "GStreamer ID3 Demuxer",
    "Codec/Demuxer/Metadata",
    "ID3 tag reader and demuxer",
    "Jan Schmidt <thaytan@mad.scientist.com>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_id3demux_class_init (GstID3DemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_id3demux_dispose;

  gobject_class->set_property = gst_id3demux_set_property;
  gobject_class->get_property = gst_id3demux_get_property;

  gstelement_class->change_state = gst_id3demux_change_state;

  g_object_class_install_property (gobject_class, ARG_PREFER_V1,
      g_param_spec_boolean ("prefer-v1", "Prefer version 1 tag",
          "Prefer tags from ID3v1 tag at end of file", FALSE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
gst_id3demux_reset (GstID3Demux * id3demux)
{
  id3demux->strip_start = 0;
  id3demux->strip_end = 0;
  id3demux->upstream_size = -1;
  id3demux->state = GST_ID3DEMUX_READID3V2;
  id3demux->send_tag_event = FALSE;

  gst_buffer_replace (&(id3demux->collect), NULL);
  gst_caps_replace (&(id3demux->src_caps), NULL);

  gst_id3demux_remove_srcpad (id3demux);

  if (id3demux->event_tags) {
    gst_tag_list_free (id3demux->event_tags);
    id3demux->event_tags = NULL;
  }
  if (id3demux->parsed_tags) {
    gst_tag_list_free (id3demux->parsed_tags);
    id3demux->parsed_tags = NULL;
  }

}

static void
gst_id3demux_init (GstID3Demux * id3demux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (id3demux);

  id3demux->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_activate_function (id3demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_id3demux_sink_activate));
  gst_pad_set_chain_function (id3demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_id3demux_chain));
  gst_element_add_pad (GST_ELEMENT (id3demux), id3demux->sinkpad);

  id3demux->prefer_v1 = FALSE;
  gst_id3demux_reset (id3demux);
}

static void
gst_id3demux_dispose (GObject * object)
{
  GstID3Demux *id3demux = GST_ID3DEMUX (object);

  gst_id3demux_reset (id3demux);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_id3demux_add_srcpad (GstID3Demux * id3demux, GstCaps * new_caps)
{
  GstPad *srcpad = NULL;

  if (id3demux->src_caps == NULL ||
      !gst_caps_is_equal (new_caps, id3demux->src_caps)) {

    gst_caps_replace (&(id3demux->src_caps), new_caps);
    if (id3demux->srcpad != NULL) {
      GST_DEBUG_OBJECT (id3demux, "Changing src pad caps to %" GST_PTR_FORMAT,
          id3demux->src_caps);

      gst_pad_set_caps (id3demux->srcpad, id3demux->src_caps);
    }
  } else {
    /* Caps never changed */
    gst_caps_unref (new_caps);
  }

  if (id3demux->srcpad == NULL) {
    srcpad = id3demux->srcpad =
        gst_pad_new_from_template (gst_element_class_get_pad_template
        (GST_ELEMENT_GET_CLASS (id3demux), "src"), "src");
    g_return_val_if_fail (id3demux->srcpad != NULL, FALSE);

    gst_pad_set_query_type_function (id3demux->srcpad,
        GST_DEBUG_FUNCPTR (gst_id3demux_get_query_types));
    gst_pad_set_query_function (id3demux->srcpad,
        GST_DEBUG_FUNCPTR (gst_id3demux_pad_query));
    gst_pad_set_event_function (id3demux->srcpad,
        GST_DEBUG_FUNCPTR (gst_id3demux_srcpad_event));
    gst_pad_set_activatepull_function (id3demux->srcpad,
        GST_DEBUG_FUNCPTR (gst_id3demux_src_activate_pull));
    gst_pad_set_checkgetrange_function (id3demux->srcpad,
        GST_DEBUG_FUNCPTR (gst_id3demux_src_checkgetrange));
    gst_pad_set_getrange_function (id3demux->srcpad,
        GST_DEBUG_FUNCPTR (gst_id3demux_src_getrange));

    gst_pad_use_fixed_caps (id3demux->srcpad);

    if (id3demux->src_caps)
      gst_pad_set_caps (id3demux->srcpad, id3demux->src_caps);

    GST_DEBUG_OBJECT (id3demux, "Adding src pad with caps %" GST_PTR_FORMAT,
        id3demux->src_caps);

    gst_object_ref (id3demux->srcpad);
    return gst_element_add_pad (GST_ELEMENT (id3demux), id3demux->srcpad);
  }

  return TRUE;
}

static gboolean
gst_id3demux_remove_srcpad (GstID3Demux * id3demux)
{
  if (id3demux->srcpad != NULL) {
    GST_DEBUG_OBJECT (id3demux, "Removing src pad");
    g_return_val_if_fail (gst_element_remove_pad (GST_ELEMENT (id3demux),
            id3demux->srcpad), FALSE);
    gst_object_unref (id3demux->srcpad);
    id3demux->srcpad = NULL;
  }

  return TRUE;
};

static gboolean
gst_id3demux_trim_buffer (GstID3Demux * id3demux, GstBuffer ** buf_ref)
{
  GstBuffer *buf = *buf_ref;

  guint trim_start = 0;
  guint out_size = GST_BUFFER_SIZE (buf);
  guint64 out_offset = GST_BUFFER_OFFSET (buf);
  gboolean need_sub = FALSE;

  /* Adjust offset and length */
  if (!GST_BUFFER_OFFSET_IS_VALID (buf)) {
    /* Can't change anything without an offset */
    return TRUE;
  }

  /* If the buffer crosses the ID3v1 tag at the end of file, trim it */
  if (id3demux->strip_end > 0) {
    if (id3demux_get_upstream_size (id3demux)) {
      guint64 v1tag_offset = id3demux->upstream_size - id3demux->strip_end;

      if (out_offset >= v1tag_offset) {
        GST_DEBUG_OBJECT (id3demux, "Buffer is past the end of the data");
        goto no_out_buffer;
      }

      if (out_offset + out_size > v1tag_offset) {
        out_size = v1tag_offset - out_offset;
        need_sub = TRUE;
      }
    }
  }

  if (id3demux->strip_start > 0) {
    /* If the buffer crosses the ID3v2 tag at the start of file, trim it */
    if (out_offset <= id3demux->strip_start) {
      if (out_offset + out_size <= id3demux->strip_start) {
        GST_DEBUG_OBJECT (id3demux, "Buffer is before the start of the data");
        goto no_out_buffer;
      }

      trim_start = id3demux->strip_start - out_offset;
      out_size -= trim_start;
      out_offset = 0;
    } else {
      out_offset -= id3demux->strip_start;
    }
    need_sub = TRUE;
  }

  g_assert (out_size > 0);

  if (need_sub == TRUE) {
    if (out_size != GST_BUFFER_SIZE (buf) || !gst_buffer_is_writable (buf)) {
      GstBuffer *sub;

      GST_DEBUG_OBJECT (id3demux, "Sub-buffering to trim size %d offset %"
          G_GINT64_FORMAT " to %d offset %" G_GINT64_FORMAT,
          GST_BUFFER_SIZE (buf), GST_BUFFER_OFFSET (buf), out_size, out_offset);

      sub = gst_buffer_create_sub (buf, trim_start, out_size);
      g_return_val_if_fail (sub != NULL, FALSE);
      gst_buffer_unref (buf);
      *buf_ref = buf = sub;
    } else {
      GST_DEBUG_OBJECT (id3demux, "Adjusting buffer from size %d offset %"
          G_GINT64_FORMAT " to %d offset %" G_GINT64_FORMAT,
          GST_BUFFER_SIZE (buf), GST_BUFFER_OFFSET (buf), out_size, out_offset);
    }

    GST_BUFFER_OFFSET (buf) = out_offset;
    GST_BUFFER_OFFSET_END (buf) = out_offset + out_size;
    gst_buffer_set_caps (buf, id3demux->src_caps);
  }

  return TRUE;
no_out_buffer:
  gst_buffer_unref (buf);
  *buf_ref = NULL;
  return TRUE;
}

static GstFlowReturn
gst_id3demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstID3Demux *id3demux;

  id3demux = GST_ID3DEMUX (GST_PAD_PARENT (pad));
  g_return_val_if_fail (GST_IS_ID3DEMUX (id3demux), GST_FLOW_ERROR);

  if (id3demux->collect == NULL) {
    id3demux->collect = buf;
  } else {
    id3demux->collect = gst_buffer_join (id3demux->collect, buf);
  }
  buf = NULL;

  switch (id3demux->state) {
    case GST_ID3DEMUX_READID3V2:
      /* If we receive a buffer that's from the middle of the file, 
       * we can't read tags so move to typefinding */
      if (GST_BUFFER_OFFSET (id3demux->collect) != 0) {
        GST_DEBUG_OBJECT (id3demux,
            "Received buffer from non-zero offset. Can't read tags");
        id3demux->state = GST_ID3DEMUX_TYPEFINDING;
        break;
      } else {
        ID3TagsResult tag_result;

        tag_result = id3demux_read_id3v2_tag (id3demux->collect,
            &id3demux->strip_start, &id3demux->parsed_tags);
        if (tag_result == ID3TAGS_MORE_DATA)
          break;                /* Go get more data and try again */
        else if (tag_result == ID3TAGS_BROKEN_TAG)
          GST_WARNING_OBJECT (id3demux,
              "Ignoring broken ID3v2 tag of size %d", id3demux->strip_start);
        else
          GST_DEBUG_OBJECT (id3demux, "Found an ID3v2 tag of size %d\n",
              id3demux->strip_start);

        id3demux->state = GST_ID3DEMUX_TYPEFINDING;
        id3demux->send_tag_event = TRUE;
      }

      /* Fall-through */
    case GST_ID3DEMUX_TYPEFINDING:{
      GstCaps *caps;
      GstBuffer *typefind_buf = NULL;

      if (GST_BUFFER_SIZE (id3demux->collect) < ID3_TYPE_FIND_MIN_SIZE)
        break;                  /* Go get more data first */

      GST_DEBUG_OBJECT (id3demux, "Typefinding with size %d",
          GST_BUFFER_SIZE (id3demux->collect));

      /* Trim the buffer and adjust offset for typefinding */
      typefind_buf = id3demux->collect;
      gst_buffer_ref (typefind_buf);
      if (!gst_id3demux_trim_buffer (id3demux, &typefind_buf))
        return GST_FLOW_ERROR;

      caps = gst_id3demux_do_typefind (id3demux, typefind_buf);

      if (caps == NULL) {
        if (GST_BUFFER_SIZE (typefind_buf) < ID3_TYPE_FIND_MAX_SIZE) {
          /* Just break for more data */
          gst_buffer_unref (typefind_buf);
          return GST_FLOW_OK;
        }

        /* We failed typefind */
        GST_ELEMENT_ERROR (id3demux, CORE, CAPS, (NULL), ("no caps found"));
        gst_buffer_unref (typefind_buf);
        gst_buffer_unref (id3demux->collect);
        id3demux->collect = NULL;
        return GST_FLOW_ERROR;
      }
      gst_buffer_unref (typefind_buf);

      if (!gst_id3demux_add_srcpad (id3demux, caps)) {
        GST_DEBUG_OBJECT (id3demux, "Failed to add srcpad");
        gst_caps_unref (caps);
        goto error;
      }
      gst_caps_unref (caps);

      /* Move onto streaming and fall-through to push out existing
       * data */
      id3demux->state = GST_ID3DEMUX_STREAMING;
      /* fall-through */
    }
    case GST_ID3DEMUX_STREAMING:{
      GstBuffer *outbuf = NULL;

      if (id3demux->send_tag_event) {
        gst_id3demux_send_tag_event (id3demux);
        id3demux->send_tag_event = FALSE;
      }

      /* Trim the buffer and adjust offset */
      if (id3demux->collect) {
        outbuf = id3demux->collect;
        id3demux->collect = NULL;
        if (!gst_id3demux_trim_buffer (id3demux, &outbuf))
          return GST_FLOW_ERROR;
      }
      if (outbuf) {
        if (G_UNLIKELY (id3demux->srcpad == NULL)) {
          gst_buffer_unref (outbuf);
          return GST_FLOW_ERROR;
        }

        GST_DEBUG_OBJECT (id3demux, "Pushing buffer %p", outbuf);
        return gst_pad_push (id3demux->srcpad, outbuf);
      }
    }
  }
  return GST_FLOW_OK;

error:
  GST_DEBUG_OBJECT (id3demux, "error in chain function");

  return GST_FLOW_ERROR;
}

static void
gst_id3demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstID3Demux *id3demux;

  g_return_if_fail (GST_IS_ID3DEMUX (object));
  id3demux = GST_ID3DEMUX (object);

  switch (prop_id) {
    case ARG_PREFER_V1:
      id3demux->prefer_v1 = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_id3demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstID3Demux *id3demux;

  g_return_if_fail (GST_IS_ID3DEMUX (object));
  id3demux = GST_ID3DEMUX (object);

  switch (prop_id) {
    case ARG_PREFER_V1:
      g_value_set_boolean (value, id3demux->prefer_v1);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
id3demux_get_upstream_size (GstID3Demux * id3demux)
{
  GstQuery *query;
  GstPad *peer = NULL;
  GstFormat format;
  gint64 result;
  gboolean res = FALSE;

  /* Short-cut if we already queried upstream */
  if (id3demux->upstream_size > 0)
    return TRUE;

  if ((peer = gst_pad_get_peer (id3demux->sinkpad)) == NULL)
    return FALSE;

  query = gst_query_new_duration (GST_FORMAT_BYTES);
  gst_query_set_duration (query, GST_FORMAT_BYTES, -1);

  if (!gst_pad_query (peer, query))
    goto out;

  gst_query_parse_duration (query, &format, &result);

  if (format != GST_FORMAT_BYTES || result == -1)
    goto out;

  id3demux->upstream_size = result;
  res = TRUE;

out:
  gst_object_unref (peer);
  return res;
}

static gboolean
gst_id3demux_srcpad_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstID3Demux *id3demux = GST_ID3DEMUX (GST_PAD_PARENT (pad));

  /* Handle SEEK events, with adjusted byte offsets and sizes. */

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekType cur_type, stop_type;
      GstSeekFlags flags;
      gint64 cur, stop;

      gst_event_parse_seek (event, &rate, &format, &flags,
          &cur_type, &cur, &stop_type, &stop);

      if (format == GST_FORMAT_BYTES &&
          id3demux->state == GST_ID3DEMUX_STREAMING &&
          gst_pad_is_linked (id3demux->sinkpad)) {
        GstEvent *upstream;

        switch (cur_type) {
          case GST_SEEK_TYPE_SET:
            cur += id3demux->strip_start;
            break;
          case GST_SEEK_TYPE_CUR:
            break;
          case GST_SEEK_TYPE_END:
            cur += id3demux->strip_end;
            break;
          default:
            g_assert_not_reached ();
            break;
        }
        switch (stop_type) {
          case GST_SEEK_TYPE_SET:
            stop += id3demux->strip_start;
            break;
          case GST_SEEK_TYPE_CUR:
            break;
          case GST_SEEK_TYPE_END:
            stop += id3demux->strip_end;
            break;
          default:
            break;
        }
        upstream = gst_event_new_seek (rate, format, flags,
            cur_type, cur, stop_type, stop);
        res = gst_pad_push_event (id3demux->sinkpad, upstream);
      }
      break;
    }
    default:
      break;
  }

  gst_event_unref (event);
  return res;
}

/* Read and interpret any ID3v1 tag when activating in pull_range */
static gboolean
gst_id3demux_read_id3v1 (GstID3Demux * id3demux, GstTagList ** tags)
{
  GstBuffer *buffer = NULL;
  gboolean res = FALSE;
  ID3TagsResult tag_res;
  GstFlowReturn flow_ret;
  guint64 id3v1_offset;

  if (id3demux->upstream_size < ID3V1_TAG_SIZE)
    return TRUE;
  id3v1_offset = id3demux->upstream_size - ID3V1_TAG_SIZE;

  flow_ret = gst_pad_pull_range (id3demux->sinkpad, id3v1_offset,
      ID3V1_TAG_SIZE, &buffer);
  if (flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (id3demux,
        "Could not read data from start of file ret=%d", flow_ret);
    goto beach;
  }

  if (GST_BUFFER_SIZE (buffer) != ID3V1_TAG_SIZE) {
    GST_DEBUG_OBJECT (id3demux,
        "Only managed to read %u bytes from file - not an ID3 file",
        GST_BUFFER_SIZE (buffer));
    goto beach;
  }

  tag_res = id3demux_read_id3v1_tag (buffer, &id3demux->strip_end, tags);
  if (tag_res == ID3TAGS_READ_TAG) {
    GST_DEBUG_OBJECT (id3demux, "Read ID3v1 tag");
    res = TRUE;
  } else if (tag_res == ID3TAGS_BROKEN_TAG) {
    GST_WARNING_OBJECT (id3demux, "Ignoring broken ID3v1 tag");
    res = TRUE;
  }
beach:
  if (buffer)
    gst_buffer_unref (buffer);
  return res;
}

/* Read and interpret any ID3v2 tag when activating in pull_range */
static gboolean
gst_id3demux_read_id3v2 (GstID3Demux * id3demux, GstTagList ** tags)
{
  GstBuffer *buffer = NULL;
  gboolean res = FALSE;
  ID3TagsResult tag_res;
  GstFlowReturn flow_ret;

  /* Handle ID3V2 tag. Try with 4kB to start with */
  flow_ret = gst_pad_pull_range (id3demux->sinkpad, 0, 4096, &buffer);
  if (flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (id3demux,
        "Could not read data from start of file ret=%d", flow_ret);
    goto beach;
  }

  if (GST_BUFFER_SIZE (buffer) < ID3V2_HDR_SIZE) {
    GST_DEBUG_OBJECT (id3demux,
        "Only managed to read %u bytes from file - not an ID3 file",
        GST_BUFFER_SIZE (buffer));
    goto beach;
  }

  tag_res = id3demux_read_id3v2_tag (buffer, &id3demux->strip_start, tags);
  if (tag_res == ID3TAGS_MORE_DATA) {
    /* Need more data to interpret the tag */
    if (buffer) {
      gst_buffer_unref (buffer);
      buffer = NULL;
    }
    g_assert (id3demux->strip_start > ID3V2_HDR_SIZE);

    GST_DEBUG_OBJECT (id3demux, "Reading %u bytes to decode ID3v2",
        id3demux->strip_start);
    flow_ret = gst_pad_pull_range (id3demux->sinkpad, 0, id3demux->strip_start,
        &buffer);
    if (flow_ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (id3demux,
          "Could not read data from start of file ret=%d", flow_ret);
      goto beach;
    }
    tag_res = id3demux_read_id3v2_tag (buffer, &id3demux->strip_start, tags);
  }

  if (tag_res == ID3TAGS_READ_TAG) {
    res = TRUE;
    GST_DEBUG_OBJECT (id3demux, "Read ID3v2 tag of size %d",
        id3demux->strip_start);
  } else if (tag_res == ID3TAGS_BROKEN_TAG) {
    res = TRUE;
    GST_WARNING_OBJECT (id3demux, "Ignoring broken ID3v2 tag of size %d",
        id3demux->strip_start);
  }
beach:
  if (buffer)
    gst_buffer_unref (buffer);
  return res;
}

/* This function operates similarly to gst_type_find_element_activate
 * in the typefind element
 * 1. try to activate in pull mode. if not, switch to push and succeed.
 * 2. try to read tags in pull mode
 * 3. typefind the contents
 * 4. deactivate pull mode.
 * 5. if we didn't find any caps, fail.
 * 6. Add the srcpad
 * 7. if the sink pad is activated, we are in pull mode. succeed.
 *    otherwise activate both pads in push mode and succeed.
 */
static gboolean
gst_id3demux_sink_activate (GstPad * sinkpad)
{
  GstID3Demux *id3demux = GST_ID3DEMUX (GST_PAD_PARENT (sinkpad));
  gboolean ret = FALSE;
  GstBuffer *buf = NULL;
  GstCaps *caps = NULL;
  GstFlowReturn flow_ret;

  /* 1: */
  /* If we can activate pull_range upstream, then read any ID3v1 and ID3v2
   * tags, otherwise activate in push mode and the chain function will 
   * collect buffers, read the ID3v2 tag and output a buffer to end
   * preroll.
   */
  if (!gst_pad_check_pull_range (sinkpad) ||
      !gst_pad_activate_pull (sinkpad, TRUE)) {
    GST_DEBUG_OBJECT (id3demux,
        "No pull mode. Changing to push, but won't be able to read ID3v1 tags");
    id3demux->state = GST_ID3DEMUX_READID3V2;
    return gst_pad_activate_push (sinkpad, TRUE);
  }

  /* Look for tags at start and end of file */
  GST_DEBUG_OBJECT (id3demux, "Activated pull mode. Looking for tags");
  if (!id3demux_get_upstream_size (id3demux))
    return FALSE;

  id3demux->strip_start = 0;
  id3demux->strip_end = 0;


  if (id3demux->prefer_v1) {
    if (!gst_id3demux_read_id3v2 (id3demux, &(id3demux->parsed_tags)))
      return FALSE;
    if (!gst_id3demux_read_id3v1 (id3demux, &(id3demux->parsed_tags)))
      return FALSE;
  } else {
    if (!gst_id3demux_read_id3v1 (id3demux, &(id3demux->parsed_tags)))
      return FALSE;
    if (!gst_id3demux_read_id3v2 (id3demux, &(id3demux->parsed_tags)))
      return FALSE;
  }
  if (id3demux->parsed_tags != NULL) {
    id3demux->send_tag_event = TRUE;
  }

  flow_ret =
      gst_id3demux_read_range (id3demux, 0, ID3_TYPE_FIND_MAX_SIZE, &buf);
  if (flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (id3demux, "Could not read data from start of file ret=%d",
        flow_ret);
    goto done_activate;
  }

  caps = gst_id3demux_do_typefind (id3demux, buf);
  gst_buffer_unref (buf);
  buf = NULL;

  /* 4 - Deactivate pull mode */
  if (!gst_pad_activate_pull (sinkpad, FALSE)) {
    if (caps)
      gst_caps_unref (caps);
    GST_DEBUG_OBJECT (id3demux,
        "Could not deactivate sinkpad after reading tags");
    return FALSE;
  }

  /* 5 - If we didn't find the caps, fail */
  if (caps == NULL) {
    GST_DEBUG_OBJECT (id3demux, "Could not detect type of contents");
    goto done_activate;
  }

  /* tag reading and typefinding were already done, don't do them again in
     the chain function if we end up in push mode */
  id3demux->state = GST_ID3DEMUX_STREAMING;

  /* 6 Add the srcpad for output now we know caps. */
  /* add_srcpad takes ownership of the caps */
  if (!gst_id3demux_add_srcpad (id3demux, caps)) {
    GST_DEBUG_OBJECT (id3demux, "Could not add source pad");
    goto done_activate;
  }

  /* 7 - if the sinkpad is active, it was done by downstream so we're 
   * done, otherwise switch to push */
  ret = TRUE;
  if (!gst_pad_is_active (sinkpad)) {
    ret = gst_pad_activate_push (id3demux->srcpad, TRUE);
    ret &= gst_pad_activate_push (sinkpad, TRUE);
  }

done_activate:
  if (buf)
    gst_buffer_unref (buf);

  return ret;
}

static gboolean
gst_id3demux_src_activate_pull (GstPad * pad, gboolean active)
{
  GstID3Demux *id3demux = GST_ID3DEMUX (GST_PAD_PARENT (pad));

  return gst_pad_activate_pull (id3demux->sinkpad, active);
}

static gboolean
gst_id3demux_src_checkgetrange (GstPad * srcpad)
{
  GstID3Demux *id3demux = GST_ID3DEMUX (GST_PAD_PARENT (srcpad));

  return gst_pad_check_pull_range (id3demux->sinkpad);
}

static GstFlowReturn
gst_id3demux_read_range (GstID3Demux * id3demux,
    guint64 offset, guint length, GstBuffer ** buffer)
{
  GstFlowReturn ret;
  guint64 in_offset;
  guint in_length;

  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  /* Adjust offset and length of the request to trim off ID3 information. 
   * For the returned buffer, adjust the output offset to match what downstream
   * should see */
  in_offset = offset + id3demux->strip_start;

  if (!id3demux_get_upstream_size (id3demux))
    return GST_FLOW_ERROR;

  if (in_offset + length >= id3demux->upstream_size - id3demux->strip_end)
    in_length = id3demux->upstream_size - id3demux->strip_end - in_offset;
  else
    in_length = length;

  ret = gst_pad_pull_range (id3demux->sinkpad, in_offset, in_length, buffer);

  if (ret == GST_FLOW_OK && *buffer) {
    if (!gst_id3demux_trim_buffer (id3demux, buffer))
      goto error;
  }

  return ret;
error:
  if (*buffer != NULL) {
    gst_buffer_unref (buffer);
    *buffer = NULL;
  }
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_id3demux_src_getrange (GstPad * srcpad,
    guint64 offset, guint length, GstBuffer ** buffer)
{
  GstID3Demux *id3demux = GST_ID3DEMUX (GST_PAD_PARENT (srcpad));

  if (id3demux->send_tag_event) {
    gst_id3demux_send_tag_event (id3demux);
    id3demux->send_tag_event = FALSE;
  }
  return gst_id3demux_read_range (id3demux, offset, length, buffer);
}

static GstStateChangeReturn
gst_id3demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstID3Demux *id3demux = GST_ID3DEMUX (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_id3demux_reset (id3demux);
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
gst_id3demux_pad_query (GstPad * pad, GstQuery * query)
{
  /* For a position or duration query, adjust the returned
   * bytes to strip off the id3v1 and id3v2 areas */

  GstID3Demux *id3demux = GST_ID3DEMUX (GST_PAD_PARENT (pad));
  GstPad *peer = NULL;
  GstFormat format;
  gint64 result;

  if ((peer = gst_pad_get_peer (id3demux->sinkpad)) == NULL)
    return FALSE;

  if (!gst_pad_query (peer, query)) {
    gst_object_unref (peer);
    return FALSE;
  }

  gst_object_unref (peer);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gst_query_parse_position (query, &format, &result);
      if (format == GST_FORMAT_BYTES) {
        result -= id3demux->strip_start;
        gst_query_set_position (query, format, result);
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
      gst_query_parse_duration (query, &format, &result);
      if (format == GST_FORMAT_BYTES) {
        result -= id3demux->strip_start + id3demux->strip_end;
        gst_query_set_duration (query, format, result);
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static const GstQueryType *
gst_id3demux_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return types;
}

typedef struct
{
  guint best_probability;
  GstCaps *caps;
  GstBuffer *buffer;
}

SimpleTypeFind;
guint8 *
simple_find_peek (gpointer data, gint64 offset, guint size)
{
  SimpleTypeFind *find = (SimpleTypeFind *) data;

  if (offset < 0)
    return NULL;

  if (GST_BUFFER_SIZE (find->buffer) >= offset + size) {
    return GST_BUFFER_DATA (find->buffer) + offset;
  }
  return NULL;
}
static void
simple_find_suggest (gpointer data, guint probability, const GstCaps * caps)
{
  SimpleTypeFind *find = (SimpleTypeFind *) data;

  if (probability > find->best_probability) {
    GstCaps *copy = gst_caps_copy (caps);

    gst_caps_replace (&find->caps, copy);
    gst_caps_unref (copy);
    find->best_probability = probability;
  }
}

static GstCaps *
gst_id3demux_do_typefind (GstID3Demux * id3demux, GstBuffer * buffer)
{
  GList *walk, *type_list;
  SimpleTypeFind find;
  GstTypeFind gst_find;

  walk = type_list = gst_type_find_factory_get_list ();

  find.buffer = buffer;
  find.best_probability = 0;
  find.caps = NULL;
  gst_find.data = &find;
  gst_find.peek = simple_find_peek;
  gst_find.get_length = NULL;
  gst_find.suggest = simple_find_suggest;
  while (walk) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (walk->data);

    gst_type_find_factory_call_function (factory, &gst_find);
    if (find.best_probability >= GST_TYPE_FIND_MAXIMUM)
      break;
    walk = g_list_next (walk);
  }
  gst_plugin_feature_list_free (type_list);
  if (find.best_probability > 0) {
    GST_DEBUG ("Found caps %" GST_PTR_FORMAT " with buf size %u", find.caps,
        GST_BUFFER_SIZE (buffer));
    return find.caps;
  }

  return NULL;
}

static void
gst_id3demux_send_tag_event (GstID3Demux * id3demux)
{
  /* FIXME: what's the correct merge mode? Docs need to tell... */
  GstTagList *merged = gst_tag_list_merge (id3demux->event_tags,
      id3demux->parsed_tags, GST_TAG_MERGE_KEEP);

  if (id3demux->parsed_tags)
    gst_element_post_message (GST_ELEMENT (id3demux),
        gst_message_new_tag (GST_OBJECT (id3demux),
            gst_tag_list_copy (id3demux->parsed_tags)));

  if (merged) {
    GstEvent *event = gst_event_new_tag (merged);

    GST_EVENT_TIMESTAMP (event) = 0;
    GST_DEBUG_OBJECT (id3demux, "Sending tag event on src pad");
    gst_pad_push_event (id3demux->srcpad, event);
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (id3demux_debug, "id3demux", 0,
      "GStreamer ID3 tag demuxer");

  return gst_element_register (plugin, "id3demux",
      GST_RANK_PRIMARY, GST_TYPE_ID3DEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "id3demux",
    "Demux ID3v1 and ID3v2 tags from a file",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
