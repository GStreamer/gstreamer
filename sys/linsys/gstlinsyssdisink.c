/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@entropywave.com>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "gstlinsyssdisink.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>

#include "sdivideo.h"

/* prototypes */


static void gst_linsys_sdi_sink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_linsys_sdi_sink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_linsys_sdi_sink_dispose (GObject * object);
static void gst_linsys_sdi_sink_finalize (GObject * object);

static GstCaps *gst_linsys_sdi_sink_get_caps (GstBaseSink * sink);
static gboolean gst_linsys_sdi_sink_set_caps (GstBaseSink * sink,
    GstCaps * caps);
static GstFlowReturn gst_linsys_sdi_sink_buffer_alloc (GstBaseSink * sink,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static void gst_linsys_sdi_sink_get_times (GstBaseSink * sink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_linsys_sdi_sink_start (GstBaseSink * sink);
static gboolean gst_linsys_sdi_sink_stop (GstBaseSink * sink);
static gboolean gst_linsys_sdi_sink_unlock (GstBaseSink * sink);
static gboolean gst_linsys_sdi_sink_event (GstBaseSink * sink,
    GstEvent * event);
static GstFlowReturn gst_linsys_sdi_sink_preroll (GstBaseSink * sink,
    GstBuffer * buffer);
static GstFlowReturn gst_linsys_sdi_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static GstStateChangeReturn gst_linsys_sdi_sink_async_play (GstBaseSink * sink);
static gboolean gst_linsys_sdi_sink_activate_pull (GstBaseSink * sink,
    gboolean active);
static void gst_linsys_sdi_sink_fixate (GstBaseSink * sink, GstCaps * caps);
static gboolean gst_linsys_sdi_sink_unlock_stop (GstBaseSink * sink);
static GstFlowReturn
gst_linsys_sdi_sink_render_list (GstBaseSink * sink,
    GstBufferList * buffer_list);

enum
{
  PROP_0,
  PROP_DEVICE
};

#define DEFAULT_DEVICE "/dev/sditx0"

/* pad templates */

static GstStaticPadTemplate gst_linsys_sdi_sink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv,format=(fourcc)UYVY,"
        "width=720,height=480,pixel-aspect-ratio=10/11,"
        "framerate=30000/1001,interlaced=true,"
        "colorspec=sdtv,chroma-site=mpeg2")
    );

/* class initialization */

GST_BOILERPLATE (GstLinsysSdiSink, gst_linsys_sdi_sink, GstBaseSink,
    GST_TYPE_BASE_SINK);

static void
gst_linsys_sdi_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_linsys_sdi_sink_sink_template));

  gst_element_class_set_static_metadata (element_class, "SDI video sink",
      "Sink/Video", "Writes video from SDI transmit device",
      "David Schleef <ds@entropywave.com>");
}

static void
gst_linsys_sdi_sink_class_init (GstLinsysSdiSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_linsys_sdi_sink_set_property;
  gobject_class->get_property = gst_linsys_sdi_sink_get_property;
  gobject_class->dispose = gst_linsys_sdi_sink_dispose;
  gobject_class->finalize = gst_linsys_sdi_sink_finalize;
  base_sink_class->get_caps = GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_get_caps);
  base_sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_set_caps);
  if (0)
    base_sink_class->buffer_alloc =
        GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_buffer_alloc);
  base_sink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_get_times);
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_stop);
  base_sink_class->unlock = GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_unlock);
  base_sink_class->event = GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_event);
  base_sink_class->preroll = GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_preroll);
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_render);
  if (0)
    base_sink_class->async_play =
        GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_async_play);
  if (0)
    base_sink_class->activate_pull =
        GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_activate_pull);
  base_sink_class->fixate = GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_fixate);
  base_sink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_unlock_stop);
  base_sink_class->render_list =
      GST_DEBUG_FUNCPTR (gst_linsys_sdi_sink_render_list);

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device", "device to transmit data on",
          DEFAULT_DEVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_linsys_sdi_sink_init (GstLinsysSdiSink * linsyssdisink,
    GstLinsysSdiSinkClass * linsyssdisink_class)
{
  linsyssdisink->device = g_strdup (DEFAULT_DEVICE);
}

void
gst_linsys_sdi_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLinsysSdiSink *linsyssdisink;

  g_return_if_fail (GST_IS_LINSYS_SDI_SINK (object));
  linsyssdisink = GST_LINSYS_SDI_SINK (object);

  switch (property_id) {
    case PROP_DEVICE:
      g_free (linsyssdisink->device);
      linsyssdisink->device = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_linsys_sdi_sink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstLinsysSdiSink *linsyssdisink;

  g_return_if_fail (GST_IS_LINSYS_SDI_SINK (object));
  linsyssdisink = GST_LINSYS_SDI_SINK (object);

  switch (property_id) {
    case PROP_DEVICE:
      g_value_set_string (value, linsyssdisink->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_linsys_sdi_sink_dispose (GObject * object)
{
  GstLinsysSdiSink *linsyssdisink;

  g_return_if_fail (GST_IS_LINSYS_SDI_SINK (object));
  linsyssdisink = GST_LINSYS_SDI_SINK (object);

  /* clean up as possible.  may be called multiple times */
  g_free (linsyssdisink->device);
  linsyssdisink->device = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_linsys_sdi_sink_finalize (GObject * object)
{
  g_return_if_fail (GST_IS_LINSYS_SDI_SINK (object));

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}



static GstCaps *
gst_linsys_sdi_sink_get_caps (GstBaseSink * sink)
{
  GST_ERROR_OBJECT (sink, "get_caps");

  return NULL;
}

static gboolean
gst_linsys_sdi_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  GST_ERROR_OBJECT (sink, "set_caps");

  return TRUE;
}

static GstFlowReturn
gst_linsys_sdi_sink_buffer_alloc (GstBaseSink * sink, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GST_ERROR_OBJECT (sink, "buffer_alloc");

  return GST_FLOW_ERROR;
}

static void
gst_linsys_sdi_sink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{

}

static gboolean
gst_linsys_sdi_sink_start (GstBaseSink * sink)
{
  GstLinsysSdiSink *linsyssdisink = GST_LINSYS_SDI_SINK (sink);
  int fd;

  GST_ERROR_OBJECT (sink, "start");

  fd = open (linsyssdisink->device, O_WRONLY, 0);
  if (fd < 0) {
    GST_ERROR_OBJECT (sink, "failed to open device");
    return FALSE;
  }

  linsyssdisink->fd = fd;
  linsyssdisink->tmpdata = g_malloc (858 * 525 * 2);

  return TRUE;
}

static gboolean
gst_linsys_sdi_sink_stop (GstBaseSink * sink)
{
  GstLinsysSdiSink *linsyssdisink = GST_LINSYS_SDI_SINK (sink);

  GST_ERROR_OBJECT (sink, "stop");

  if (linsyssdisink->fd > 0) {
    close (linsyssdisink->fd);
  }
  g_free (linsyssdisink->tmpdata);
  linsyssdisink->tmpdata = NULL;

  return TRUE;
}

static gboolean
gst_linsys_sdi_sink_unlock (GstBaseSink * sink)
{
  GST_ERROR_OBJECT (sink, "unlock");

  return TRUE;
}

static gboolean
gst_linsys_sdi_sink_event (GstBaseSink * sink, GstEvent * event)
{
  GST_ERROR_OBJECT (sink, "event");

  return TRUE;
}

static GstFlowReturn
gst_linsys_sdi_sink_preroll (GstBaseSink * sink, GstBuffer * buffer)
{
  GST_ERROR_OBJECT (sink, "preroll");

  return GST_FLOW_OK;
}

#define EAV 0x74
#define SAV 0x80

static int
get_av (int f, int v, int h)
{
  static int table[] = {
    0x80, 0x9d, 0xab, 0xb6, 0xc7, 0xda, 0xec, 0xf1
  };

  return table[(f << 2) | (v << 1) | h];
}

static void
sdi_mux (guint8 * data, GstBuffer * buffer)
{
  int j;
  int i;
  guint8 *dest;
  int f, v;
  int line;

  for (j = 0; j < 525; j++) {
    dest = data + 858 * 2 * j;

    line = (j + 4) % 525;

    if (line < 10 || (line >= 264 && line < 273)) {
      v = 1;
    } else {
      v = 0;
    }

    if (line >= 266 || line < 4) {
      f = 1;
    } else {
      f = 0;
    }

    dest[0] = 0xff;
    dest[1] = 0;
    dest[2] = 0;
    dest[3] = get_av (f, v, 1);

    for (i = 1; i < (858 - 720) / 2 - 1; i++) {
      dest[i * 4 + 0] = 0x200 >> 2;
      dest[i * 4 + 1] = 0x040 >> 2;
      dest[i * 4 + 2] = 0x200 >> 2;
      dest[i * 4 + 3] = 0x040 >> 2;
    }

    i = (858 - 720) / 2 - 1;
    dest[i * 4 + 0] = 0xff;
    dest[i * 4 + 1] = 0x00;
    dest[i * 4 + 2] = 0x00;
    dest[3] = get_av (f, v, 0);

    i = (858 - 720) / 2;
    if (line >= 23 && line <= 262) {
      int src_line = (line - 23) * 2 + 1;
      memcpy (dest + i * 4, GST_BUFFER_DATA (buffer) + 720 * 2 * src_line,
          720 * 2);
    } else if (line >= 285 && line <= 525) {
      int src_line = (line - 285) * 2 + 0;
      memcpy (dest + i * 4, GST_BUFFER_DATA (buffer) + 720 * 2 * src_line,
          720 * 2);
    } else {
      for (i = (858 - 720) / 2; i < 858 / 2; i++) {
        dest[i * 4 + 0] = 0x200 >> 2;
        dest[i * 4 + 1] = 0x040 >> 2;
        dest[i * 4 + 2] = 0x200 >> 2;
        dest[i * 4 + 3] = 0x040 >> 2;
      }
    }
  }

}

static GstFlowReturn
gst_linsys_sdi_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstLinsysSdiSink *linsyssdisink = GST_LINSYS_SDI_SINK (sink);
  int ret;
  struct pollfd pfd;
  int offset;
  guint8 *data = linsyssdisink->tmpdata;

  GST_ERROR_OBJECT (sink, "render");

  sdi_mux (data, buffer);

  offset = 0;
#define SIZE (858*525*2)
  while (offset < SIZE) {
    pfd.fd = linsyssdisink->fd;
    pfd.events = POLLOUT | POLLPRI;
    ret = poll (&pfd, 1, -1);
    if (ret < 0) {
      GST_ERROR_OBJECT (sink, "poll failed %d", ret);
      return GST_FLOW_ERROR;
    }

    if (pfd.revents & POLLOUT) {
      ret = write (linsyssdisink->fd, data + offset, SIZE - offset);
      if (ret < 0) {
        GST_ERROR_OBJECT (sink, "write failed %d", ret);
        return GST_FLOW_ERROR;
      }
      offset += ret;
    }
    if (pfd.revents & POLLPRI) {
      long val;

      ret = ioctl (linsyssdisink->fd, SDIVIDEO_IOC_TXGETEVENTS, &val);
      if (ret < 0) {
        GST_ERROR_OBJECT (sink, "ioctl failed %d", ret);
        return GST_FLOW_ERROR;
      }
      if (val & SDIVIDEO_EVENT_TX_BUFFER) {
        GST_ERROR_OBJECT (sink, "transmit buffer underrun");
        return GST_FLOW_ERROR;
      }
      if (val & SDIVIDEO_EVENT_TX_FIFO) {
        GST_ERROR_OBJECT (sink, "transmit FIFO underrun");
        return GST_FLOW_ERROR;
      }
      if (val & SDIVIDEO_EVENT_TX_DATA) {
        GST_ERROR_OBJECT (sink, "transmit status change");
      }
    }
  }

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_linsys_sdi_sink_async_play (GstBaseSink * sink)
{
  GST_ERROR_OBJECT (sink, "render");

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
gst_linsys_sdi_sink_activate_pull (GstBaseSink * sink, gboolean active)
{
  GST_ERROR_OBJECT (sink, "activate_pull");

  return TRUE;
}

static void
gst_linsys_sdi_sink_fixate (GstBaseSink * sink, GstCaps * caps)
{
  GST_ERROR_OBJECT (sink, "fixate");

}

static gboolean
gst_linsys_sdi_sink_unlock_stop (GstBaseSink * sink)
{
  GST_ERROR_OBJECT (sink, "unlock_stop");

  return TRUE;
}

static GstFlowReturn
gst_linsys_sdi_sink_render_list (GstBaseSink * sink,
    GstBufferList * buffer_list)
{
  GST_ERROR_OBJECT (sink, "render_list");

  return GST_FLOW_OK;
}
