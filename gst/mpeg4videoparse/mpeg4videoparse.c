/* GStreamer
 * Copyright (C) <2008> Mindfruit B.V.
 *   @author Sjoerd Simons <sjoerd@luon.net>
 * Copyright (C) <2007> Julien Moutte <julien@fluendo.com>
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

#include <string.h>
#include "mpeg4videoparse.h"

GST_DEBUG_CATEGORY_STATIC (mpeg4v_parse_debug);
#define GST_CAT_DEFAULT mpeg4v_parse_debug

/* elementfactory information */
static GstElementDetails mpeg4vparse_details =
GST_ELEMENT_DETAILS ("MPEG 4 video elementary stream parser",
    "Codec/Parser/Video",
    "Parses MPEG-4 Part 2 elementary video streams",
    "Julien Moutte <julien@fluendo.com>");

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) 4, "
        "parsed = (boolean) true, " "systemstream = (boolean) false")
    );

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) 4, "
        "parsed = (boolean) false, " "systemstream = (boolean) false")
    );

/* Properties */
#define DEFAULT_PROP_DROP	TRUE

enum
{
  PROP_0,
  PROP_DROP,
  PROP_LAST
};

GST_BOILERPLATE (GstMpeg4VParse, gst_mpeg4vparse, GstElement, GST_TYPE_ELEMENT);

static gboolean
gst_mpeg4vparse_set_new_caps (GstMpeg4VParse * parse,
    guint16 time_increment_resolution, guint16 fixed_time_increment,
    gint aspect_ratio_width, gint aspect_ratio_height, gint width, gint height)
{
  gboolean res;
  GstCaps *out_caps = gst_caps_new_simple ("video/mpeg",
      "mpegversion", G_TYPE_INT, 4,
      "systemstream", G_TYPE_BOOLEAN, FALSE,
      "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (parse->profile != 0) {
    gchar *profile = NULL;

    /* FIXME does it make sense to expose the profile in the caps ? */
    profile = g_strdup_printf ("%d", parse->profile);
    gst_caps_set_simple (out_caps, "profile-level-id",
        G_TYPE_STRING, profile, NULL);
    g_free (profile);
  }

  if (parse->config != NULL) {
    gst_caps_set_simple (out_caps, "codec_data",
        GST_TYPE_BUFFER, parse->config, NULL);
  }

  if (fixed_time_increment != 0) {
    /* we have  a framerate */
    gst_caps_set_simple (out_caps, "framerate",
        GST_TYPE_FRACTION, time_increment_resolution, fixed_time_increment,
        NULL);
    parse->frame_duration = gst_util_uint64_scale_int (GST_SECOND,
        fixed_time_increment, time_increment_resolution);
  } else {
    /* unknown duration */
    parse->frame_duration = 0;
  }

  if (aspect_ratio_width > 0 && aspect_ratio_height > 0) {
    gst_caps_set_simple (out_caps, "pixel-aspect-ratio",
        GST_TYPE_FRACTION, aspect_ratio_width, aspect_ratio_height, NULL);
  }

  if (width > 0 && height > 0) {
    gst_caps_set_simple (out_caps,
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
  }

  GST_DEBUG_OBJECT (parse, "setting downstream caps to %" GST_PTR_FORMAT,
      out_caps);
  res = gst_pad_set_caps (parse->srcpad, out_caps);
  gst_caps_unref (out_caps);

  return res;
}

#define VOS_STARTCODE                   0xB0
#define VOS_ENDCODE                     0xB1
#define USER_DATA_STARTCODE             0xB2
#define GOP_STARTCODE                   0xB3
#define VISUAL_OBJECT_STARTCODE         0xB5
#define VOP_STARTCODE                   0xB6

#define START_MARKER                    0x000001
#define VISUAL_OBJECT_STARTCODE_MARKER  ((START_MARKER << 8) + VISUAL_OBJECT_STARTCODE)
#define USER_DATA_STARTCODE_MARKER      ((START_MARKER << 8) + USER_DATA_STARTCODE)

typedef struct
{
  const guint8 *data;
  /* byte offset */
  gsize offset;
  /* bit offset */
  gsize b_offset;

  /* size in bytes */
  gsize size;
} bitstream_t;

static gboolean
get_bits (bitstream_t * b, int num, guint32 * bits)
{
  *bits = 0;

  if (b->offset + ((b->b_offset + num) / 8) > b->size)
    return FALSE;

  if (b->b_offset + num <= 8) {
    *bits = b->data[b->offset];
    *bits = (*bits >> (8 - num - b->b_offset)) & (((1 << num)) - 1);

    b->offset += (b->b_offset + num) / 8;
    b->b_offset = (b->b_offset + num) % 8;
    return TRUE;
  } else {
    /* going over the edge.. */
    int next;

    next = (8 - b->b_offset);
    do {
      guint32 t;

      if (!get_bits (b, next, &t))
        return FALSE;
      *bits <<= next;
      *bits |= t;
      num -= next;
      next = MIN (8, num);
    } while (num > 0);

    return TRUE;
  }
}

#define GET_BITS(b, num, bits) G_STMT_START { \
  if (!get_bits(b, num, bits))                \
    goto failed;                              \
} G_STMT_END

