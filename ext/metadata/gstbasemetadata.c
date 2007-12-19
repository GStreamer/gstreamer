/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:metadatabase-metadata
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc location=./test.jpeg ! metadatabase ! fakesink silent=TRUE
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstbasemetadata.h"

#include "metadataexif.h"

#include "metadataiptc.h"

#include "metadataxmp.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_base_metadata_debug);
#define GST_CAT_DEFAULT gst_base_metadata_debug

#define GOTO_DONE_IF_NULL(ptr) do { if ( NULL == (ptr) ) goto done; } while(FALSE)
#define GOTO_DONE_IF_NULL_AND_FAIL(ptr, ret) do { if ( NULL == (ptr) ) { (ret) = FALSE; goto done; } } while(FALSE)

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_EXIF,
  ARG_IPTC,
  ARG_XMP
};

static GstBaseMetadataClass *parent_class = NULL;

static void gst_base_metadata_dispose (GObject * object);

static void gst_base_metadata_finalize (GObject * object);

static void gst_base_metadata_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_metadata_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn
gst_base_metadata_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_base_metadata_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_metadata_sink_event (GstPad * pad, GstEvent * event);

static GstFlowReturn gst_base_metadata_chain (GstPad * pad, GstBuffer * buf);

static gboolean gst_base_metadata_checkgetrange (GstPad * srcpad);

static GstFlowReturn
gst_base_metadata_get_range (GstPad * pad, guint64 offset_orig, guint size,
    GstBuffer ** buf);

static gboolean gst_base_metadata_sink_activate (GstPad * pad);

static gboolean
gst_base_metadata_src_activate_pull (GstPad * pad, gboolean active);

static gboolean gst_base_metadata_pull_range_base (GstBaseMetadata * filter);

static void gst_base_metadata_init_members (GstBaseMetadata * filter);
static void gst_base_metadata_dispose_members (GstBaseMetadata * filter);

static gboolean gst_base_metadata_configure_caps (GstBaseMetadata * filter);

static int
gst_base_metadata_parse (GstBaseMetadata * filter, const guint8 * buf,
    guint32 size);

static const GstQueryType *gst_base_metadata_get_query_types (GstPad * pad);

static gboolean gst_base_metadata_src_query (GstPad * pad, GstQuery * query);

static void gst_base_metadata_base_init (gpointer gclass);
static void gst_base_metadata_class_init (GstBaseMetadataClass * klass);
static void
gst_base_metadata_init (GstBaseMetadata * filter,
    GstBaseMetadataClass * gclass);

GType
gst_base_metadata_get_type (void)
{
  static GType base_metadata_type = 0;

  if (G_UNLIKELY (base_metadata_type == 0)) {
    static const GTypeInfo base_metadata_info = {
      sizeof (GstBaseMetadataClass),
      (GBaseInitFunc) gst_base_metadata_base_init,
      NULL,
      (GClassInitFunc) gst_base_metadata_class_init,
      NULL,
      NULL,
      sizeof (GstBaseMetadata),
      0,
      (GInstanceInitFunc) gst_base_metadata_init,
    };

    base_metadata_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseMetadata", &base_metadata_info, G_TYPE_FLAG_ABSTRACT);
  }
  return base_metadata_type;
}


static void
gst_base_metadata_base_init (gpointer gclass)
{
  GST_DEBUG_CATEGORY_INIT (gst_base_metadata_debug, "basemetadata", 0,
      "basemetadata element");
}

/* initialize the plugin's class */
static void
gst_base_metadata_class_init (GstBaseMetadataClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_base_metadata_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_metadata_finalize);

  gobject_class->set_property = gst_base_metadata_set_property;
  gobject_class->get_property = gst_base_metadata_get_property;

  g_object_class_install_property (gobject_class, ARG_EXIF,
      g_param_spec_boolean ("exif", "EXIF", "Send EXIF metadata ?",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_IPTC,
      g_param_spec_boolean ("iptc", "IPTC", "Send IPTC metadata ?",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_XMP,
      g_param_spec_boolean ("xmp", "XMP", "Send XMP metadata ?",
          TRUE, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_base_metadata_change_state;

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_base_metadata_init (GstBaseMetadata * filter, GstBaseMetadataClass * gclass)
{
  /* sink pad */

  filter->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (gclass), "sink"), "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gclass->set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gclass->get_sink_caps));
  gst_pad_set_event_function (filter->sinkpad, gst_base_metadata_sink_event);
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_metadata_chain));
  gst_pad_set_activate_function (filter->sinkpad,
      gst_base_metadata_sink_activate);

  /* source pad */

  filter->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (gclass), "src"), "src");
  gst_pad_set_getcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gclass->get_src_caps));
  gst_pad_set_event_function (filter->srcpad, gst_base_metadata_src_event);
  gst_pad_set_query_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_metadata_src_query));
  gst_pad_set_query_type_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_metadata_get_query_types));
  gst_pad_use_fixed_caps (filter->srcpad);

  gst_pad_set_checkgetrange_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_metadata_checkgetrange));
  gst_pad_set_getrange_function (filter->srcpad, gst_base_metadata_get_range);

  gst_pad_set_activatepull_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_metadata_src_activate_pull));
  /* addind pads */

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  metadataparse_xmp_init ();
  /* init members */

  filter->options = META_OPT_EXIF | META_OPT_IPTC | META_OPT_XMP;

  gst_base_metadata_init_members (filter);

}

