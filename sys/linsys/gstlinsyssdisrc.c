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
#include <gst/base/gstbasesrc.h>
#include "gstlinsyssdisrc.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>

#include "sdivideo.h"

/* prototypes */


static void gst_linsys_sdi_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_linsys_sdi_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_linsys_sdi_src_dispose (GObject * object);
static void gst_linsys_sdi_src_finalize (GObject * object);

static GstCaps *gst_linsys_sdi_src_get_caps (GstBaseSrc * src);
static gboolean gst_linsys_sdi_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_linsys_sdi_src_negotiate (GstBaseSrc * src);
static gboolean gst_linsys_sdi_src_newsegment (GstBaseSrc * src);
static gboolean gst_linsys_sdi_src_start (GstBaseSrc * src);
static gboolean gst_linsys_sdi_src_stop (GstBaseSrc * src);
static void
gst_linsys_sdi_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_linsys_sdi_src_get_size (GstBaseSrc * src, guint64 * size);
static gboolean gst_linsys_sdi_src_is_seekable (GstBaseSrc * src);
static gboolean gst_linsys_sdi_src_unlock (GstBaseSrc * src);
static gboolean gst_linsys_sdi_src_event (GstBaseSrc * src, GstEvent * event);
static GstFlowReturn
gst_linsys_sdi_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf);
static gboolean gst_linsys_sdi_src_do_seek (GstBaseSrc * src,
    GstSegment * segment);
static gboolean gst_linsys_sdi_src_query (GstBaseSrc * src, GstQuery * query);
static gboolean gst_linsys_sdi_src_check_get_range (GstBaseSrc * src);
static void gst_linsys_sdi_src_fixate (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_linsys_sdi_src_unlock_stop (GstBaseSrc * src);
static gboolean
gst_linsys_sdi_src_prepare_seek_segment (GstBaseSrc * src, GstEvent * seek,
    GstSegment * segment);

enum
{
  PROP_0,
  PROP_DEVICE
};

#define DEFAULT_DEVICE "/dev/sdirx0"

GST_DEBUG_CATEGORY (gst_linsys_sdi_src_debug);
#define GST_CAT_DEFAULT gst_linsys_sdi_src_debug

/* pad templates */

static GstStaticPadTemplate gst_linsys_sdi_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv,format=(fourcc)UYVY,"
        "width=720,height=480,pixel-aspect-ratio=10/11,"
        "framerate=30000/1001,interlaced=true,"
        "colorspec=sdtv,chroma-site=mpeg2")
    );

/* class initialization */

GST_BOILERPLATE (GstLinsysSdiSrc, gst_linsys_sdi_src, GstBaseSrc,
    GST_TYPE_BASE_SRC);

static void
gst_linsys_sdi_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_linsys_sdi_src_src_template));

  gst_element_class_set_static_metadata (element_class, "SDI video source",
      "Source/Video", "Reads video from SDI capture device",
      "David Schleef <ds@entropywave.com>");
}

static void
gst_linsys_sdi_src_class_init (GstLinsysSdiSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_linsys_sdi_src_debug, "linsyssdisrc", 0,
      "FIXME");

  gobject_class->set_property = gst_linsys_sdi_src_set_property;
  gobject_class->get_property = gst_linsys_sdi_src_get_property;
  gobject_class->dispose = gst_linsys_sdi_src_dispose;
  gobject_class->finalize = gst_linsys_sdi_src_finalize;
  base_src_class->get_caps = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_get_caps);
  base_src_class->set_caps = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_set_caps);
  if (0)
    base_src_class->negotiate =
        GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_negotiate);
  base_src_class->newsegment =
      GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_newsegment);
  base_src_class->start = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_stop);
  base_src_class->get_times = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_get_times);
  base_src_class->get_size = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_get_size);
  base_src_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_is_seekable);
  base_src_class->unlock = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_unlock);
  base_src_class->event = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_event);
  base_src_class->create = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_create);
  if (0)
    base_src_class->do_seek = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_do_seek);
  base_src_class->query = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_query);
  base_src_class->check_get_range =
      GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_check_get_range);
  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_fixate);
  base_src_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_unlock_stop);
  base_src_class->prepare_seek_segment =
      GST_DEBUG_FUNCPTR (gst_linsys_sdi_src_prepare_seek_segment);

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device", "device to transmit data on",
          DEFAULT_DEVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_linsys_sdi_src_init (GstLinsysSdiSrc * linsyssdisrc,
    GstLinsysSdiSrcClass * linsyssdisrc_class)
{

  gst_base_src_set_live (GST_BASE_SRC (linsyssdisrc), TRUE);
  gst_base_src_set_blocksize (GST_BASE_SRC (linsyssdisrc), 720 * 480 * 2);

  linsyssdisrc->device = g_strdup (DEFAULT_DEVICE);

  linsyssdisrc->is_625 = FALSE;
  linsyssdisrc->fd = -1;
}

