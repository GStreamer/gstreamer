/* GStreamer DXR3 Hardware MPEG video decoder plugin
 * Copyright (C) <2002> Rehan Khwaja <rehankhwaja@yahoo.com>
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


#include <string.h>
#include <fcntl.h>
#include "gstdxr3videosink.h"
#include <sys/ioctl.h>
#include <linux/em8300.h>


enum {
  ARG_0,
  ARG_TV_MODE,
  ARG_DEVICE,
  ARG_ASPECT_RATIO
  /* TODO -
   * DIGITAL/ANALOG AUDIO OUT
   * OVERLAY MODE
   * SET BCS??  what's that?
   * OVERLAY ATTRIBUTES - where on the screen to put image
   * OVERLAY WINDOW - as above
   * OVERLAY KEYCOLOR
   * OVERLAY SIGNAL MODE
   * PLAYMODE - pausing, playing, slow forward etc
   */
};

static void	gst_dxr3_video_sink_class_init	(GstDxr3VideoSinkClass *klass);
static void	gst_dxr3_video_sink_init		(GstDxr3VideoSink *example);

static void	gst_dxr3_video_sink_chain		(GstPad *pad, GstBuffer *buf);

static void	gst_dxr3_video_sink_set_property	(GObject *object, guint prop_id,
						 const GValue *value, GParamSpec *pspec);
static void	gst_dxr3_video_sink_get_property	(GObject *object, guint prop_id,
						 GValue *value, GParamSpec *pspec);
static gboolean	gst_dxr3_video_sink_handle_event	(GstPad *pad, GstEvent *event);
static void	gst_dxr3_video_sink_set_clock		(GstElement *element, GstClock *clock);


static GstElementClass *parent_class = NULL;


#define GST_TYPE_DXR3_VIDEO_SINK_ASPECT_RATIOS (gst_dxr3_video_sink_aspect_ratios_get_type())
static GType
gst_dxr3_video_sink_aspect_ratios_get_type(void) {
  static GType dxr3_video_sink_aspect_ratio_type = 0;
  static GEnumValue dxr3_video_sink_aspect_ratios[] = {
    {0, "0", "4:3"},
    {1, "1", "16:9"},
    {0, NULL, NULL},
  };
  if (!dxr3_video_sink_aspect_ratio_type) {
    dxr3_video_sink_aspect_ratio_type = g_enum_register_static("GstDxr3VideoSinkAspectRatios", dxr3_video_sink_aspect_ratios);
  }
  return dxr3_video_sink_aspect_ratio_type;
}

#define GST_TYPE_DXR3_VIDEO_SINK_TV_MODES (gst_dxr3_video_sink_tv_modes_get_type())
static GType
gst_dxr3_video_sink_tv_modes_get_type(void) {
  static GType dxr3_video_sink_tv_mode_type = 0;
  static GEnumValue dxr3_video_sink_tv_modes[] = {
    {0, "0", "NTSC"},
    {1, "1", "PAL"},
    {0, NULL, NULL},
  };
  if (!dxr3_video_sink_tv_mode_type) {
    dxr3_video_sink_tv_mode_type = g_enum_register_static("GstDxr3VideoSinkTvModes", dxr3_video_sink_tv_modes);
  }
  return dxr3_video_sink_tv_mode_type;
}

static void
gst_dxr3_video_sink_set_clock(GstElement *element, GstClock *clock)
{
  GstDxr3VideoSink *dxr3_video_sink;

  dxr3_video_sink = GST_DXR3_VIDEO_SINK(element);
  dxr3_video_sink->clock = clock;
}