static void
gst_base_metadata_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseMetadata *filter = GST_BASE_METADATA (object);

  switch (prop_id) {
    case ARG_EXIF:
      if (g_value_get_boolean (value))
        filter->options |= META_OPT_EXIF;
      else
        filter->options &= ~META_OPT_EXIF;
      break;
    case ARG_IPTC:
      if (g_value_get_boolean (value))
        filter->options |= META_OPT_IPTC;
      else
        filter->options &= ~META_OPT_IPTC;
      break;
    case ARG_XMP:
      if (g_value_get_boolean (value))
        filter->options |= META_OPT_XMP;
      else
        filter->options &= ~META_OPT_XMP;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_metadata_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseMetadata *filter = GST_BASE_METADATA (object);

  switch (prop_id) {
    case ARG_EXIF:
      g_value_set_boolean (value, filter->options & META_OPT_EXIF);
      break;
    case ARG_IPTC:
      g_value_set_boolean (value, filter->options & META_OPT_IPTC);
      break;
    case ARG_XMP:
      g_value_set_boolean (value, filter->options & META_OPT_XMP);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

static gboolean
gst_base_metadata_processing (GstBaseMetadata * filter)
{
  gboolean ret = TRUE;
  GstBaseMetadataClass *bclass = GST_BASE_METADATA_GET_CLASS (filter);

  if (filter->need_processing) {
    bclass->processing (filter);
    if (gst_metadata_common_calculate_offsets (&filter->common)) {
      filter->need_processing = FALSE;
    } else {
      ret = FALSE;
    }
  }

  return ret;

}

static gboolean
gst_base_metadata_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseMetadata *filter = NULL;
  gboolean ret = FALSE;
  GstBaseMetadataClass *bclass;

  filter = GST_BASE_METADATA (gst_pad_get_parent (pad));
  bclass = GST_BASE_METADATA_GET_CLASS (filter);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (filter->need_more_data) {
        GST_ELEMENT_WARNING (filter, STREAM, DECODE, (NULL),
            ("Need more data. Unexpected EOS"));
      }
      break;
    case GST_EVENT_TAG:
      break;
    default:
      break;
  }

  ret = bclass->sink_event (pad, event);

  gst_object_unref (filter);

  return ret;

}


static gboolean
gst_base_metadata_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseMetadata *filter = NULL;
  gboolean ret = FALSE;

  filter = GST_BASE_METADATA (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type;
      gint64 start;
      GstSeekType stop_type;
      gint64 stop;

      /* we don't know where are the chunks to be stripped before base */
      if (!gst_base_metadata_processing (filter)) {
        ret = FALSE;
        goto done;
      }

      gst_event_parse_seek (event, &rate, &format, &flags,
          &start_type, &start, &stop_type, &stop);

      switch (format) {
        case GST_FORMAT_BYTES:
          break;
        case GST_FORMAT_PERCENT:
          if (filter->common.duration < 0)
            goto done;
          start = start * filter->common.duration / 100;
          stop = stop * filter->common.duration / 100;
          break;
        default:
          goto done;
      }
      format = GST_FORMAT_BYTES;

      if (start_type == GST_SEEK_TYPE_CUR)
        start = filter->offset + start;
      else if (start_type == GST_SEEK_TYPE_END) {
        if (filter->common.duration < 0)
          goto done;
        start = filter->common.duration + start;
      }
      start_type == GST_SEEK_TYPE_SET;

      if (filter->prepend_buffer) {
        gst_buffer_unref (filter->prepend_buffer);
        filter->prepend_buffer = NULL;
      }

      /* FIXME: related to append */
      filter->offset = start;
      gst_metadata_common_translate_pos_to_orig (&filter->common, start, &start,
          &filter->prepend_buffer);
      filter->offset_orig = start;

      if (stop_type == GST_SEEK_TYPE_CUR)
        stop = filter->offset + stop;
      else if (stop_type == GST_SEEK_TYPE_END) {
        if (filter->common.duration < 0)
          goto done;
        stop = filter->common.duration + stop;
      }
      stop_type == GST_SEEK_TYPE_SET;

      gst_metadata_common_translate_pos_to_orig (&filter->common, stop, &stop,
          NULL);

      gst_event_unref (event);
      event = gst_event_new_seek (rate, format, flags,
          start_type, start, stop_type, stop);

    }
      break;
    default:
      break;
  }

  ret = gst_pad_event_default (pad, event);
  event = NULL;                 /* event has another owner */