#define MARKER_BIT(b) G_STMT_START {  \
  guint32 i;                          \
  GET_BITS(b, 1, &i);                 \
  if (i != 0x1)                       \
    goto failed;                      \
} G_STMT_END

static inline gboolean
next_start_code (bitstream_t * b)
{
  guint32 bits;

  GET_BITS (b, 1, &bits);
  if (bits != 0)
    goto failed;

  while (b->b_offset != 0) {
    GET_BITS (b, 1, &bits);
    if (bits != 0x1)
      goto failed;
  }

  return TRUE;

failed:
  return FALSE;
}

static gint aspect_ratio_table[6][2] = { {-1, -1}, {1, 1}, {12, 11},
{10, 11}, {16, 11}, {40, 33}
};

static gboolean
gst_mpeg4vparse_handle_vo (GstMpeg4VParse * parse, const guint8 * data,
    gsize size)
{
  guint32 bits;
  bitstream_t bs = { data, 0, 0, size };
  guint16 time_increment_resolution = 0;
  guint16 fixed_time_increment = 0;
  gint aspect_ratio_width = -1, aspect_ratio_height = -1;
  gint height = -1, width = -1;

  /* expecting a video object startcode */
  GET_BITS (&bs, 32, &bits);
  if (bits > 0x11F)
    goto failed;

  /* expecting a video object layer startcode */
  GET_BITS (&bs, 32, &bits);
  if (bits < 0x120 || bits > 0x12F)
    goto failed;

  /* ignore random accessible vol  and video object type indication */
  GET_BITS (&bs, 9, &bits);

  GET_BITS (&bs, 1, &bits);
  if (bits) {
    /* skip video object layer verid and priority */
    GET_BITS (&bs, 7, &bits);
  }

  /* aspect ratio info */
  GET_BITS (&bs, 4, &bits);
  if (bits == 0)
    goto failed;

  /* check if aspect ratio info  is extended par */
  if (bits == 0xf) {
    GET_BITS (&bs, 8, &bits);
    aspect_ratio_width = bits;
    GET_BITS (&bs, 8, &bits);
    aspect_ratio_height = bits;
  } else if (bits < 0x6) {
    aspect_ratio_width = aspect_ratio_table[bits][0];
    aspect_ratio_height = aspect_ratio_table[bits][1];
  }

  GET_BITS (&bs, 1, &bits);
  if (bits) {
    /* vol control parameters, skip chroma and low delay */
    GET_BITS (&bs, 3, &bits);
    GET_BITS (&bs, 1, &bits);
    if (bits) {
      /* skip vbv_parameters */
      GET_BITS (&bs, 79, &bits);
    }
  }

  /* layer shape */
  GET_BITS (&bs, 2, &bits);
  /* only support rectangular */
  if (bits != 0)
    goto failed;

  MARKER_BIT (&bs);
  GET_BITS (&bs, 16, &bits);
  time_increment_resolution = bits;
  MARKER_BIT (&bs);

  GST_DEBUG_OBJECT (parse, "time increment resolution %d",
      time_increment_resolution);

  GET_BITS (&bs, 1, &bits);
  if (bits) {
    /* fixed time increment */
    int n;

    /* Lenght of the time increment is the minimal number of bits needed to
     * represent time_increment_resolution */
    for (n = 0; (time_increment_resolution >> n) != 0; n++);
    GET_BITS (&bs, n, &bits);

    fixed_time_increment = bits;
  } else {
    /* When fixed_vop_rate is not set we can't guess any framerate */
    fixed_time_increment = 0;
  }
  GST_DEBUG_OBJECT (parse, "fixed time increment %d", fixed_time_increment);

  /* assuming rectangular shape */
  MARKER_BIT (&bs);
  GET_BITS (&bs, 13, &bits);
  width = bits;
  MARKER_BIT (&bs);
  GET_BITS (&bs, 13, &bits);
  height = bits;
  MARKER_BIT (&bs);

  /* ok we know there is enough data in the stream to decode it and we can start
   * pushing the data */
  parse->have_config = TRUE;

out:
  return gst_mpeg4vparse_set_new_caps (parse, time_increment_resolution,
      fixed_time_increment, aspect_ratio_width, aspect_ratio_height,
      width, height);

  /* ERRORS */
failed:
  {
    GST_WARNING_OBJECT (parse, "Failed to parse config data");
    goto out;
  }
}

