/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@temple-baptist.com>
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

#include "flx_fmt.h"
#include "gstflxdec.h"
#include <gst/video/video.h>

#define JIFFIE  (GST_SECOND/70)

GST_DEBUG_CATEGORY_STATIC (flxdec_debug);
#define GST_CAT_DEFAULT flxdec_debug

/* flx element information */
static GstElementDetails flxdec_details = {
  "FLX Decoder",
  "Codec/Decoder/Audio",
  "FLX decoder",
  "Sepp Wijnands <mrrazz@garbage-coderz.net>, Zeeshan Ali <zeenix@gmail.com>"
};

/* Flx signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

/* input */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-fli")
    );

/* output */
static GstStaticPadTemplate src_video_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN)
    );


static void gst_flxdec_class_init (GstFlxDecClass * klass);
static void gst_flxdec_base_init (GstFlxDecClass * klass);
static void gst_flxdec_init (GstFlxDec * flxdec);

static GstFlowReturn gst_flxdec_chain (GstPad * pad, GstBuffer * buf);

static GstStateChangeReturn gst_flxdec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_flxdec_src_event_handler (GstPad * pad, GstEvent * event);
static gboolean gst_flxdec_sink_event_handler (GstPad * pad, GstEvent * event);

static void gst_flxdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_flxdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static void flx_decode_color (GstFlxDec *, guchar *, guchar *, gint);
static void flx_decode_brun (GstFlxDec *, guchar *, guchar *);
static void flx_decode_delta_fli (GstFlxDec *, guchar *, guchar *);
static void flx_decode_delta_flc (GstFlxDec *, guchar *, guchar *);

#define rndalign(off) ((off) + ((off) % 2))

static GstElementClass *parent_class = NULL;

GType
gst_flxdec_get_type (void)
{
  static GType flxdec_type = 0;

  if (!flxdec_type) {
    static const GTypeInfo flxdec_info = {
      sizeof (GstFlxDecClass),
      (GBaseInitFunc) gst_flxdec_base_init,
      NULL,
      (GClassInitFunc) gst_flxdec_class_init,
      NULL,
      NULL,
      sizeof (GstFlxDec),
      0,
      (GInstanceInitFunc) gst_flxdec_init,
    };

    flxdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstFlxDec", &flxdec_info, 0);
  }
  return flxdec_type;
}

static void
gst_flxdec_base_init (GstFlxDecClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (gstelement_class, &flxdec_details);
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_video_factory));
}

static void
gst_flxdec_class_init (GstFlxDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  GST_DEBUG_CATEGORY_INIT (flxdec_debug, "flxdec", 0, "FLX video decoder");

  gobject_class->set_property = gst_flxdec_set_property;
  gobject_class->get_property = gst_flxdec_get_property;

  gstelement_class->change_state = gst_flxdec_change_state;
}

static void
gst_flxdec_init (GstFlxDec * flxdec)
{
  flxdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_factory),
      "sink");
  gst_element_add_pad (GST_ELEMENT (flxdec), flxdec->sinkpad);
  gst_pad_set_chain_function (flxdec->sinkpad, gst_flxdec_chain);
  gst_pad_set_event_function (flxdec->sinkpad, gst_flxdec_sink_event_handler);

  flxdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_video_factory), "src");
  gst_element_add_pad (GST_ELEMENT (flxdec), flxdec->srcpad);
  gst_pad_set_event_function (flxdec->srcpad, gst_flxdec_src_event_handler);

  gst_pad_use_fixed_caps (flxdec->srcpad);

  flxdec->frame = NULL;
  flxdec->delta = NULL;

  flxdec->adapter = gst_adapter_new ();
}

static gboolean
gst_flxdec_src_event_handler (GstPad * pad, GstEvent * event)
{
  GstFlxDec *flxdec = (GstFlxDec *) gst_pad_get_parent (pad);

  g_return_val_if_fail (GST_IS_EVENT (event), FALSE);

  /* TODO: implement the seek and other event handling */

  return gst_pad_push_event (flxdec->sinkpad, event);
}

static gboolean
gst_flxdec_sink_event_handler (GstPad * pad, GstEvent * event)
{
  GstFlxDec *flxdec = (GstFlxDec *) gst_pad_get_parent (pad);

  g_return_val_if_fail (GST_IS_EVENT (event), FALSE);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS ||
      GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT)
    GST_STREAM_LOCK (flxdec->srcpad);

  gst_pad_push_event (flxdec->srcpad, gst_event_ref (event));

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS ||
      GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT)
    GST_STREAM_UNLOCK (flxdec->srcpad);

  return TRUE;
}

