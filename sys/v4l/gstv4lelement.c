/* G-Streamer generic V4L element - generic V4L calls handling
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include "v4l_calls.h"

#if 0
static GstElementDetails gst_v4lelement_details = {
  "Generic video4linux Element",
  "None/Video",
  "Generic plugin for handling common video4linux calls",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2001",
};
#endif

/* V4lElement signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_CHANNEL,
  ARG_CHANNEL_NAME,
  ARG_NORM,
  ARG_NORM_NAME,
  ARG_HAS_TUNER,
  ARG_FREQUENCY,
  ARG_HAS_AUDIO,
  ARG_MUTE,
  ARG_MODE,
  ARG_VOLUME,
  ARG_HUE,
  ARG_BRIGHTNESS,
  ARG_CONTRAST,
  ARG_SATURATION,
  ARG_DEVICE,
  ARG_DEVICE_NAME,
  ARG_DEVICE_IS_CAPTURE,
  ARG_DEVICE_IS_OVERLAY,
  ARG_DEVICE_IS_MJPEG_CAPTURE,
  ARG_DEVICE_IS_MJPEG_PLAYBACK,
  ARG_DEVICE_IS_MPEG_CAPTURE,
  ARG_DEVICE_IS_MPEG_PLAYBACK
};


static void                  gst_v4lelement_class_init   (GstV4lElementClass *klass);
static void                  gst_v4lelement_init         (GstV4lElement      *v4lelement);
static void                  gst_v4lelement_set_property (GObject            *object,
                                                          guint              prop_id,
                                                          const GValue       *value,
                                                          GParamSpec         *pspec);
static void                  gst_v4lelement_get_property (GObject            *object,
                                                          guint              prop_id,
                                                          GValue             *value,
                                                          GParamSpec         *pspec);
static GstElementStateReturn gst_v4lelement_change_state (GstElement         *element);
static gboolean              plugin_init                 (GModule            *module,
                                                          GstPlugin          *plugin);


static GstElementClass *parent_class = NULL;
//static guint gst_v4lelement_signals[LAST_SIGNAL] = { 0 };


GType
gst_v4lelement_get_type (void)
{
  static GType v4lelement_type = 0;

  if (!v4lelement_type) {
    static const GTypeInfo v4lelement_info = {
      sizeof(GstV4lElementClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_v4lelement_class_init,
      NULL,
      NULL,
      sizeof(GstV4lElement),
      0,
      (GInstanceInitFunc)gst_v4lelement_init,
      NULL
    };
    v4lelement_type = g_type_register_static(GST_TYPE_ELEMENT, "GstV4lElement", &v4lelement_info, 0);
  }
  return v4lelement_type;
}



static void
gst_v4lelement_class_init (GstV4lElementClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHANNEL,
    g_param_spec_int("channel","channel","channel",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHANNEL_NAME,
    g_param_spec_string("channel_name","channel_name","channel_name",
    NULL, G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NORM,
    g_param_spec_int("norm","norm","norm",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NORM_NAME,
    g_param_spec_string("norm_name","norm_name","norm_name",
    NULL, G_PARAM_READABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HAS_TUNER,
    g_param_spec_boolean("has_tuner","has_tuner","has_tuner",
    0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FREQUENCY,
    g_param_spec_ulong("frequency","frequency","frequency",
    0,G_MAXULONG,0,G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HAS_AUDIO,
    g_param_spec_boolean("has_audio","has_audio","has_audio",
    0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUTE,
    g_param_spec_boolean("mute","mute","mute",
    0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_VOLUME,
    g_param_spec_int("volume","volume","volume",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MODE,
    g_param_spec_int("mode","mode","mode",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HUE,
    g_param_spec_int("hue","hue","hue",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BRIGHTNESS,
    g_param_spec_int("brightness","brightness","brightness",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CONTRAST,
    g_param_spec_int("contrast","contrast","contrast",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SATURATION,
    g_param_spec_int("saturation","saturation","saturation",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE,
    g_param_spec_string("device","device","device",
    NULL, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_NAME,
    g_param_spec_string("device_name","device_name","device_name",
    NULL, G_PARAM_READABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_IS_CAPTURE,
    g_param_spec_boolean("can_capture","can_capture","can_capture",
    0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_IS_OVERLAY,
    g_param_spec_boolean("has_overlay","has_overlay","has_overlay",
    0,G_PARAM_READABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_IS_MJPEG_CAPTURE,
    g_param_spec_boolean("can_capture_mjpeg","can_capture_mjpeg","can_capture_mjpeg",
    0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_IS_MJPEG_PLAYBACK,
    g_param_spec_boolean("can_playback_mjpeg","can_playback_mjpeg","can_playback_mjpeg",
    0,G_PARAM_READABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_IS_MPEG_CAPTURE,
    g_param_spec_boolean("can_capture_mpeg","can_capture_mpeg","can_capture_mpeg",
    0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_IS_MPEG_PLAYBACK,
    g_param_spec_boolean("can_playback_mpeg","can_playback_mpeg","can_playback_mpeg",
    0,G_PARAM_READABLE));

  gobject_class->set_property = gst_v4lelement_set_property;
  gobject_class->get_property = gst_v4lelement_get_property;

  gstelement_class->change_state = gst_v4lelement_change_state;
}


static void
gst_v4lelement_init (GstV4lElement *v4lelement)
{
  /* some default values */
  v4lelement->video_fd = -1;
  v4lelement->buffer = NULL;
  v4lelement->videodev = NULL;

  v4lelement->norm = -1;
  v4lelement->channel = -1; /* the first channel */

  v4lelement->frequency = 0;

  v4lelement->mute = FALSE;
  v4lelement->volume = -1;
  v4lelement->mode = -1;

  v4lelement->brightness = -1;
  v4lelement->hue = -1;
  v4lelement->contrast = -1;
  v4lelement->saturation = -1;
}