static inline gboolean
skip_user_data (bitstream_t * bs, guint32 * bits)
{
  while (*bits == USER_DATA_STARTCODE_MARKER) {
    guint32 b;

    do {
      GET_BITS (bs, 8, &b);
      *bits = (*bits << 8) | b;
    } while ((*bits >> 8) != START_MARKER);
  }

  return TRUE;

failed:
  return FALSE;
}

/* Returns whether we successfully set the caps downstream if needed */
static gboolean
gst_mpeg4vparse_handle_vos (GstMpeg4VParse * parse, const guint8 * data,
    gsize size)
{
  /* Skip the startcode */
  guint32 bits;

  guint8 profile;
  gboolean equal;
  bitstream_t bs = { data, 0, 0, size };

  if (size < 5)
    goto failed;

  /* Parse the config from the VOS frame */
  bs.offset = 5;

  profile = data[4];

  /* invalid profile, yikes */
  if (profile == 0)
    return FALSE;

  equal = FALSE;
  if (G_LIKELY (parse->config &&
          memcmp (GST_BUFFER_DATA (parse->config), data, size) == 0))
    equal = TRUE;

  if (G_LIKELY (parse->profile == profile && equal)) {
    /* We know this profile and config data, so we can just keep the same caps
     */
    return TRUE;
  }

  /* Even if we fail to parse, then some other element might succeed, so always
   * put the VOS in the config */
  parse->profile = profile;
  if (parse->config != NULL)
    gst_buffer_unref (parse->config);

  parse->config = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (parse->config), data, size);

  parse->have_config = TRUE;

  /* Expect Visual Object startcode */
  GET_BITS (&bs, 32, &bits);

  /* but skip optional user data */
  if (!skip_user_data (&bs, &bits))
    goto failed;

  if (bits != VISUAL_OBJECT_STARTCODE_MARKER)
    goto failed;

  GET_BITS (&bs, 1, &bits);
  if (bits == 0x1) {
    /* Skip visual_object_verid and priority */
    GET_BITS (&bs, 7, &bits);
  }

  GET_BITS (&bs, 4, &bits);
  /* Only support video ID */
  if (bits != 0x1)
    goto failed;

  /* video signal type */
  GET_BITS (&bs, 1, &bits);

  if (bits == 0x1) {
    /* video signal type, ignore format and range */
    GET_BITS (&bs, 4, &bits);

    GET_BITS (&bs, 1, &bits);
    if (bits == 0x1) {
      /* ignore color description */
      GET_BITS (&bs, 24, &bits);
    }
  }

  if (!next_start_code (&bs))
    goto failed;

  /* skip optional user data */
  GET_BITS (&bs, 32, &bits);
  if (!skip_user_data (&bs, &bits))
    goto failed;
  /* rewind to start code */
  bs.offset -= 4;

  data = &bs.data[bs.offset];
  size -= bs.offset;

  return gst_mpeg4vparse_handle_vo (parse, data, size);

