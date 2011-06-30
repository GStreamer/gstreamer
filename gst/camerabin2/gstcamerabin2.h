/* GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
#ifndef _GST_CAMERA_BIN2_H_
#define _GST_CAMERA_BIN2_H_

#include <gst/gst.h>
#include <gst/pbutils/encoding-profile.h>

G_BEGIN_DECLS

#define GST_TYPE_CAMERA_BIN2   (gst_camera_bin2_get_type())
#define GST_CAMERA_BIN2(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAMERA_BIN2,GstCameraBin2))
#define GST_CAMERA_BIN2_CAST(obj)   ((GstCameraBin2 *) obj)
#define GST_CAMERA_BIN2_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAMERA_BIN2,GstCameraBin2Class))
#define GST_IS_CAMERA_BIN2(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAMERA_BIN2))
#define GST_IS_CAMERA_BIN2_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAMERA_BIN2))

typedef enum
{
  /* matches GstEncFlags GST_ENC_FLAG_NO_AUDIO_CONVERSION in encodebin */
  GST_CAM_FLAG_NO_AUDIO_CONVERSION = (1 << 0),
  /* matches GstEncFlags GST_ENC_FLAG_NO_VIDEO_CONVERSION in encodebin */
  GST_CAM_FLAG_NO_VIDEO_CONVERSION = (1 << 1)
} GstCamFlags;


typedef struct _GstCameraBin2 GstCameraBin2;
typedef struct _GstCameraBin2Class GstCameraBin2Class;

struct _GstCameraBin2
{
  GstPipeline pipeline;

  GstElement *src;
  GstElement *user_src;
  gulong src_capture_notify_id;

  GstElement *video_encodebin;
  gulong video_encodebin_signal_id;
  GstElement *videosink;
  GstElement *videobin_capsfilter;

  GstElement *viewfinderbin;
  GstElement *viewfinderbin_queue;
  GstElement *viewfinderbin_capsfilter;

  GstElement *image_encodebin;
  gulong image_encodebin_signal_id;
  GstElement *imagesink;
  GstElement *imagebin_queue;
  GstElement *imagebin_capsfilter;

  GstElement *video_filter;
  GstElement *image_filter;
  GstElement *viewfinder_filter;
  GstElement *user_video_filter;
  GstElement *user_image_filter;
  GstElement *user_viewfinder_filter;

  GstElement *audio_src;
  GstElement *user_audio_src;
  GstElement *audio_volume;
  GstElement *audio_capsfilter;

  gint processing_counter; /* atomic int */

  /* Index of the auto incrementing file index for captures */
  gint capture_index;

  /* stores list of image locations to be pushed to the image sink
   * as file location change notifications, they are pushed before
   * each buffer capture */
  GSList *image_location_list;

  gboolean video_profile_switch;
  gboolean image_profile_switch;

  /* properties */
  gint mode;
  gchar *location;
  gboolean post_previews;
  GstCaps *preview_caps;
  GstElement *preview_filter;
  GstEncodingProfile *video_profile;
  GstEncodingProfile *image_profile;
  gfloat zoom;
  gfloat max_zoom;
  GstCamFlags flags;

  gboolean elements_created;
};

struct _GstCameraBin2Class
{
  GstPipelineClass pipeline_class;

  /* Action signals */
  void (*start_capture) (GstCameraBin2 * camera);
  void (*stop_capture) (GstCameraBin2 * camera);
};

GType gst_camera_bin2_get_type (void);
gboolean gst_camera_bin2_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif
