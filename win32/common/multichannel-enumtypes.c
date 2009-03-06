
/* Generated data (by glib-mkenums) */

#include "multichannel-enumtypes.h"

#include "multichannel.h"

/* enumerations from "multichannel.h" */
GType
gst_audio_channel_position_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_AUDIO_CHANNEL_POSITION_INVALID, "GST_AUDIO_CHANNEL_POSITION_INVALID",
          "invalid"},
      {GST_AUDIO_CHANNEL_POSITION_FRONT_MONO,
          "GST_AUDIO_CHANNEL_POSITION_FRONT_MONO", "front-mono"},
      {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT", "front-left"},
      {GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          "GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT", "front-right"},
      {GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
          "GST_AUDIO_CHANNEL_POSITION_REAR_CENTER", "rear-center"},
      {GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
          "GST_AUDIO_CHANNEL_POSITION_REAR_LEFT", "rear-left"},
      {GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
          "GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT", "rear-right"},
      {GST_AUDIO_CHANNEL_POSITION_LFE, "GST_AUDIO_CHANNEL_POSITION_LFE", "lfe"},
      {GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          "GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER", "front-center"},
      {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
            "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER",
          "front-left-of-center"},
      {GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
            "GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER",
          "front-right-of-center"},
      {GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
          "GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT", "side-left"},
      {GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
          "GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT", "side-right"},
      {GST_AUDIO_CHANNEL_POSITION_NONE, "GST_AUDIO_CHANNEL_POSITION_NONE",
          "none"},
      {GST_AUDIO_CHANNEL_POSITION_NUM, "GST_AUDIO_CHANNEL_POSITION_NUM", "num"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstAudioChannelPosition", values);
  }
  return etype;
}

/* Generated data ends here */