void
gst_linsys_sdi_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLinsysSdiSrc *linsyssdisrc;

  g_return_if_fail (GST_IS_LINSYS_SDI_SRC (object));
  linsyssdisrc = GST_LINSYS_SDI_SRC (object);

  switch (property_id) {
    case PROP_DEVICE:
      g_free (linsyssdisrc->device);
      linsyssdisrc->device = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_linsys_sdi_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstLinsysSdiSrc *linsyssdisrc;

  g_return_if_fail (GST_IS_LINSYS_SDI_SRC (object));
  linsyssdisrc = GST_LINSYS_SDI_SRC (object);

  switch (property_id) {
    case PROP_DEVICE:
      g_value_set_string (value, linsyssdisrc->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_linsys_sdi_src_dispose (GObject * object)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (object);
  g_return_if_fail (linsyssdisrc != NULL);

  /* clean up as possible.  may be called multiple times */
  g_free (linsyssdisrc->device);
  linsyssdisrc->device = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_linsys_sdi_src_finalize (GObject * object)
{
  g_return_if_fail (GST_IS_LINSYS_SDI_SRC (object));

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GstCaps *
gst_linsys_sdi_src_get_caps (GstBaseSrc * src)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "get_caps");

  return NULL;
}

static gboolean
gst_linsys_sdi_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "set_caps");

  return TRUE;
}

static gboolean
gst_linsys_sdi_src_negotiate (GstBaseSrc * src)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "negotiate");

  return TRUE;
}

static gboolean
gst_linsys_sdi_src_newsegment (GstBaseSrc * src)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "newsegment");

  return TRUE;
}

static gboolean
gst_linsys_sdi_src_start (GstBaseSrc * src)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);
  int fd;

  GST_DEBUG_OBJECT (linsyssdisrc, "start");

  fd = open (linsyssdisrc->device, O_RDONLY);
  if (fd < 0) {
    GST_ERROR_OBJECT (src, "failed to open device");
    return FALSE;
  }

  linsyssdisrc->fd = fd;

  if (linsyssdisrc->is_625) {
    linsyssdisrc->tmpdata = g_malloc (864 * 625 * 2);
  } else {
    linsyssdisrc->tmpdata = g_malloc (858 * 525 * 2);
  }
  linsyssdisrc->have_sync = FALSE;

  return TRUE;
}

static gboolean
gst_linsys_sdi_src_stop (GstBaseSrc * src)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "stop");

#if 0
  if (linsyssdisrc->fd > 0) {
    close (linsyssdisrc->fd);
    linsyssdisrc->fd = -1;
  }
  g_free (linsyssdisrc->tmpdata);
  linsyssdisrc->tmpdata = NULL;
#endif

  return TRUE;
}

static void
gst_linsys_sdi_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "get_times");
}

static gboolean
gst_linsys_sdi_src_get_size (GstBaseSrc * src, guint64 * size)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "get_size");

  return FALSE;
}

static gboolean
gst_linsys_sdi_src_is_seekable (GstBaseSrc * src)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "is_seekable");

  return FALSE;
}

static gboolean
gst_linsys_sdi_src_unlock (GstBaseSrc * src)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "unlock");

  return TRUE;
}

static gboolean
gst_linsys_sdi_src_event (GstBaseSrc * src, GstEvent * event)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "event");

  return TRUE;
}

static void
sdi_demux (guint8 * data, GstBuffer * buf, gboolean is_625)
{
  int j;
  int line;
  int offset;

  if (is_625) {
    offset = (864 - 720) / 2;

    for (j = 0; j < 480; j++) {
      if (j & 1) {
        line = 23 + (j - 1) / 2;
      } else {
        line = 335 + j / 2;
      }
      memcpy (GST_BUFFER_DATA (buf) + j * 720 * 2,
          data + (line - 1) * 864 * 2 + offset * 4, 720 * 2);
    }
  } else {
    offset = (858 - 720) / 2;

    for (j = 0; j < 480; j++) {
      if (j & 1) {
        line = 23 + (j - 1) / 2;
      } else {
        line = 285 + j / 2;
      }
      memcpy (GST_BUFFER_DATA (buf) + j * 720 * 2,
          data + (line - 1) * 858 * 2 + offset * 4, 720 * 2);
    }
  }

}