static void
flx_decode_chunks (GstFlxDec * flxdec, gulong count, gchar * data, gchar * dest)
{
  FlxFrameChunk *hdr;

  g_return_if_fail (data != NULL);

  while (count--) {
    hdr = (FlxFrameChunk *) data;
    data += FlxFrameChunkSize;

    switch (hdr->id) {
      case FLX_COLOR64:
        flx_decode_color (flxdec, data, dest, 2);
        data += rndalign (hdr->size) - FlxFrameChunkSize;
        break;

      case FLX_COLOR256:
        flx_decode_color (flxdec, data, dest, 0);
        data += rndalign (hdr->size) - FlxFrameChunkSize;
        break;

      case FLX_BRUN:
        flx_decode_brun (flxdec, data, dest);
        data += rndalign (hdr->size) - FlxFrameChunkSize;
        break;

      case FLX_LC:
        flx_decode_delta_fli (flxdec, data, dest);
        data += rndalign (hdr->size) - FlxFrameChunkSize;
        break;

      case FLX_SS2:
        flx_decode_delta_flc (flxdec, data, dest);
        data += rndalign (hdr->size) - FlxFrameChunkSize;
        break;

      case FLX_BLACK:
        memset (dest, 0, flxdec->size);
        break;

      case FLX_MINI:
        data += rndalign (hdr->size) - FlxFrameChunkSize;
        break;

      default:
        GST_WARNING ("Unimplented chunk type: 0x%02x size: %d - skipping",
            hdr->id, hdr->size);
        data += rndalign (hdr->size) - FlxFrameChunkSize;
        break;
    }
  }
}


static void
flx_decode_color (GstFlxDec * flxdec, guchar * data, guchar * dest, gint scale)
{
  guint packs, count, indx;

  g_return_if_fail (flxdec != NULL);

  packs = (data[0] + (data[1] << 8));

  data += 2;
  indx = 0;

  GST_LOG ("GstFlxDec: cmap packs: %d", packs);
  while (packs--) {
    /* color map index + skip count */
    indx += *data++;

    /* number of rgb triplets */
    count = *data++ & 0xff;
    if (count == 0)
      count = 256;

    GST_LOG ("GstFlxDec: cmap count: %d (indx: %d)\n", count, indx);
    flx_set_palette_vector (flxdec->converter, indx, count, data, scale);

    data += (count * 3);
  }
}

static void
flx_decode_brun (GstFlxDec * flxdec, guchar * data, guchar * dest)
{
  gulong count, lines, row;
  guchar x;

  g_return_if_fail (flxdec != NULL);

  lines = flxdec->hdr.height;
  while (lines--) {
    /* packet count.  
     * should not be used anymore, since the flc format can
     * contain more then 255 RLE packets. we use the frame 
     * width instead. 
     */
    data++;

    row = flxdec->hdr.width;
    while (row) {
      count = *data++;

      if (count > 0x7f) {
        /* literal run */
        count = 0x100 - count;
        row -= count;

        while (count--)
          *dest++ = *data++;

      } else {
        /* replicate run */
        row -= count;
        x = *data++;

        while (count--)
          *dest++ = x;
      }
    }
  }
}

static void
flx_decode_delta_fli (GstFlxDec * flxdec, guchar * data, guchar * dest)
{
  gulong count, packets, lines, start_line, start_l;
  guchar *start_p, x;

  g_return_if_fail (flxdec != NULL);
  g_return_if_fail (flxdec->delta != NULL);


  /* use last frame for delta */
  memcpy (dest, GST_BUFFER_DATA (flxdec->delta),
      GST_BUFFER_SIZE (flxdec->delta));

  start_line = (data[0] + (data[1] << 8));
  lines = (data[2] + (data[3] << 8));
  data += 4;

  /* start position of delta */
  dest += (flxdec->hdr.width * start_line);
  start_p = dest;
  start_l = lines;

  while (lines--) {
    /* packet count */
    packets = *data++;

    while (packets--) {
      /* skip count */
      dest += *data++;

      /* RLE count */
      count = *data++;

      if (count > 0x7f) {
        /* literal run */
        count = 0x100 - count;
        x = *data++;

        while (count--)
          *dest++ = x;

      } else {
        /* replicate run */
        while (count--)
          *dest++ = *data++;
      }
    }
    start_p += flxdec->hdr.width;
    dest = start_p;
  }
}