done:

  if (event) {
    gst_event_unref (event);
  }

  gst_object_unref (filter);

  return ret;

}

static void
gst_base_metadata_dispose (GObject * object)
{
  GstBaseMetadata *filter = NULL;

  filter = GST_BASE_METADATA (object);

  gst_base_metadata_dispose_members (filter);

  metadataparse_xmp_dispose ();

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_base_metadata_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_metadata_dispose_members (GstBaseMetadata * filter)
{
  gst_metadata_common_dispose (&filter->common);

  if (filter->adapter_parsing) {
    gst_object_unref (filter->adapter_parsing);
    filter->adapter_parsing = NULL;
  }

  if (filter->adapter_holding) {
    gst_object_unref (filter->adapter_holding);
    filter->adapter_holding = NULL;
  }

  if (filter->prepend_buffer) {
    gst_buffer_unref (filter->prepend_buffer);
    filter->prepend_buffer = NULL;
  }
}

static void
gst_base_metadata_init_members (GstBaseMetadata * filter)
{
  filter->need_processing = FALSE;

  filter->adapter_parsing = NULL;
  filter->adapter_holding = NULL;
  filter->next_offset = 0;
  filter->next_size = 0;
  filter->img_type = IMG_NONE;
  filter->offset_orig = 0;
  filter->offset = 0;
  filter->need_more_data = FALSE;

  filter->prepend_buffer = NULL;

  memset (&filter->common, 0x00, sizeof (filter->common));

}

static gboolean
gst_base_metadata_configure_caps (GstBaseMetadata * filter)
{
  GstCaps *caps = NULL;
  gboolean ret = FALSE;
  gchar *mime = NULL;
  GstPad *peer = NULL;

  peer = gst_pad_get_peer (filter->sinkpad);

  switch (filter->img_type) {
    case IMG_JPEG:
      mime = "image/jpeg";
      break;
    case IMG_PNG:
      mime = "image/png";
      break;
    default:
      goto done;
      break;
  }

  caps = gst_caps_new_simple (mime, NULL);

  if (!gst_pad_set_caps (peer, caps)) {
    goto done;
  }

  ret = gst_pad_set_caps (filter->sinkpad, caps);

done:

  if (caps) {
    gst_caps_unref (caps);
    caps = NULL;
  }

  if (peer) {
    gst_object_unref (peer);
    peer = NULL;
  }

  return ret;

}

static const GstQueryType *
gst_base_metadata_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_base_metadata_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_FORMATS,
    0
  };

  return gst_base_metadata_src_query_types;
}

static gboolean
gst_base_metadata_src_query (GstPad * pad, GstQuery * query)
{
  gboolean ret = FALSE;
  GstFormat format;
  GstBaseMetadata *filter = GST_BASE_METADATA (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, NULL);

      if (format == GST_FORMAT_BYTES) {
        gst_query_set_position (query, GST_FORMAT_BYTES, filter->offset);
        ret = TRUE;
      }
      break;
    case GST_QUERY_DURATION:

      if (!gst_base_metadata_processing (filter)) {
        ret = FALSE;
        goto done;
      }

      gst_query_parse_duration (query, &format, NULL);

      if (format == GST_FORMAT_BYTES) {
        if (filter->common.duration >= 0) {
          gst_query_set_duration (query, GST_FORMAT_BYTES,
              filter->common.duration);
          ret = TRUE;
        }
      }
      break;
    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 1, GST_FORMAT_BYTES);
      ret = TRUE;
      break;
    default:
      break;
  }

done:

  gst_object_unref (filter);

  return ret;

}