out:
  return gst_mpeg4vparse_set_new_caps (parse, 0, 0, -1, -1, -1, -1);

  /* ERRORS */
failed:
  {
    GST_WARNING_OBJECT (parse, "Failed to parse config data");
    goto out;
  }
}

static void
gst_mpeg4vparse_push (GstMpeg4VParse * parse, gsize size)
{
  if (G_UNLIKELY (!parse->have_config && parse->drop)) {
    GST_LOG_OBJECT (parse, "Dropping %d bytes", parse->offset);
    gst_adapter_flush (parse->adapter, size);
  } else {
    GstBuffer *out_buf;

    out_buf = gst_adapter_take_buffer (parse->adapter, parse->offset);

    if (out_buf) {
      /* Set GST_BUFFER_FLAG_DELTA_UNIT if it's not an intra frame */
      if (!parse->intra_frame) {
        GST_BUFFER_FLAG_SET (out_buf, GST_BUFFER_FLAG_DELTA_UNIT);
      }
      gst_buffer_set_caps (out_buf, GST_PAD_CAPS (parse->srcpad));
      GST_BUFFER_TIMESTAMP (out_buf) = parse->timestamp;
      gst_pad_push (parse->srcpad, out_buf);
    }
  }

  /* Restart now that we flushed data */
  parse->offset = 0;
  parse->state = PARSE_NEED_START;
  parse->intra_frame = FALSE;
}