static void
flx_decode_delta_flc (GstFlxDec * flxdec, guchar * data, guchar * dest)
{
  gulong count, lines, start_l, opcode;
  guchar *start_p;

  g_return_if_fail (flxdec != NULL);
  g_return_if_fail (flxdec->delta != NULL);


  /* use last frame for delta */
  memcpy (dest, GST_BUFFER_DATA (flxdec->delta),
      GST_BUFFER_SIZE (flxdec->delta));

  lines = (data[0] + (data[1] << 8));
  data += 2;

  start_p = dest;
  start_l = lines;

  while (lines) {
    dest = start_p + (flxdec->hdr.width * (start_l - lines));

    /* process opcode(s) */
    while ((opcode = (data[0] + (data[1] << 8))) & 0xc000) {
      data += 2;
      if ((opcode & 0xc000) == 0xc000) {
        /* skip count */
        start_l += (0x10000 - opcode);
        dest += flxdec->hdr.width * (0x10000 - opcode);
      } else {
        /* last pixel */
        dest += flxdec->hdr.width;
        *dest++ = (opcode & 0xff);
      }
    }
    data += 2;

    /* last opcode is the packet count */
    while (opcode--) {
      /* skip count */
      dest += *data++;

      /* RLE count */
      count = *data++;

      if (count > 0x7f) {
        /* replicate word run */
        count = 0x100 - count;
        while (count--) {
          *dest++ = data[0];
          *dest++ = data[1];
        }
        data += 2;
      } else {
        /* literal word run */
        while (count--) {
          *dest++ = *data++;
          *dest++ = *data++;
        }
      }
    }
    lines--;
  }
}