/*
 * Do parsing step-by-step and reconfigure caps if need
 * return:
 *   META_PARSING_ERROR
 *   META_PARSING_DONE
 *   META_PARSING_NEED_MORE_DATA
 */

static int
gst_base_metadata_parse (GstBaseMetadata * filter, const guint8 * buf,
    guint32 size)
{

  int ret = META_PARSING_ERROR;

  filter->next_offset = 0;
  filter->next_size = 0;

  ret = metadata_parse (&filter->common.metadata, buf, size,
      &filter->next_offset, &filter->next_size);

  if (ret == META_PARSING_ERROR) {
    if (META_DATA_IMG_TYPE (filter->common.metadata) == IMG_NONE) {
      /* image type not recognized */
      GST_ELEMENT_ERROR (filter, STREAM, TYPE_NOT_FOUND, (NULL),
          ("Only jpeg and png are supported"));
      goto done;
    }
  } else if (ret == META_PARSING_NEED_MORE_DATA) {
    filter->need_more_data = TRUE;
  } else {
    filter->common.state = MT_STATE_PARSED;
    filter->need_more_data = FALSE;
    filter->need_processing = TRUE;
  }

  /* reconfigure caps if it is different from type detected by 'base_metadata' function */
  if (filter->img_type != META_DATA_IMG_TYPE (filter->common.metadata)) {
    filter->img_type = META_DATA_IMG_TYPE (filter->common.metadata);
    if (!gst_base_metadata_configure_caps (filter)) {
      GST_ELEMENT_ERROR (filter, STREAM, FORMAT, (NULL),
          ("Couldn't reconfigure caps for %s",
              gst_metadata_common_get_type_name (filter->img_type)));
      ret = META_PARSING_ERROR;
      goto done;
    }
  }

done:

  return ret;

}

/* chain function
 * this function does the actual processing
 */

/* FIXME */
/* Current base is just done before is pull mode could be activated */
/* may be it is possible to base in chain mode by doing some trick with gst-adapter */
/* the pipeline below would be a test for that case */
/* gst-launch-0.10 filesrc location=Exif.jpg ! queue !  metadatabase ! filesink location=gen3.jpg */

static GstFlowReturn
gst_base_metadata_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseMetadata *filter = NULL;
  GstFlowReturn ret = GST_FLOW_ERROR;
  guint32 buf_size = 0;
  guint32 new_buf_size = 0;
  gboolean append = FALSE;

  filter = GST_BASE_METADATA (gst_pad_get_parent (pad));

  if (filter->common.state != MT_STATE_PARSED) {
    guint32 adpt_size = gst_adapter_available (filter->adapter_parsing);

    if (filter->next_offset) {
      if (filter->next_offset >= adpt_size) {
        /* clean adapter */
        gst_adapter_clear (filter->adapter_parsing);
        filter->next_offset -= adpt_size;
        if (filter->next_offset >= GST_BUFFER_SIZE (buf)) {
          /* we don't need data in this buffer */
          filter->next_offset -= GST_BUFFER_SIZE (buf);
        } else {
          GstBuffer *new_buf;

          /* add to adapter just need part from buf */
          new_buf =
              gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buf) -
              filter->next_offset);
          memcpy (GST_BUFFER_DATA (new_buf),
              GST_BUFFER_DATA (buf) + filter->next_offset,
              GST_BUFFER_SIZE (buf) - filter->next_offset);
          filter->next_offset = 0;
          gst_adapter_push (filter->adapter_parsing, new_buf);
        }
      } else {
        /* remove first bytes and add buffer */
        gst_adapter_flush (filter->adapter_parsing, filter->next_offset);
        filter->next_offset = 0;
        gst_adapter_push (filter->adapter_parsing, gst_buffer_copy (buf));
      }
    } else {
      /* just push buffer */
      gst_adapter_push (filter->adapter_parsing, gst_buffer_copy (buf));
    }

    adpt_size = gst_adapter_available (filter->adapter_parsing);

    if (adpt_size && filter->next_size <= adpt_size) {
      const guint8 *new_buf =
          gst_adapter_peek (filter->adapter_parsing, adpt_size);

      if (gst_base_metadata_parse (filter, new_buf,
              adpt_size) == META_PARSING_ERROR) {
        ret = GST_FLOW_ERROR;
        goto done;
      }
    }
  }

  if (filter->common.state == MT_STATE_PARSED) {

    if (!gst_base_metadata_processing (filter)) {
      ret = GST_FLOW_ERROR;
      goto done;
    }

    if (filter->adapter_holding) {
      gst_adapter_push (filter->adapter_holding, buf);
      buf = gst_adapter_take_buffer (filter->adapter_holding,
          gst_adapter_available (filter->adapter_holding));
      g_object_unref (filter->adapter_holding);
      filter->adapter_holding = NULL;
    }

    if (filter->offset_orig + GST_BUFFER_SIZE (buf) ==
        filter->common.duration_orig)
      append = TRUE;

    buf_size = GST_BUFFER_SIZE (buf);

    gst_metadata_common_strip_push_buffer (&filter->common, filter->offset_orig,
        &filter->prepend_buffer, &buf);

    if (buf) {                  /* may be all buffer has been striped */
      gst_buffer_set_caps (buf, GST_PAD_CAPS (filter->srcpad));
      new_buf_size = GST_BUFFER_SIZE (buf);

      ret = gst_pad_push (filter->srcpad, buf);
      buf = NULL;               /* this function don't owner it anymore */
      if (ret != GST_FLOW_OK)
        goto done;
    } else {
      ret = GST_FLOW_OK;
    }

    if (append && filter->common.append_buffer) {
      gst_buffer_set_caps (filter->common.append_buffer,
          GST_PAD_CAPS (filter->srcpad));
      gst_buffer_ref (filter->common.append_buffer);
      ret = gst_pad_push (filter->srcpad, filter->common.append_buffer);
      if (ret != GST_FLOW_OK)
        goto done;
    }

    filter->offset_orig += buf_size;
    filter->offset += new_buf_size;

  } else {
    /* just store while still not based */
    if (!filter->adapter_holding)
      filter->adapter_holding = gst_adapter_new ();
    gst_adapter_push (filter->adapter_holding, buf);
    buf = NULL;
    ret = GST_FLOW_OK;
  }