static GstFlowReturn
gst_mpeg4vparse_drain (GstMpeg4VParse * parse, GstBuffer * last_buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const guint8 *data = NULL;
  guint available = 0;

  available = gst_adapter_available (parse->adapter);
  /* We do a quick check here to avoid the _peek() below. */
  if (G_UNLIKELY (available < 5)) {
    GST_DEBUG_OBJECT (parse, "we need more data, %d < 5", available);
    goto beach;
  }
  data = gst_adapter_peek (parse->adapter, available);

  /* Need at least 5 more bytes, 4 for the startcode, 1 to optionally determine
   * the VOP frame type */
  while (available >= 5 && parse->offset < available - 5) {
    if (data[parse->offset] == 0 && data[parse->offset + 1] == 0 &&
        data[parse->offset + 2] == 1) {

      switch (parse->state) {
        case PARSE_NEED_START:
        {
          gboolean found = FALSE;
          guint8 code;

          code = data[parse->offset + 3];

          switch (code) {
            case VOP_STARTCODE:
            case VOS_STARTCODE:
            case GOP_STARTCODE:
              found = TRUE;
              break;
            default:
              if (code <= 0x1f)
                found = TRUE;
              break;
          }
          if (found) {
            /* valid starts of a frame */
            parse->state = PARSE_START_FOUND;
            if (parse->offset > 0) {
              GST_LOG_OBJECT (parse, "Flushing %u bytes", parse->offset);
              gst_adapter_flush (parse->adapter, parse->offset);
              parse->offset = 0;
              available = gst_adapter_available (parse->adapter);
              data = gst_adapter_peek (parse->adapter, available);
            }
          } else
            parse->offset += 4;
          break;
        }
        case PARSE_START_FOUND:
        {
          guint8 code;

          code = data[parse->offset + 3];

          switch (code) {
            case VOP_STARTCODE:
              GST_LOG_OBJECT (parse, "found VOP start marker at %u",
                  parse->offset);
              parse->intra_frame = ((data[parse->offset + 4] >> 6 & 0x3) == 0);
              /* Ensure that the timestamp of the outgoing buffer is the same
               * as the one the VOP header is found in */
              parse->timestamp = GST_BUFFER_TIMESTAMP (last_buffer);
              parse->state = PARSE_VOP_FOUND;
              break;
            case VOS_STARTCODE:
              GST_LOG_OBJECT (parse, "found VOS start marker at %u",
                  parse->offset);
              parse->vos_offset = parse->offset;
              parse->state = PARSE_VOS_FOUND;
              break;
            default:
              if (code <= 0x1f) {
                GST_LOG_OBJECT (parse, "found VO start marker at %u",
                    parse->offset);
                parse->vos_offset = parse->offset;
                parse->state = PARSE_VO_FOUND;
              }
              break;
          }
          /* Jump over it */
          parse->offset += 4;
          break;
        }
        case PARSE_VO_FOUND:
          switch (data[parse->offset + 3]) {
            case GOP_STARTCODE:
            case VOP_STARTCODE:
              /* end of VOS found, interpret the config data and restart the
               * search for the VOP */
              gst_mpeg4vparse_handle_vo (parse, data + parse->vos_offset,
                  parse->offset - parse->vos_offset);
              parse->state = PARSE_START_FOUND;
              break;
            default:
              parse->offset += 4;
          }
          break;
        case PARSE_VOS_FOUND:
          switch (data[parse->offset + 3]) {
            case GOP_STARTCODE:
            case VOP_STARTCODE:
              /* end of VOS found, interpret the config data and restart the
               * search for the VOP */
              gst_mpeg4vparse_handle_vos (parse, data + parse->vos_offset,
                  parse->offset - parse->vos_offset);
              parse->state = PARSE_START_FOUND;
              break;
            default:
              parse->offset += 4;
          }
          break;
        case PARSE_VOP_FOUND:
        {                       /* We were in a VOP already, any start code marks the end of it */
          GST_LOG_OBJECT (parse, "found VOP end marker at %u", parse->offset);

          gst_mpeg4vparse_push (parse, parse->offset);

          available = gst_adapter_available (parse->adapter);
          data = gst_adapter_peek (parse->adapter, available);
          break;
        }
        default:
          GST_WARNING_OBJECT (parse, "unexpected parse state (%d)",
              parse->state);
          ret = GST_FLOW_UNEXPECTED;
          goto beach;
      }
    } else {                    /* Continue searching */
      parse->offset++;
    }
  }

beach:
  return ret;
}

static GstFlowReturn
gst_mpeg4vparse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (parse, "received buffer of %u bytes with ts %"
      GST_TIME_FORMAT " and offset %" G_GINT64_FORMAT, GST_BUFFER_SIZE (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_BUFFER_OFFSET (buffer));

  gst_adapter_push (parse->adapter, buffer);

  /* Drain the accumulated blocks frame per frame */
  ret = gst_mpeg4vparse_drain (parse, buffer);

  gst_object_unref (parse);

  return ret;
}

