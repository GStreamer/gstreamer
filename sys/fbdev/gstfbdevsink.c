/* GStreamer fbdev plugin
 * Copyright (C) 2007 Sean D'Epagnier <sean@depagnier.com>
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

/* currently the driver does not switch modes, instead uses current mode.
   the video is centered and cropped if needed to fit onscreen.
   Whatever bitdepth is set is used, and tested to work for 16, 24, 32 bits
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "_stdint.h"

#include "gstfbdevsink.h"

enum
{
  ARG_0,
  ARG_DEVICE,
};

static void gst_fbdevsink_base_init (gpointer g_class);
static void gst_fbdevsink_class_init (GstFBDEVSinkClass * klass);
static void gst_fbdevsink_get_times (GstBaseSink * basesink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);

static gboolean gst_fbdevsink_setcaps (GstBaseSink * bsink, GstCaps * caps);

static GstFlowReturn gst_fbdevsink_render (GstBaseSink * bsink,
    GstBuffer * buff);
static gboolean gst_fbdevsink_start (GstBaseSink * bsink);
static gboolean gst_fbdevsink_stop (GstBaseSink * bsink);

static void gst_fbdevsink_finalize (GObject * object);
static void gst_fbdevsink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_fbdevsink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn
gst_fbdevsink_change_state (GstElement * element, GstStateChange transition);

static GstCaps *gst_fbdevsink_getcaps (GstBaseSink * bsink);

static GstVideoSinkClass *parent_class = NULL;

#define GST_FBDEV_TEMPLATE_CAPS \
     GST_VIDEO_CAPS_RGB_15 \
 ";" GST_VIDEO_CAPS_RGB_16 \
 ";" GST_VIDEO_CAPS_BGR \
 ";" GST_VIDEO_CAPS_BGRx \
 ";" GST_VIDEO_CAPS_xBGR \
 ";" GST_VIDEO_CAPS_RGB \
 ";" GST_VIDEO_CAPS_RGBx \
 ";" GST_VIDEO_CAPS_xRGB \

static void
gst_fbdevsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS (GST_FBDEV_TEMPLATE_CAPS)
      );

  gst_element_class_set_details_simple (element_class, "fbdev video sink",
      "Sink/Video",
      "A linux framebuffer videosink", "Sean D'Epagnier <sean@depagnier.com>");
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
}


static void
gst_fbdevsink_get_times (GstBaseSink * basesink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstFBDEVSink *fbdevsink;

  fbdevsink = GST_FBDEVSINK (basesink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    *start = GST_BUFFER_TIMESTAMP (buffer);
    if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
      *end = *start + GST_BUFFER_DURATION (buffer);
    } else {
      if (fbdevsink->fps_n > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, fbdevsink->fps_d,
            fbdevsink->fps_n);
      }
    }
  }
}

static uint32_t
swapendian (uint32_t val)
{
  return (val & 0xff) << 24 | (val & 0xff00) << 8
      | (val & 0xff0000) >> 8 | (val & 0xff000000) >> 24;
}

static GstCaps *
gst_fbdevsink_getcaps (GstBaseSink * bsink)
{
  GstFBDEVSink *fbdevsink;
  GstCaps *caps;
  uint32_t rmask;
  uint32_t gmask;
  uint32_t bmask;
  int endianness;

  fbdevsink = GST_FBDEVSINK (bsink);

  if (!fbdevsink->framebuffer)
    return gst_caps_from_string (GST_FBDEV_TEMPLATE_CAPS);

  rmask = ((1 << fbdevsink->varinfo.red.length) - 1)
      << fbdevsink->varinfo.red.offset;
  gmask = ((1 << fbdevsink->varinfo.green.length) - 1)
      << fbdevsink->varinfo.green.offset;
  bmask = ((1 << fbdevsink->varinfo.blue.length) - 1)
      << fbdevsink->varinfo.blue.offset;

  endianness = 0;

  switch (fbdevsink->varinfo.bits_per_pixel) {
    case 32:
      /* swap endian of masks */
      rmask = swapendian (rmask);
      gmask = swapendian (gmask);
      bmask = swapendian (bmask);
      endianness = 4321;
      break;
    case 24:{
      /* swap red and blue masks */
      uint32_t t = rmask;

      rmask = bmask;
      bmask = t;
      endianness = 4321;
      break;
    }
    case 15:
    case 16:
      endianness = 1234;
      break;
    default:
      /* other bit depths are not supported */
      g_warning ("unsupported bit depth: %d\n",
          fbdevsink->varinfo.bits_per_pixel);
      return NULL;
  }

  /* replace all but width, height, and framerate */
  caps = gst_caps_from_string (GST_VIDEO_CAPS_RGB_15);
  gst_caps_set_simple (caps,
      "bpp", G_TYPE_INT, fbdevsink->varinfo.bits_per_pixel,
      "depth", G_TYPE_INT, fbdevsink->varinfo.red.length +
      fbdevsink->varinfo.green.length +
      fbdevsink->varinfo.blue.length +
      fbdevsink->varinfo.transp.length,
      "endianness", G_TYPE_INT, endianness,
      "red_mask", G_TYPE_INT, rmask,
      "green_mask", G_TYPE_INT, gmask, "blue_mask", G_TYPE_INT, bmask, NULL);

  return caps;
}

