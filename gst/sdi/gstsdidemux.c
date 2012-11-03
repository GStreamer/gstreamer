/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
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
 * SECTION:element-gstsdidemux
 *
 * The gstsdidemux element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! gstsdidemux ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gst.h>
#include <string.h>
#include "gstsdidemux.h"

/* prototypes */


static void gst_sdi_demux_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_sdi_demux_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_sdi_demux_dispose (GObject * object);
static void gst_sdi_demux_finalize (GObject * object);

static GstStateChangeReturn
gst_sdi_demux_change_state (GstElement * element, GstStateChange transition);
static GstFlowReturn gst_sdi_demux_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_sdi_demux_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_sdi_demux_src_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_sdi_demux_src_getcaps (GstPad * pad);


enum
{
  PROP_0
};

/* pad templates */

#define GST_VIDEO_CAPS_NTSC(fourcc) \
  "video/x-raw-yuv,format=(fourcc)" fourcc ",width=720,height=480," \
  "framerate=30000/1001,interlaced=TRUE,pixel-aspect-ratio=10/11," \
  "chroma-site=mpeg2,color-matrix=sdtv"
#define GST_VIDEO_CAPS_NTSC_WIDE(fourcc) \
  "video/x-raw-yuv,format=(fourcc)" fourcc ",width=720,height=480," \
  "framerate=30000/1001,interlaced=TRUE,pixel-aspect-ratio=40/33," \
  "chroma-site=mpeg2,color-matrix=sdtv"
#define GST_VIDEO_CAPS_PAL(fourcc) \
  "video/x-raw-yuv,format=(fourcc)" fourcc ",width=720,height=576," \
  "framerate=25/1,interlaced=TRUE,pixel-aspect-ratio=12/11," \
  "chroma-site=mpeg2,color-matrix=sdtv"
#define GST_VIDEO_CAPS_PAL_WIDE(fourcc) \
  "video/x-raw-yuv,format=(fourcc)" fourcc ",width=720,height=576," \
  "framerate=25/1,interlaced=TRUE,pixel-aspect-ratio=16/11," \
  "chroma-site=mpeg2,color-matrix=sdtv"

static GstStaticPadTemplate gst_sdi_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-raw-sdi")
    );

static GstStaticPadTemplate gst_sdi_demux_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_NTSC ("UYVY") ";"
        GST_VIDEO_CAPS_PAL ("UYVY"))
    );

/* class initialization */

GST_BOILERPLATE (GstSdiDemux, gst_sdi_demux, GstElement, GST_TYPE_ELEMENT);

static void
gst_sdi_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sdi_demux_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sdi_demux_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "SDI Demuxer",
      "Demuxer",
      "Demultiplex SDI streams into raw audio and video",
      "David Schleef <ds@schleef.org>");
}

static void
gst_sdi_demux_class_init (GstSdiDemuxClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_sdi_demux_set_property;
  gobject_class->get_property = gst_sdi_demux_get_property;
  gobject_class->dispose = gst_sdi_demux_dispose;
  gobject_class->finalize = gst_sdi_demux_finalize;
  if (0)
    element_class->change_state =
        GST_DEBUG_FUNCPTR (gst_sdi_demux_change_state);

}

static void
gst_sdi_demux_init (GstSdiDemux * sdidemux, GstSdiDemuxClass * sdidemux_class)
{

  sdidemux->sinkpad =
      gst_pad_new_from_static_template (&gst_sdi_demux_sink_template, "sink");
  gst_pad_set_event_function (sdidemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sdi_demux_sink_event));
  gst_pad_set_chain_function (sdidemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sdi_demux_chain));
  gst_element_add_pad (GST_ELEMENT (sdidemux), sdidemux->sinkpad);

  sdidemux->srcpad =
      gst_pad_new_from_static_template (&gst_sdi_demux_src_template, "src");
  gst_pad_set_event_function (sdidemux->srcpad,
      GST_DEBUG_FUNCPTR (gst_sdi_demux_src_event));
  gst_pad_set_getcaps_function (sdidemux->srcpad,
      GST_DEBUG_FUNCPTR (gst_sdi_demux_src_getcaps));
  gst_element_add_pad (GST_ELEMENT (sdidemux), sdidemux->srcpad);


}