static GstFlowReturn
gst_linsys_sdi_src_create (GstBaseSrc * src, guint64 _offset, guint size,
    GstBuffer ** buf)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);
  int offset;
  int ret;
  struct pollfd pfd;
  int sdi_size;
  int sdi_width;
  guint8 *data = linsyssdisrc->tmpdata;

  if (linsyssdisrc->fd < 0)
    return GST_FLOW_FLUSHING;

  if (linsyssdisrc->is_625) {
    sdi_width = 864;
    sdi_size = 864 * 625 * 2;
  } else {
    sdi_width = 858;
    sdi_size = 858 * 525 * 2;
  }

  GST_DEBUG_OBJECT (linsyssdisrc, "create size=%d fd=%d", size,
      linsyssdisrc->fd);

  offset = 0;
  while (offset < sdi_size) {
    pfd.fd = linsyssdisrc->fd;
    pfd.events = POLLIN | POLLPRI;
    ret = poll (&pfd, 1, 1000);
    if (ret < 0) {
      GST_ERROR_OBJECT (src, "poll failed %d", ret);
      return GST_FLOW_ERROR;
    }

    if (pfd.revents & POLLIN) {
      if (linsyssdisrc->have_sync) {
        ret = read (linsyssdisrc->fd, data + offset, sdi_size - offset);
      } else {
        ret = read (linsyssdisrc->fd, data + offset, sdi_width * 2);
      }
      if (ret < 0) {
        GST_ERROR_OBJECT (src, "read failed %d", ret);
        return GST_FLOW_ERROR;
      }

      if (!linsyssdisrc->have_sync) {
        int v = (data[3] >> 5) & 1;
        int f = (data[3] >> 6) & 1;
        if (!linsyssdisrc->have_vblank && (f == 0) && (v == 1)) {
          linsyssdisrc->have_vblank = TRUE;
        } else if (linsyssdisrc->have_vblank && (f == 0) && (v == 0)) {
          offset += sdi_width * 2 * 9;
          linsyssdisrc->have_sync = TRUE;
          offset += ret;
        }
      } else {
        offset += ret;
      }
    }
    if (pfd.revents & POLLPRI) {
      long val;

      ret = ioctl (linsyssdisrc->fd, SDIVIDEO_IOC_RXGETEVENTS, &val);
      if (ret < 0) {
        GST_ERROR_OBJECT (src, "ioctl failed %d", ret);
        return GST_FLOW_ERROR;
      }
      if (val & SDIVIDEO_EVENT_RX_BUFFER) {
        GST_ERROR_OBJECT (src, "receive buffer overrun");
        return GST_FLOW_ERROR;
      }
      if (val & SDIVIDEO_EVENT_RX_FIFO) {
        GST_ERROR_OBJECT (src, "receive FIFO overrun");
        return GST_FLOW_ERROR;
      }
      if (val & SDIVIDEO_EVENT_RX_CARRIER) {
        GST_ERROR_OBJECT (src, "carrier status change");
      }
    }
  }

  *buf = gst_buffer_new_and_alloc (size);
  sdi_demux (data, *buf, linsyssdisrc->is_625);

  return GST_FLOW_OK;
}

static gboolean
gst_linsys_sdi_src_do_seek (GstBaseSrc * src, GstSegment * segment)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "do_seek");

  return FALSE;
}

static gboolean
gst_linsys_sdi_src_query (GstBaseSrc * src, GstQuery * query)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "query");

  return TRUE;
}

static gboolean
gst_linsys_sdi_src_check_get_range (GstBaseSrc * src)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "get_range");

  return FALSE;
}

static void
gst_linsys_sdi_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "fixate");
}

static gboolean
gst_linsys_sdi_src_unlock_stop (GstBaseSrc * src)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "stop");

  return TRUE;
}

static gboolean
gst_linsys_sdi_src_prepare_seek_segment (GstBaseSrc * src, GstEvent * seek,
    GstSegment * segment)
{
  GstLinsysSdiSrc *linsyssdisrc = GST_LINSYS_SDI_SRC (src);

  GST_DEBUG_OBJECT (linsyssdisrc, "seek_segment");

  return FALSE;
}