static gboolean
gst_mpeg4vparse_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean res = TRUE;
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (gst_pad_get_parent (pad));
  GstStructure *s;
  const GValue *value;

  GST_DEBUG_OBJECT (parse, "setcaps called with %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);

  if ((value = gst_structure_get_value (s, "codec_data")) != NULL
      && G_VALUE_HOLDS (value, GST_TYPE_BUFFER)) {
    GstBuffer *buf = gst_value_get_buffer (value);

    res = gst_mpeg4vparse_handle_vos (parse, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
  } else {
    /* No codec data, set minimal new caps.. VOS parsing later will fill in
     * the other fields */
    res = gst_mpeg4vparse_set_new_caps (parse, 0, 0, 0, 0, 0, 0);
  }

  gst_object_unref (parse);
  return res;
}

static gboolean
gst_mpeg4vparse_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (parse, "handling event type %s",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (parse->state == PARSE_VOP_FOUND) {
        /* If we've found the start of the VOP assume what's left in the
         * adapter is the complete VOP. This might cause us to send an
         * incomplete VOP out, but prevents the last video frame from
         * potentially being dropped */
        gst_mpeg4vparse_push (parse, gst_adapter_available (parse->adapter));
      }
      /* fallthrough */
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (parse);

  return res;
}

static gboolean
gst_mpeg4vparse_src_query (GstPad * pad, GstQuery * query)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (gst_pad_get_parent (pad));
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      /* We need to send the query upstream and add the returned latency to our
       * own */
      GstClockTime min_latency, max_latency;

      gboolean us_live;

      GstClockTime our_latency;

      if ((res = gst_pad_peer_query (parse->sinkpad, query))) {
        gst_query_parse_latency (query, &us_live, &min_latency, &max_latency);

        GST_DEBUG_OBJECT (parse, "Peer latency: min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        /* our latency is 1 frame, find the frame duration */
        our_latency = parse->frame_duration;

        GST_DEBUG_OBJECT (parse, "Our latency: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (our_latency));

        /* we add some latency */
        min_latency += our_latency;
        if (max_latency != -1)
          max_latency += our_latency;

        GST_DEBUG_OBJECT (parse, "Calculated total latency : min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        gst_query_set_latency (query, us_live, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_peer_query (parse->sinkpad, query);
      break;
  }
  gst_object_unref (parse);

  return res;
}

static void
gst_mpeg4vparse_cleanup (GstMpeg4VParse * parse)
{
  if (parse->adapter) {
    gst_adapter_clear (parse->adapter);
  }
  if (parse->config != NULL) {
    gst_buffer_unref (parse->config);
    parse->config = NULL;
  }

  parse->state = PARSE_NEED_START;
  parse->have_config = FALSE;
  parse->offset = 0;
}

static GstStateChangeReturn
gst_mpeg4vparse_change_state (GstElement * element, GstStateChange transition)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (element);

  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mpeg4vparse_cleanup (parse);
      break;
    default:
      break;
  }
  return ret;
}

static void
gst_mpeg4vparse_dispose (GObject * object)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (object);

  if (parse->adapter) {
    g_object_unref (parse->adapter);
    parse->adapter = NULL;
  }
  if (parse->config != NULL) {
    gst_buffer_unref (parse->config);
    parse->config = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_mpeg4vparse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &mpeg4vparse_details);
}

static void
gst_mpeg4vparse_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (object);

  switch (property_id) {
    case PROP_DROP:
      parse->drop = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_mpeg4vparse_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEOPARSE (object);

  switch (property_id) {
    case PROP_DROP:
      g_value_set_boolean (value, parse->drop);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_mpeg4vparse_class_init (GstMpeg4VParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  gstelement_class = (GstElementClass *) klass;
  gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_mpeg4vparse_dispose);

  gobject_class->set_property = gst_mpeg4vparse_set_property;
  gobject_class->get_property = gst_mpeg4vparse_get_property;

  g_object_class_install_property (gobject_class, PROP_DROP,
      g_param_spec_boolean ("drop", "drop",
          "Drop data untill valid configuration data is received either "
          "in the stream or through caps", DEFAULT_PROP_DROP,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_change_state);
}

static void
gst_mpeg4vparse_init (GstMpeg4VParse * parse, GstMpeg4VParseClass * g_class)
{
  parse->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_chain));
  gst_pad_set_event_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_sink_event));
  gst_pad_set_setcaps_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  parse->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_query_function (parse->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_src_query));
  gst_pad_use_fixed_caps (parse->srcpad);
  gst_element_add_pad (GST_ELEMENT (parse), parse->srcpad);

  parse->adapter = gst_adapter_new ();

  gst_mpeg4vparse_cleanup (parse);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mpeg4v_parse_debug, "mpeg4videoparse", 0,
      "MPEG-4 video parser");

  if (!gst_element_register (plugin, "mpeg4videoparse", GST_RANK_SECONDARY,
          gst_mpeg4vparse_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpeg4videoparse",
    "MPEG-4 video parser",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