GType
gst_dxr3_video_sink_get_type(void)
{
  static GType dxr3_video_sink_type = 0;

  if (!dxr3_video_sink_type) {
    static const GTypeInfo dxr3_video_sink_info = {
      sizeof(GstDxr3VideoSinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_dxr3_video_sink_class_init,
      NULL,
      NULL,
      sizeof(GstDxr3VideoSink),
      0,
      (GInstanceInitFunc)gst_dxr3_video_sink_init,
    };
    dxr3_video_sink_type = g_type_register_static(GST_TYPE_ELEMENT, "GstDxr3VideoSink", &dxr3_video_sink_info, 0);
  }
  return dxr3_video_sink_type;
}

static gchar *
gst_dxr3_video_sink_get_control_device(GstDxr3VideoSink *dxr3_video_sink)
{
  return g_strdup_printf("/dev/em8300-%d", dxr3_video_sink->device_number);
}

static gchar *
gst_dxr3_video_sink_get_video_device(GstDxr3VideoSink *dxr3_video_sink)
{
  return g_strdup_printf("/dev/em8300_mv-%d", dxr3_video_sink->device_number);
}

static void
gst_dxr3_video_sink_class_init (GstDxr3VideoSinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TV_MODE,
    g_param_spec_enum("tv-mode","tv-mode","sets NTSC/PAL output format",
                     GST_TYPE_DXR3_VIDEO_SINK_TV_MODES, 0, G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE,
    g_param_spec_int("device","device","sets/returns board number",
                     0,3,0,G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ASPECT_RATIO,
    g_param_spec_enum("aspect-ratio", "aspect-ratio", "sets/returns aspect ratio",
                     GST_TYPE_DXR3_VIDEO_SINK_ASPECT_RATIOS, 0, G_PARAM_READWRITE));

  gobject_class->set_property = gst_dxr3_video_sink_set_property;
  gobject_class->get_property = gst_dxr3_video_sink_get_property;
}

static void
gst_dxr3_video_sink_open_device(GstDxr3VideoSink *dxr3_video_sink)
{
  gchar *video_device_path;

  video_device_path = gst_dxr3_video_sink_get_video_device(dxr3_video_sink);
  if (dxr3_video_sink->device){
    fclose(dxr3_video_sink->device);
  }
  /* this is what disksink uses */
  dxr3_video_sink->device = fopen(video_device_path, "w");

  g_free(video_device_path);
}

static void
gst_dxr3_video_sink_init(GstDxr3VideoSink *dxr3_video_sink)
{
  dxr3_video_sink->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (dxr3_video_sink_factory), "video_sink");
  gst_pad_set_chain_function(dxr3_video_sink->sinkpad, gst_dxr3_video_sink_chain);
  gst_element_add_pad(GST_ELEMENT(dxr3_video_sink),dxr3_video_sink->sinkpad);

  dxr3_video_sink->device_number = 0;
  dxr3_video_sink->device = 0;
  /* FIXME - should only have device open when necessary
   * which probably requires handling events?
   * also, when is this device closed?
   */
  gst_dxr3_video_sink_open_device(dxr3_video_sink);

  GST_FLAG_SET (GST_ELEMENT(dxr3_video_sink), GST_ELEMENT_EVENT_AWARE);
  gst_pad_set_event_function(dxr3_video_sink->sinkpad, gst_dxr3_video_sink_handle_event);

  dxr3_video_sink->clock = NULL;
  GST_ELEMENT(dxr3_video_sink)->setclockfunc = gst_dxr3_video_sink_set_clock;
}

static gboolean
gst_dxr3_video_sink_handle_event(GstPad *pad, GstEvent *event)
{
  GstEventType type;

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_SEEK:
      g_print("seek event\n\n");
      break;
    case GST_EVENT_NEW_MEDIA:
      g_print("new media event\n\n");
      break;
    case GST_EVENT_FLUSH:
      g_print("flush event\n\n");
      break;
    default:
      g_print("event\n\n");
      gst_pad_event_default (pad, event);
      break;
  }

  return TRUE;
}

static void
gst_dxr3_video_sink_chain (GstPad *pad, GstBuffer *buf)
{
  GstDxr3VideoSink *dxr3_video_sink;
  long pts;

  /* Some of these checks are of dubious value, since if there were not
   * already true, the chain function would never be called.
   */
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  dxr3_video_sink = GST_DXR3_VIDEO_SINK(gst_pad_get_parent (pad));

  g_return_if_fail(dxr3_video_sink != NULL);
  g_return_if_fail(GST_IS_DXR3_VIDEO_SINK(dxr3_video_sink));

  /* Copy the data in the incoming buffer onto the device. */
  fwrite(GST_BUFFER_DATA (buf), 1, GST_BUFFER_SIZE (buf), dxr3_video_sink->device);
  pts = (long)GST_BUFFER_TIMESTAMP(buf);
  if (-1 == ioctl((int)dxr3_video_sink->device, EM8300_IOCTL_VIDEO_SETPTS, &pts)){
    GST_DEBUG(0, "FAILED call to EM8300_IOCTL_VIDEO_SETPTS\n");
  }

  gst_buffer_unref (buf);
}