static gboolean
gst_fbdevsink_setcaps (GstBaseSink * bsink, GstCaps * vscapslist)
{
  GstFBDEVSink *fbdevsink;
  GstStructure *structure;
  const GValue *fps;

  fbdevsink = GST_FBDEVSINK (bsink);

  structure = gst_caps_get_structure (vscapslist, 0);

  fps = gst_structure_get_value (structure, "framerate");
  fbdevsink->fps_n = gst_value_get_fraction_numerator (fps);
  fbdevsink->fps_d = gst_value_get_fraction_denominator (fps);

  gst_structure_get_int (structure, "width", &fbdevsink->width);
  gst_structure_get_int (structure, "height", &fbdevsink->height);

  /* calculate centering and scanlengths for the video */
  fbdevsink->bytespp = fbdevsink->fixinfo.line_length / fbdevsink->varinfo.xres;

  fbdevsink->cx = ((int) fbdevsink->varinfo.xres - fbdevsink->width) / 2;
  if (fbdevsink->cx < 0)
    fbdevsink->cx = 0;

  fbdevsink->cy = ((int) fbdevsink->varinfo.yres - fbdevsink->height) / 2;
  if (fbdevsink->cy < 0)
    fbdevsink->cy = 0;

  fbdevsink->linelen = fbdevsink->width * fbdevsink->bytespp;
  if (fbdevsink->linelen > fbdevsink->fixinfo.line_length)
    fbdevsink->linelen = fbdevsink->fixinfo.line_length;

  fbdevsink->lines = fbdevsink->height;
  if (fbdevsink->lines > fbdevsink->varinfo.yres)
    fbdevsink->lines = fbdevsink->varinfo.yres;

  return TRUE;
}


static GstFlowReturn
gst_fbdevsink_render (GstBaseSink * bsink, GstBuffer * buf)
{

  GstFBDEVSink *fbdevsink;
  int i;

  fbdevsink = GST_FBDEVSINK (bsink);

  /* optimization could remove this memcpy by allocating the buffer
     in framebuffer memory, but would only work when xres matches
     the video width */

  for (i = 0; i < fbdevsink->lines; i++)
    memcpy (fbdevsink->framebuffer
        + (i + fbdevsink->cy) * fbdevsink->fixinfo.line_length
        + fbdevsink->cx * fbdevsink->bytespp,
        GST_BUFFER_DATA (buf) + i * fbdevsink->width * fbdevsink->bytespp,
        fbdevsink->linelen);

  return GST_FLOW_OK;
}