void
gst_sdi_demux_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_SDI_DEMUX (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_sdi_demux_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_SDI_DEMUX (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_sdi_demux_dispose (GObject * object)
{
  g_return_if_fail (GST_IS_SDI_DEMUX (object));

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_sdi_demux_finalize (GObject * object)
{
  g_return_if_fail (GST_IS_SDI_DEMUX (object));

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GstStateChangeReturn
gst_sdi_demux_change_state (GstElement * element, GstStateChange transition)
{

  return GST_STATE_CHANGE_SUCCESS;
}

static GstCaps *
gst_sdi_demux_src_getcaps (GstPad * pad)
{
  return gst_caps_from_string (GST_VIDEO_CAPS_NTSC ("UYVY"));
}

static void
gst_sdi_demux_get_output_buffer (GstSdiDemux * sdidemux)
{
  sdidemux->output_buffer =
      gst_buffer_new_and_alloc (720 * sdidemux->format->active_lines * 2);
  gst_buffer_set_caps (sdidemux->output_buffer,
      gst_caps_from_string (GST_VIDEO_CAPS_PAL ("UYVY")));
  GST_BUFFER_TIMESTAMP (sdidemux->output_buffer) =
      GST_SECOND * sdidemux->frame_number;
  sdidemux->frame_number++;
}

static guint32
get_word10 (guint8 * ptr)
{
  guint32 a;

  a = (((ptr[0] >> 2) | (ptr[1] << 6)) & 0xff) << 24;
  a |= (((ptr[1] >> 4) | (ptr[2] << 4)) & 0xff) << 16;
  a |= (((ptr[2] >> 6) | (ptr[3] << 2)) & 0xff) << 8;
  a |= ptr[4];

  return a;
}

static void
line10_copy (guint8 * dest, guint8 * src, int n)
{
  int i;
  guint32 a;
  for (i = 0; i < n; i++) {
    a = get_word10 (src);
    GST_WRITE_UINT32_BE (dest, a);
    src += 5;
    dest += 4;
  }

}


static GstFlowReturn
copy_line (GstSdiDemux * sdidemux, guint8 * line)
{
  guint8 *output_data;
  GstFlowReturn ret = GST_FLOW_OK;
  GstSdiFormat *format = sdidemux->format;

  output_data = GST_BUFFER_DATA (sdidemux->output_buffer);

  /* line is one less than the video line */
  if (sdidemux->line >= format->start0 - 1 &&
      sdidemux->line < format->start0 - 1 + format->active_lines / 2) {
#if 0
    memcpy (output_data + 720 * 2 * ((sdidemux->line -
                (format->start0 - 1)) * 2 + (!format->tff)),
        line + (format->width - 720) * 2, 720 * 2);
#else
    line10_copy (output_data + 720 * 2 * ((sdidemux->line -
                (format->start0 - 1)) * 2 + (!format->tff)),
        line + (format->width - 720) / 2 * 5, 720 / 2);
#endif
  }
  if (sdidemux->line >= format->start1 - 1 &&
      sdidemux->line < format->start1 - 1 + format->active_lines / 2) {
#if 0
    memcpy (output_data + 720 * 2 * ((sdidemux->line -
                (format->start1 - 1)) * 2 + (format->tff)),
        line + (format->width - 720) * 2, 720 * 2);
#else
    line10_copy (output_data + 720 * 2 * ((sdidemux->line -
                (format->start1 - 1)) * 2 + (format->tff)),
        line + (format->width - 720) / 2 * 5, 720 / 2);
#endif
  }

  sdidemux->offset = 0;
  sdidemux->line++;
  if (sdidemux->line == format->lines) {
    ret = gst_pad_push (sdidemux->srcpad, sdidemux->output_buffer);
    gst_sdi_demux_get_output_buffer (sdidemux);
    sdidemux->line = 0;
  }

  return ret;
}

#define SDI_IS_SYNC(a) (((a)&0xffffff80) == 0xff000080)
#define SDI_SYNC_F(a) (((a)>>6)&1)
#define SDI_SYNC_V(a) (((a)>>5)&1)
#define SDI_SYNC_H(a) (((a)>>4)&1)

GstSdiFormat sd_ntsc = { 525, 480, 858, 20, 283, 0 };
GstSdiFormat sd_pal = { 625, 576, 864, 23, 336, 1 };

static GstFlowReturn
gst_sdi_demux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSdiDemux *sdidemux;
  int offset = 0;
  guint8 *data = GST_BUFFER_DATA (buffer);
  int size = GST_BUFFER_SIZE (buffer);
  GstFlowReturn ret = GST_FLOW_OK;
  GstSdiFormat *format;

  sdidemux = GST_SDI_DEMUX (gst_pad_get_parent (pad));
  sdidemux->format = &sd_pal;
  format = sdidemux->format;

  GST_DEBUG_OBJECT (sdidemux, "chain");

  if (GST_BUFFER_IS_DISCONT (buffer)) {
    sdidemux->have_hsync = FALSE;
    sdidemux->have_vsync = FALSE;
  }

  if (!sdidemux->have_hsync) {
#if 0
    for (offset = 0; offset < size; offset += 4) {
      guint32 sync = READ_UINT32_BE (data + offset);
      GST_ERROR ("sync value %08x", sync);
      if (SDI_IS_SYNC (sync) && SDI_SYNC_H (sync)) {
        sdidemux->have_hsync = TRUE;
        sdidemux->line = 0;
        sdidemux->offset = 0;
        break;
      }
    }
#else
    for (offset = 0; offset < size; offset += 5) {
      guint32 sync = get_word10 (data + offset);
      //GST_ERROR("sync value %08x", sync);
      if (SDI_IS_SYNC (sync) && SDI_SYNC_H (sync)) {
        sdidemux->have_hsync = TRUE;
        sdidemux->line = 0;
        sdidemux->offset = 0;
        break;
      }
    }
#endif
    if (!sdidemux->have_hsync) {
      GST_ERROR ("no sync");
      goto out;
    }
  }

  if (sdidemux->output_buffer == NULL) {
    gst_sdi_demux_get_output_buffer (sdidemux);
  }
#if 0
  if (sdidemux->offset) {
    int n;

    /* second half of a line */
    n = MIN (size - offset, format->width * 2 - sdidemux->offset);

    memcpy (sdidemux->stored_line + sdidemux->offset, data + offset, n);

    offset += n;
    sdidemux->offset += n;

    if (sdidemux->offset == format->width * 2) {
      guint32 sync =
          GST_READ_UINT32_BE (data + offset + (format->width - 720 - 2) * 2);

      //GST_ERROR("%08x", sync);
      if (!sdidemux->have_vsync) {
        //GST_ERROR("%08x", GST_READ_UINT32_BE(data+offset));
        if (SDI_IS_SYNC (sync) && !SDI_SYNC_F (sync) &&
            SDI_SYNC_F (sdidemux->last_sync)) {
          sdidemux->have_vsync = TRUE;
        }
        sdidemux->line = 0;
      }

      ret = copy_line (sdidemux, sdidemux->stored_line);

      sdidemux->last_sync = sync;
    }
  }

  while (size - offset >= format->width * 2) {
    guint32 sync =
        GST_READ_UINT32_BE (data + offset + (format->width - 720 - 2) * 2);

    //GST_ERROR("%08x", sync);
    if (!sdidemux->have_vsync) {
      if (SDI_IS_SYNC (sync) && !SDI_SYNC_F (sync) &&
          SDI_SYNC_F (sdidemux->last_sync)) {
        sdidemux->have_vsync = TRUE;
      }
      sdidemux->line = 0;
    }

    ret = copy_line (sdidemux, data + offset);
    offset += format->width * 2;

    sdidemux->last_sync = sync;
  }

  if (size - offset > 0) {
    memcpy (sdidemux->stored_line, data + offset, size - offset);
    sdidemux->offset = size - offset;
  }
#else
  if (sdidemux->offset) {
    int n;

    /* second half of a line */
    n = MIN (size - offset, format->width / 2 * 5 - sdidemux->offset);

    memcpy (sdidemux->stored_line + sdidemux->offset, data + offset, n);

    offset += n;
    sdidemux->offset += n;

    if (sdidemux->offset == (format->width / 2) * 5) {
      guint32 sync =
          get_word10 (data + offset + ((format->width - 720 - 2) / 2) * 5);

      if (!sdidemux->have_vsync) {
        //GST_ERROR("%08x", GST_READ_UINT32_BE(data+offset));
        if (SDI_IS_SYNC (sync) && !SDI_SYNC_F (sync) &&
            SDI_SYNC_F (sdidemux->last_sync)) {
          sdidemux->have_vsync = TRUE;
        }
        sdidemux->line = 0;
      }

      ret = copy_line (sdidemux, sdidemux->stored_line);

      sdidemux->last_sync = sync;
    }
  }

  while (size - offset >= format->width / 2 * 5) {
    guint32 sync =
        get_word10 (data + offset + ((format->width - 720 - 2) / 2) * 5);

    //GST_ERROR("%08x", sync);
    if (!sdidemux->have_vsync) {
      if (SDI_IS_SYNC (sync) && !SDI_SYNC_F (sync) &&
          SDI_SYNC_F (sdidemux->last_sync)) {
        sdidemux->have_vsync = TRUE;
      }
      sdidemux->line = 0;
    }

    ret = copy_line (sdidemux, data + offset);
    offset += (format->width / 2) * 5;

    sdidemux->last_sync = sync;
  }

  if (size - offset > 0) {
    memcpy (sdidemux->stored_line, data + offset, size - offset);
    sdidemux->offset = size - offset;
  }
#endif

out:
  gst_buffer_unref (buffer);
  gst_object_unref (sdidemux);
  return ret;
}

static gboolean
gst_sdi_demux_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstSdiDemux *sdidemux;

  sdidemux = GST_SDI_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (sdidemux, "event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (sdidemux->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      res = gst_pad_push_event (sdidemux->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
      res = gst_pad_push_event (sdidemux->srcpad, event);
      break;
    case GST_EVENT_EOS:
      res = gst_pad_push_event (sdidemux->srcpad, event);
      break;
    default:
      res = gst_pad_push_event (sdidemux->srcpad, event);
      break;
  }

  gst_object_unref (sdidemux);
  return res;
}

static gboolean
gst_sdi_demux_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstSdiDemux *sdidemux;

  sdidemux = GST_SDI_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (sdidemux, "event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_pad_push_event (sdidemux->sinkpad, event);
      break;
    default:
      res = gst_pad_push_event (sdidemux->sinkpad, event);
      break;
  }

  gst_object_unref (sdidemux);
  return res;
}