static GstFlowReturn
gst_flxdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstCaps *caps;
  guint avail;
  GstFlowReturn res = GST_FLOW_OK;

  GstFlxDec *flxdec;
  FlxHeader *flxh;
  FlxFrameChunk *flxfh;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);
  flxdec = (GstFlxDec *) gst_pad_get_parent (pad);
  g_return_val_if_fail (flxdec != NULL, GST_FLOW_ERROR);

  gst_adapter_push (flxdec->adapter, buf);
  avail = gst_adapter_available (flxdec->adapter);

  if (flxdec->state == GST_FLXDEC_READ_HEADER) {
    if (avail >= FlxHeaderSize) {
      const guint8 *data = gst_adapter_peek (flxdec->adapter, FlxHeaderSize);

      memcpy ((gchar *) & flxdec->hdr, data, FlxHeaderSize);
      gst_adapter_flush (flxdec->adapter, FlxHeaderSize);

      flxh = &flxdec->hdr;

      /* check header */
      if (flxh->type != FLX_MAGICHDR_FLI &&
          flxh->type != FLX_MAGICHDR_FLC && flxh->type != FLX_MAGICHDR_FLX) {
        GST_ELEMENT_ERROR (flxdec, STREAM, WRONG_TYPE, (NULL),
            ("not a flx file (type %d)\n", flxh->type));
        return GST_FLOW_ERROR;
      }


      GST_LOG ("size      :  %d\n", flxh->size);
      GST_LOG ("frames    :  %d\n", flxh->frames);
      GST_LOG ("width     :  %d\n", flxh->width);
      GST_LOG ("height    :  %d\n", flxh->height);
      GST_LOG ("depth     :  %d\n", flxh->depth);
      GST_LOG ("speed     :  %d\n", flxh->speed);

      flxdec->next_time = 0;

      if (flxh->type == FLX_MAGICHDR_FLI) {
        flxdec->frame_time = JIFFIE * flxh->speed;
      } else {
        flxdec->frame_time = flxh->speed * GST_MSECOND;
      }

      caps = gst_caps_from_string (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN);
      gst_caps_set_simple (caps,
          "width", G_TYPE_INT, flxh->width,
          "height", G_TYPE_INT, flxh->height,
          "framerate", G_TYPE_DOUBLE,
          (gdouble) (GST_SECOND / flxdec->frame_time), NULL);

      gst_pad_set_caps (flxdec->srcpad, caps);
      gst_caps_unref (caps);

      if (flxh->depth <= 8)
        flxdec->converter =
            flx_colorspace_converter_new (flxh->width, flxh->height);

      if (flxh->type == FLX_MAGICHDR_FLC || flxh->type == FLX_MAGICHDR_FLX) {
        GST_LOG ("(FLC) aspect_dx :  %d\n", flxh->aspect_dx);
        GST_LOG ("(FLC) aspect_dy :  %d\n", flxh->aspect_dy);
        GST_LOG ("(FLC) oframe1   :  0x%08x\n", flxh->oframe1);
        GST_LOG ("(FLC) oframe2   :  0x%08x\n", flxh->oframe2);
      }

      flxdec->size = (flxh->width * flxh->height);

      /* create delta and output frame */
      flxdec->frame = gst_buffer_new ();
      flxdec->delta = gst_buffer_new ();
      GST_BUFFER_DATA (flxdec->frame) = g_malloc (flxdec->size);
      GST_BUFFER_SIZE (flxdec->frame) = flxdec->size;
      GST_BUFFER_DATA (flxdec->delta) = g_malloc (flxdec->size);
      GST_BUFFER_SIZE (flxdec->delta) = flxdec->size;

      flxdec->state = GST_FLXDEC_PLAYING;
    }
  } else if (flxdec->state == GST_FLXDEC_PLAYING) {
    GstBuffer *out;

    if (avail >= FlxFrameChunkSize) {
      guchar *chunk = NULL;
      guint to_flush = 0;
      const guint8 *data =
          gst_adapter_peek (flxdec->adapter, FlxFrameChunkSize);
      flxfh = (FlxFrameChunk *) g_memdup (data, FlxFrameChunkSize);

      switch (flxfh->id) {
        case FLX_FRAME_TYPE:
          if (avail < flxfh->size) {
            break;
          }

          gst_adapter_flush (flxdec->adapter, FlxFrameChunkSize);
          data = gst_adapter_peek (flxdec->adapter,
              flxfh->size - FlxFrameChunkSize);
          chunk = g_memdup (data, flxfh->size - FlxFrameChunkSize);
          to_flush = flxfh->size - FlxFrameChunkSize;

          if (((FlxFrameType *) chunk)->chunks == 0)
            break;

          /* create 32 bits output frame */
          res = gst_pad_alloc_buffer (flxdec->srcpad,
              GST_BUFFER_OFFSET_NONE,
              flxdec->size * 4, GST_PAD_CAPS (flxdec->srcpad), &out);

          if (res != GST_FLOW_OK)
            break;

          /* decode chunks */
          flx_decode_chunks (flxdec,
              ((FlxFrameType *) chunk)->chunks,
              chunk + FlxFrameTypeSize, GST_BUFFER_DATA (flxdec->frame));

          /* save copy of the current frame for possible delta. */
          memcpy (GST_BUFFER_DATA (flxdec->delta),
              GST_BUFFER_DATA (flxdec->frame), GST_BUFFER_SIZE (flxdec->delta));

          /* convert current frame. */
          flx_colorspace_convert (flxdec->converter,
              GST_BUFFER_DATA (flxdec->frame), GST_BUFFER_DATA (out));

          GST_BUFFER_TIMESTAMP (out) = flxdec->next_time;
          flxdec->next_time += flxdec->frame_time;

          gst_pad_push (flxdec->srcpad, out);
          break;
      }

      gst_adapter_flush (flxdec->adapter, to_flush);

      if (chunk)
        g_free (chunk);
      g_free (flxfh);
    }
  }

  return res;
}

static GstStateChangeReturn
gst_flxdec_change_state (GstElement * element, GstStateChange transition)
{
  GstFlxDec *flxdec;

  flxdec = GST_FLXDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (flxdec->adapter);
      flxdec->state = GST_FLXDEC_READ_HEADER;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_buffer_unref (flxdec->frame);
      flxdec->frame = NULL;
      gst_buffer_unref (flxdec->delta);
      flxdec->delta = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
  }

  return parent_class->change_state (element, transition);

  //return GST_STATE_CHANGE_SUCCESS;
}

static void
gst_flxdec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstFlxDec *flxdec;

  g_return_if_fail (GST_IS_FLXDEC (object));
  flxdec = GST_FLXDEC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_flxdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFlxDec *flxdec;

  g_return_if_fail (GST_IS_FLXDEC (object));
  flxdec = GST_FLXDEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "flxdec",
      GST_RANK_PRIMARY, GST_TYPE_FLXDEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "flxdec",
    "FLX video decoder",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