static gboolean
gst_fbdevsink_start (GstBaseSink * bsink)
{
  GstFBDEVSink *fbdevsink;

  fbdevsink = GST_FBDEVSINK (bsink);

  if (!fbdevsink->device) {
    fbdevsink->device = g_strdup ("/dev/fb0");
  }

  fbdevsink->fd = open (fbdevsink->device, O_RDWR);

  if (fbdevsink->fd == -1)
    return FALSE;

  /* get the fixed screen info */
  if (ioctl (fbdevsink->fd, FBIOGET_FSCREENINFO, &fbdevsink->fixinfo))
    return FALSE;

  /* get the variable screen info */
  if (ioctl (fbdevsink->fd, FBIOGET_VSCREENINFO, &fbdevsink->varinfo))
    return FALSE;

  /* map the framebuffer */
  fbdevsink->framebuffer = mmap (0, fbdevsink->fixinfo.smem_len,
      PROT_WRITE, MAP_SHARED, fbdevsink->fd, 0);
  if (fbdevsink->framebuffer == MAP_FAILED)
    return FALSE;

  return TRUE;
}

static gboolean
gst_fbdevsink_stop (GstBaseSink * bsink)
{
  GstFBDEVSink *fbdevsink;

  fbdevsink = GST_FBDEVSINK (bsink);

  if (munmap (fbdevsink->framebuffer, fbdevsink->fixinfo.smem_len))
    return FALSE;

  if (close (fbdevsink->fd))
    return FALSE;


  return TRUE;
}

static void
gst_fbdevsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFBDEVSink *fbdevsink;

  fbdevsink = GST_FBDEVSINK (object);

  switch (prop_id) {
    case ARG_DEVICE:{
      g_free (fbdevsink->device);
      fbdevsink->device = g_value_dup_string (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_fbdevsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFBDEVSink *fbdevsink;

  fbdevsink = GST_FBDEVSINK (object);

  switch (prop_id) {
    case ARG_DEVICE:{
      g_value_set_string (value, fbdevsink->device);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_fbdevsink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_FBDEVSINK (element), GST_STATE_CHANGE_FAILURE);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }
  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "fbdevsink", GST_RANK_NONE,
          GST_TYPE_FBDEVSINK))
    return FALSE;

  return TRUE;
}

static void
gst_fbdevsink_class_init (GstFBDEVSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstvs_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstvs_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_fbdevsink_set_property;
  gobject_class->get_property = gst_fbdevsink_get_property;
  gobject_class->finalize = gst_fbdevsink_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_fbdevsink_change_state);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE,
      g_param_spec_string ("device", "device",
          "The framebuffer device eg: /dev/fb0", NULL, G_PARAM_READWRITE));

  gstvs_class->set_caps = GST_DEBUG_FUNCPTR (gst_fbdevsink_setcaps);
  gstvs_class->get_caps = GST_DEBUG_FUNCPTR (gst_fbdevsink_getcaps);
  gstvs_class->get_times = GST_DEBUG_FUNCPTR (gst_fbdevsink_get_times);
  gstvs_class->preroll = GST_DEBUG_FUNCPTR (gst_fbdevsink_render);
  gstvs_class->render = GST_DEBUG_FUNCPTR (gst_fbdevsink_render);
  gstvs_class->start = GST_DEBUG_FUNCPTR (gst_fbdevsink_start);
  gstvs_class->stop = GST_DEBUG_FUNCPTR (gst_fbdevsink_stop);

}

static void
gst_fbdevsink_finalize (GObject * object)
{
  GstFBDEVSink *fbdevsink = GST_FBDEVSINK (object);

  g_free (fbdevsink->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

GType
gst_fbdevsink_get_type (void)
{
  static GType fbdevsink_type = 0;

  if (!fbdevsink_type) {
    static const GTypeInfo fbdevsink_info = {
      sizeof (GstFBDEVSinkClass),
      gst_fbdevsink_base_init,
      NULL,
      (GClassInitFunc) gst_fbdevsink_class_init,
      NULL,
      NULL,
      sizeof (GstFBDEVSink),
      0,
      NULL
    };

    fbdevsink_type =
        g_type_register_static (GST_TYPE_BASE_SINK, "GstFBDEVSink",
        &fbdevsink_info, 0);
  }
  return fbdevsink_type;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "fbdevsink",
    "linux framebuffer video sink",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