done:


  if (buf) {
    /* there was an error and buffer wasn't pushed */
    gst_buffer_unref (buf);
    buf = NULL;
  }

  gst_object_unref (filter);

  return ret;

}

static gboolean
gst_base_metadata_pull_range_base (GstBaseMetadata * filter)
{

  int res;
  gboolean ret = TRUE;
  guint32 offset = 0;
  gint64 duration = 0;
  GstFormat format = GST_FORMAT_BYTES;

  if (!(ret =
          gst_pad_query_peer_duration (filter->sinkpad, &format, &duration))) {
    /* this should never happen, but try chain anyway */
    ret = TRUE;
    goto done;
  }
  filter->common.duration_orig = duration;

  if (format != GST_FORMAT_BYTES) {
    /* this should never happen, but try chain anyway */
    ret = TRUE;
    goto done;
  }

  do {
    GstFlowReturn flow;
    GstBuffer *buf = NULL;

    offset += filter->next_offset;

    /* 'filter->next_size' only says the minimum required number of bytes.
       We try provided more bytes (4096) just to avoid a lot of calls to 'metadata_parse'
       returning META_PARSING_NEED_MORE_DATA */
    if (filter->next_size < 4096) {
      if (duration - offset < 4096) {
        /* In case there is no 4096 bytes available upstream.
           It should be done upstream but we do here for safety */
        filter->next_size = duration - offset;
      } else {
        filter->next_size = 4096;
      }
    }

    flow =
        gst_pad_pull_range (filter->sinkpad, offset, filter->next_size, &buf);
    if (GST_FLOW_OK != flow) {
      ret = FALSE;
      goto done;
    }

    res =
        gst_base_metadata_parse (filter, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    if (res == META_PARSING_ERROR) {
      ret = FALSE;
      goto done;
    }

    gst_buffer_unref (buf);

  } while (res == META_PARSING_NEED_MORE_DATA);

done:

  return ret;

}

static gboolean
gst_base_metadata_sink_activate (GstPad * pad)
{
  GstBaseMetadata *filter = NULL;
  gboolean ret = TRUE;


  filter = GST_BASE_METADATA (GST_PAD_PARENT (pad));

  if (!gst_pad_check_pull_range (pad) ||
      !gst_pad_activate_pull (filter->sinkpad, TRUE)) {
    /* FIXME: currently it is not possible to base in chain. Fail here ? */
    /* nothing to be done by now, activate push mode */
    return gst_pad_activate_push (pad, TRUE);
  }

  /* try to base */
  if (filter->common.state == MT_STATE_NULL) {
    ret = gst_base_metadata_pull_range_base (filter);
  }

done:

  if (ret) {
    gst_pad_activate_pull (pad, FALSE);
    gst_pad_activate_push (filter->srcpad, FALSE);
    if (!gst_pad_is_active (pad)) {
      ret = gst_pad_activate_push (filter->srcpad, TRUE);
      ret = ret && gst_pad_activate_push (pad, TRUE);
    }
  }

  return ret;

}

static gboolean
gst_base_metadata_checkgetrange (GstPad * srcpad)
{
  GstBaseMetadata *filter = NULL;

  filter = GST_BASE_METADATA (GST_PAD_PARENT (srcpad));

  return gst_pad_check_pull_range (filter->sinkpad);
}

static GstFlowReturn
gst_base_metadata_get_range (GstPad * pad,
    guint64 offset, guint size, GstBuffer ** buf)
{
  GstBaseMetadata *filter = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  gint64 offset_orig = 0;
  guint size_orig;
  GstBuffer *prepend = NULL;
  gboolean need_append = FALSE;

  filter = GST_BASE_METADATA (GST_PAD_PARENT (pad));

  if (!gst_base_metadata_processing (filter)) {
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (offset + size > filter->common.duration) {
    size = filter->common.duration - offset;
  }

  size_orig = size;

  gst_metadata_common_translate_pos_to_orig (&filter->common, offset,
      &offset_orig, &prepend);

  if (size > 1) {
    gint64 pos;

    pos = offset + size - 1;
    gst_metadata_common_translate_pos_to_orig (&filter->common, pos, &pos,
        NULL);
    size_orig = pos + 1 - offset_orig;
  }

  if (size_orig) {

    ret = gst_pad_pull_range (filter->sinkpad, offset_orig, size_orig, buf);

    if (ret == GST_FLOW_OK && *buf) {
      gst_metadata_common_strip_push_buffer (&filter->common, offset_orig,
          &prepend, buf);

      if (GST_BUFFER_SIZE (*buf) < size) {
        /* need append */
        need_append = TRUE;
      }

    }
  } else {
    *buf = prepend;
  }

done:

  if (need_append) {
    /* FIXME: together with SEEK and
     * gst_metadata_common_translate_pos_to_orig
     * this way if chunk is added in the end we are in trolble
     * ...still not implemented 'cause it will not be the
     * case for the time being
     */
  }

  return ret;

}

static gboolean
gst_base_metadata_src_activate_pull (GstPad * pad, gboolean active)
{
  GstBaseMetadata *filter = NULL;
  gboolean ret;

  filter = GST_BASE_METADATA (gst_pad_get_parent (pad));

  ret = gst_pad_activate_pull (filter->sinkpad, active);

  if (ret && filter->common.state == MT_STATE_NULL) {
    ret = gst_base_metadata_pull_range_base (filter);
  }

  gst_object_unref (filter);

  return ret;
}


static GstStateChangeReturn
gst_base_metadata_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstBaseMetadata *filter = GST_BASE_METADATA (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_base_metadata_init_members (filter);
      filter->adapter_parsing = gst_adapter_new ();
      gst_metadata_common_init (&filter->common, filter->options);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      filter->offset = 0;
      filter->offset_orig = 0;
      if (filter->adapter_parsing) {
        gst_adapter_clear (filter->adapter_parsing);
      }
      if (filter->adapter_holding) {
        gst_adapter_clear (filter->adapter_holding);
      }
      if (filter->common.state != MT_STATE_PARSED) {
        /* cleanup parser */
        gst_metadata_common_dispose (&filter->common);
        gst_metadata_common_init (&filter->common, filter->options);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_base_metadata_dispose_members (filter);
      break;
    default:
      break;
  }

done:

  return ret;
}

void
gst_base_metadata_set_option_flag (GstBaseMetadata * metadata,
    MetaOptions options)
{
  metadata->options |= options;
}

void
gst_base_metadata_unset_option_flag (GstBaseMetadata * metadata,
    MetaOptions options)
{
  metadata->options &= ~options;
}

MetaOptions
gst_base_metadata_get_option_flag (const GstBaseMetadata * metadata)
{
  return metadata->options;
}

void
gst_base_metadata_update_segment_with_new_buffer (GstBaseMetadata * metadata,
    guint8 ** buf, guint32 * size, MetadataChunkType type)
{
  gst_metadata_common_update_segment_with_new_buffer (&metadata->common, buf,
      size, type);
}

void
gst_base_metadata_chunk_array_remove_zero_size (GstBaseMetadata * metadata)
{
  metadata_chunk_array_remove_zero_size (&metadata->common.metadata.
      inject_chunks);
}