static void
gst_dxr3_video_sink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstDxr3VideoSink *dxr3_video_sink;
  int device;
  gchar *device_path;
  int tv_mode_ioctl, aspect_ratio_ioctl;

  g_return_if_fail(GST_IS_DXR3_VIDEO_SINK(object));

  dxr3_video_sink = GST_DXR3_VIDEO_SINK(object);

  switch (prop_id) {
    case ARG_TV_MODE:
      device_path = gst_dxr3_video_sink_get_control_device(dxr3_video_sink);
      device = open(device_path, O_WRONLY);
      if (device == -1){
        GST_DEBUG(0, "failed to open control device\n");
	break;
      }
      switch(g_value_get_int(value)){
      case 0:
        tv_mode_ioctl = EM8300_VIDEOMODE_NTSC;
	break;
      case 1:
        tv_mode_ioctl = EM8300_VIDEOMODE_PAL;
	break;
      case 2:
        tv_mode_ioctl = EM8300_VIDEOMODE_PAL60;
	break;
      }
      if (-1 == ioctl(device, EM8300_IOCTL_SET_VIDEOMODE, &tv_mode_ioctl)){
        GST_DEBUG (0,"failed to set tv-mode\n");
      }
      close(device);
      break;
    case ARG_DEVICE:
      dxr3_video_sink->device_number = g_value_get_int(value);
      gst_dxr3_video_sink_open_device(dxr3_video_sink);
      break;
    case ARG_ASPECT_RATIO:
      device_path = gst_dxr3_video_sink_get_control_device(dxr3_video_sink);
      device = open(device_path, O_WRONLY);
      if (device == -1){
        GST_DEBUG(0, "failed to open control device\n");
	break;
      }
      switch(g_value_get_enum(value)){
      case 0:
        aspect_ratio_ioctl = EM8300_ASPECTRATIO_4_3;
	break;
      case 1:
        aspect_ratio_ioctl = EM8300_ASPECTRATIO_16_9;
	break;
      }
      if (-1 == ioctl(device, EM8300_IOCTL_SET_ASPECTRATIO, &aspect_ratio_ioctl)){
        GST_DEBUG(0, "failed to set aspect-ratio\n");
      }
      break;
    default:
      break;
  }
}

static void
gst_dxr3_video_sink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstDxr3VideoSink *dxr3_video_sink;
  gint device;
  gchar *device_path;
  int tv_mode_ioctl, aspect_ratio_ioctl;

  g_return_if_fail(GST_IS_DXR3_VIDEO_SINK(object));
  dxr3_video_sink = GST_DXR3_VIDEO_SINK(object);

  switch (prop_id) {
    case ARG_TV_MODE:
      device_path = gst_dxr3_video_sink_get_control_device(dxr3_video_sink);
      tv_mode_ioctl = 0;
      device = open(device_path, O_WRONLY);
      if (-1 == device){
        GST_DEBUG(0, "failed to open control device\n");
	break;
      }
      if (-1 == ioctl(device, EM8300_IOCTL_GET_VIDEOMODE, &tv_mode_ioctl)){
        GST_DEBUG(0, "failed to get tv-mode\n");
      }
      close(device);

      switch(tv_mode_ioctl){
      case EM8300_VIDEOMODE_NTSC:
        g_value_set_enum(value, 0);
	break;
      case EM8300_VIDEOMODE_PAL:
        g_value_set_enum(value, 1);
	break;
      case EM8300_VIDEOMODE_PAL60:
        g_value_set_enum(value, 2);
	break;
      }
      break;
    case ARG_DEVICE:
      g_value_set_int(value, dxr3_video_sink->device_number);
      break;
    case ARG_ASPECT_RATIO:
      device_path = gst_dxr3_video_sink_get_control_device(dxr3_video_sink);
      aspect_ratio_ioctl = 0;
      device = open(device_path, O_WRONLY);
      if (-1 == device){
        GST_DEBUG(0, "failed to open control device\n");
	break;
      }
      if (-1 == ioctl(device, EM8300_IOCTL_GET_ASPECTRATIO, &aspect_ratio_ioctl)){
        GST_DEBUG(0, "failed to get aspect ratio\n");
      }
      close(device);

      switch(aspect_ratio_ioctl){
      case EM8300_ASPECTRATIO_4_3:
        g_value_set_enum(value, 0);
	break;
      case EM8300_ASPECTRATIO_16_9:
        g_value_set_enum(value, 1);
        break;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