static void
gst_v4lelement_set_property (GObject      *object,
                             guint        prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  GstV4lElement *v4lelement;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_V4LELEMENT(object));
  v4lelement = GST_V4LELEMENT(object);

  switch (prop_id)
  {
    case ARG_CHANNEL:
      v4lelement->channel = g_value_get_int(value);
      if (GST_V4L_IS_OPEN(v4lelement) && !GST_V4L_IS_ACTIVE(v4lelement))
      {
        if (v4lelement->norm >= VIDEO_MODE_PAL &&
            v4lelement->norm < VIDEO_MODE_AUTO &&
            v4lelement->channel >= 0)
          if (!gst_v4l_set_chan_norm(v4lelement, v4lelement->channel, v4lelement->norm))
            return;
      }
      break;
    case ARG_NORM:
      v4lelement->norm = g_value_get_int(value);
      if (GST_V4L_IS_OPEN(v4lelement) && !GST_V4L_IS_ACTIVE(v4lelement))
      {
        if (v4lelement->norm >= VIDEO_MODE_PAL &&
            v4lelement->norm < VIDEO_MODE_AUTO &&
            v4lelement->channel >= 0)
          if (!gst_v4l_set_chan_norm(v4lelement, v4lelement->channel, v4lelement->norm))
            return;
      }
      break;
    case ARG_FREQUENCY:
      v4lelement->frequency = g_value_get_ulong(value);
      if (GST_V4L_IS_OPEN(v4lelement) && !GST_V4L_IS_ACTIVE(v4lelement))
      {
        if (gst_v4l_has_tuner(v4lelement))
          if (!gst_v4l_set_frequency(v4lelement, v4lelement->frequency))
            return;
      }
      break;
    case ARG_MUTE:
      v4lelement->mute = g_value_get_boolean(value);
      if (GST_V4L_IS_OPEN(v4lelement))
      {
        if (gst_v4l_has_audio(v4lelement))
          if (!gst_v4l_set_audio(v4lelement, V4L_AUDIO_MUTE, v4lelement->mute))
            return;
      }
      break;
    case ARG_MODE:
      v4lelement->mode = g_value_get_int(value);
      if (GST_V4L_IS_OPEN(v4lelement))
      {
        if (!gst_v4l_set_audio(v4lelement, V4L_AUDIO_MODE, v4lelement->mute))
          return;
      }
      break;
    case ARG_VOLUME:
      v4lelement->volume = g_value_get_int(value);
      if (GST_V4L_IS_OPEN(v4lelement))
      {
        if (!gst_v4l_set_audio(v4lelement, V4L_AUDIO_VOLUME, v4lelement->volume))
          return;
      }
      break;
    case ARG_HUE:
      v4lelement->hue = g_value_get_int(value);
      if (GST_V4L_IS_OPEN(v4lelement))
      {
        if (!gst_v4l_set_picture(v4lelement, V4L_PICTURE_HUE, v4lelement->hue))
          return;
      }
      break;
    case ARG_BRIGHTNESS:
      v4lelement->brightness = g_value_get_int(value);
      if (GST_V4L_IS_OPEN(v4lelement))
      {
        if (!gst_v4l_set_picture(v4lelement, V4L_PICTURE_BRIGHTNESS, v4lelement->brightness))
          return;
      }
      break;
    case ARG_CONTRAST:
      v4lelement->contrast = g_value_get_int(value);
      if (GST_V4L_IS_OPEN(v4lelement))
      {
        if (!gst_v4l_set_picture(v4lelement, V4L_PICTURE_CONTRAST, v4lelement->contrast))
          return;
      }
      break;
    case ARG_SATURATION:
      v4lelement->saturation = g_value_get_int(value);
      if (GST_V4L_IS_OPEN(v4lelement))
      {
        if (!gst_v4l_set_picture(v4lelement, V4L_PICTURE_SATURATION, v4lelement->saturation))
          return;
      }
      break;
    case ARG_DEVICE:
      if (GST_V4L_IS_OPEN(v4lelement))
        break; /* only set when *not* open */
      if (v4lelement->videodev)
        g_free(v4lelement->videodev);
      v4lelement->videodev = g_strdup(g_value_get_string(value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4lelement_get_property (GObject    *object,
                             guint      prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  GstV4lElement *v4lelement;
  gint temp_i = 0;
  gulong temp_ul = 0;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_V4LELEMENT(object));
  v4lelement = GST_V4LELEMENT(object);

  switch (prop_id)
  {
    case ARG_CHANNEL:
      if (GST_V4L_IS_OPEN(v4lelement))
        gst_v4l_get_chan_norm(v4lelement, &temp_i, NULL);
      g_value_set_int(value, temp_i);
      break;
    case ARG_CHANNEL_NAME:
      if (GST_V4L_IS_OPEN(v4lelement))
        g_value_set_string(value, g_strdup(v4lelement->vchan.name));
      else
        g_value_set_string(value, g_strdup("Unknown"));
      break;
    case ARG_NORM:
      if (GST_V4L_IS_OPEN(v4lelement))
        gst_v4l_get_chan_norm(v4lelement, NULL, &temp_i);
      g_value_set_int(value, temp_i);
      break;
    case ARG_NORM_NAME:
      if (GST_V4L_IS_OPEN(v4lelement))
      {
        gst_v4l_get_chan_norm(v4lelement, NULL, &temp_i);
        g_value_set_string(value, g_strdup(norm_name[temp_i]));
      }
      else
        g_value_set_string(value, g_strdup("Unknwon"));
      break;
    case ARG_HAS_TUNER:
      g_value_set_boolean(value, FALSE);
      if (GST_V4L_IS_OPEN(v4lelement))
        if (gst_v4l_has_tuner(v4lelement))
          g_value_set_boolean(value, TRUE);
      break;
    case ARG_FREQUENCY:
      if (GST_V4L_IS_OPEN(v4lelement))
        if (gst_v4l_has_tuner(v4lelement))
          gst_v4l_get_frequency(v4lelement, &temp_ul);
      g_value_set_ulong(value, temp_ul);
      break;
    case ARG_HAS_AUDIO:
      g_value_set_boolean(value, FALSE);
      if (GST_V4L_IS_OPEN(v4lelement))
        if (gst_v4l_has_audio(v4lelement))
          g_value_set_boolean(value, TRUE);
      break;
    case ARG_MUTE:
      if (GST_V4L_IS_OPEN(v4lelement))
        if (gst_v4l_has_audio(v4lelement))
          gst_v4l_get_audio(v4lelement, V4L_AUDIO_MUTE, &temp_i);
      g_value_set_boolean(value, temp_i?TRUE:FALSE);
      break;
    case ARG_MODE:
      if (GST_V4L_IS_OPEN(v4lelement))
        if (gst_v4l_has_tuner(v4lelement))
          gst_v4l_get_audio(v4lelement, V4L_AUDIO_MODE, &temp_i);
      g_value_set_int(value, temp_i);
      break;
    case ARG_VOLUME:
      if (GST_V4L_IS_OPEN(v4lelement))
        if (gst_v4l_has_tuner(v4lelement))
          gst_v4l_get_audio(v4lelement, V4L_AUDIO_VOLUME, &temp_i);
      g_value_set_int(value, temp_i);
      break;
    case ARG_HUE:
      if (GST_V4L_IS_OPEN(v4lelement))
        gst_v4l_get_picture(v4lelement, V4L_PICTURE_HUE, &temp_i);
      g_value_set_int(value, temp_i);
      break;
    case ARG_BRIGHTNESS:
      if (GST_V4L_IS_OPEN(v4lelement))
        gst_v4l_get_picture(v4lelement, V4L_PICTURE_BRIGHTNESS, &temp_i);
      g_value_set_int(value, temp_i);
      break;
    case ARG_CONTRAST:
      if (GST_V4L_IS_OPEN(v4lelement))
        gst_v4l_get_picture(v4lelement, V4L_PICTURE_CONTRAST, &temp_i);
      g_value_set_int(value, temp_i);
      break;
    case ARG_SATURATION:
      if (GST_V4L_IS_OPEN(v4lelement))
        gst_v4l_get_picture(v4lelement, V4L_PICTURE_SATURATION, &temp_i);
      g_value_set_int(value, temp_i);
      break;
    case ARG_DEVICE:
      g_value_set_string(value, g_strdup(v4lelement->videodev?v4lelement->videodev:"/dev/video"));
      break;
    case ARG_DEVICE_NAME:
      if (GST_V4L_IS_OPEN(v4lelement))
        g_value_set_string(value, g_strdup(v4lelement->vcap.name));
      else
        g_value_set_string(value, g_strdup("None"));
      break;
    case ARG_DEVICE_IS_CAPTURE:
      g_value_set_boolean(value, FALSE);
      if (GST_V4L_IS_OPEN(v4lelement))
        g_value_set_boolean(value, v4lelement->vcap.type & VID_TYPE_CAPTURE);
      break;
    case ARG_DEVICE_IS_OVERLAY:
      g_value_set_boolean(value, FALSE);
      if (GST_V4L_IS_OPEN(v4lelement))
        g_value_set_boolean(value, v4lelement->vcap.type & VID_TYPE_OVERLAY);
      break;
    case ARG_DEVICE_IS_MJPEG_CAPTURE:
      g_value_set_boolean(value, FALSE);
      if (GST_V4L_IS_OPEN(v4lelement))
        g_value_set_boolean(value, v4lelement->vcap.type & VID_TYPE_MJPEG_ENCODER);
      break;
    case ARG_DEVICE_IS_MJPEG_PLAYBACK:
      g_value_set_boolean(value, FALSE);
      if (GST_V4L_IS_OPEN(v4lelement))
        g_value_set_boolean(value, v4lelement->vcap.type & VID_TYPE_MJPEG_DECODER);
      break;
    case ARG_DEVICE_IS_MPEG_CAPTURE:
      g_value_set_boolean(value, FALSE);
      if (GST_V4L_IS_OPEN(v4lelement))
        g_value_set_boolean(value, v4lelement->vcap.type & VID_TYPE_MPEG_ENCODER);
      break;
    case ARG_DEVICE_IS_MPEG_PLAYBACK:
      g_value_set_boolean(value, FALSE);
      if (GST_V4L_IS_OPEN(v4lelement))
        g_value_set_boolean(value, v4lelement->vcap.type & VID_TYPE_MPEG_DECODER);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_v4lelement_change_state (GstElement *element)
{
  GstV4lElement *v4lelement;
  
  g_return_val_if_fail(GST_IS_V4LELEMENT(element), GST_STATE_FAILURE);
  
  v4lelement = GST_V4LELEMENT(element);

  /* if going down into NULL state, close the device if it's open
   * if going to READY, open the device (and set some options)
   */
  switch (GST_STATE_TRANSITION(element))
  {
    case GST_STATE_NULL_TO_READY:
    {
      int n, temp;

      if (!gst_v4l_open(v4lelement))
        return GST_STATE_FAILURE;

      /* now, sync options */
      if (v4lelement->norm >= VIDEO_MODE_PAL &&
          v4lelement->norm < VIDEO_MODE_AUTO &&
          v4lelement->channel >= 0)
      {
        if (!gst_v4l_set_chan_norm(v4lelement, v4lelement->channel, v4lelement->norm))
          return GST_STATE_FAILURE;
      }
      if (v4lelement->frequency > 0 && gst_v4l_has_tuner(v4lelement))
      {
        if (!gst_v4l_set_frequency(v4lelement, v4lelement->frequency))
          return GST_STATE_FAILURE;
      }
      for (n=V4L_AUDIO_VOLUME;n<=V4L_AUDIO_MODE;n++)
      {
        switch (n)
        {
          case V4L_AUDIO_MUTE:   temp = v4lelement->mute;   break;
          case V4L_AUDIO_VOLUME: temp = v4lelement->volume; break;
          case V4L_AUDIO_MODE:   temp = v4lelement->mode;   break;
        }
        if (temp >= 0 && gst_v4l_has_audio(v4lelement))
        {
          if (!gst_v4l_set_audio(v4lelement, n, temp))
            return GST_STATE_FAILURE;
        }
      }
      for (n=V4L_PICTURE_HUE;n<=V4L_PICTURE_SATURATION;n++)
      {
        switch (n)
        {
          case V4L_PICTURE_HUE:        temp = v4lelement->hue;        break;
          case V4L_PICTURE_BRIGHTNESS: temp = v4lelement->brightness; break;
          case V4L_PICTURE_SATURATION: temp = v4lelement->saturation; break;
          case V4L_PICTURE_CONTRAST:   temp = v4lelement->contrast;   break;
        }
        if (temp >= 0)
        {
          if (!gst_v4l_set_picture(v4lelement, n, temp))
            return GST_STATE_FAILURE;
        }
      }
    }
      break;
    case GST_STATE_READY_TO_NULL:
      if (!gst_v4l_close(v4lelement))
        return GST_STATE_FAILURE;
      break;
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);

  return GST_STATE_SUCCESS;
}


static gboolean
plugin_init (GModule   *module,
             GstPlugin *plugin)
{
#if 0
  GstElementFactory *factory;

  /* create an elementfactory for the v4lelement */
  factory = gst_elementfactory_new("v4lelement",GST_TYPE_V4LELEMENT,
                                   &gst_v4lelement_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
#endif
  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "v4lelement",
  plugin_init
};
