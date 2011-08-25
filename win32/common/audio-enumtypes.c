


#include "audio-enumtypes.h"

#include "multichannel.h"
#include "gstringbuffer.h"

/* enumerations from "multichannel.h" */
GType
gst_audio_channel_position_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
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
      {GST_AUDIO_CHANNEL_POSITION_TOP_CENTER,
          "GST_AUDIO_CHANNEL_POSITION_TOP_CENTER", "top-center"},
      {GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
          "GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT", "top-front-left"},
      {GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
          "GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT", "top-front-right"},
      {GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER,
          "GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER", "top-front-center"},
      {GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
          "GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT", "top-rear-left"},
      {GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT,
          "GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT", "top-rear-right"},
      {GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER,
          "GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER", "top-rear-center"},
      {GST_AUDIO_CHANNEL_POSITION_NONE, "GST_AUDIO_CHANNEL_POSITION_NONE",
          "none"},
      {GST_AUDIO_CHANNEL_POSITION_NUM, "GST_AUDIO_CHANNEL_POSITION_NUM", "num"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_enum_register_static ("GstAudioChannelPosition", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

/* enumerations from "gstringbuffer.h" */
GType
gst_ring_buffer_state_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {GST_RING_BUFFER_STATE_STOPPED, "GST_RING_BUFFER_STATE_STOPPED",
          "stopped"},
      {GST_RING_BUFFER_STATE_PAUSED, "GST_RING_BUFFER_STATE_PAUSED", "paused"},
      {GST_RING_BUFFER_STATE_STARTED, "GST_RING_BUFFER_STATE_STARTED",
          "started"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_enum_register_static ("GstRingBufferState", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

GType
gst_ring_buffer_seg_state_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {GST_SEGSTATE_INVALID, "GST_SEGSTATE_INVALID", "invalid"},
      {GST_SEGSTATE_EMPTY, "GST_SEGSTATE_EMPTY", "empty"},
      {GST_SEGSTATE_FILLED, "GST_SEGSTATE_FILLED", "filled"},
      {GST_SEGSTATE_PARTIAL, "GST_SEGSTATE_PARTIAL", "partial"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_enum_register_static ("GstRingBufferSegState", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

GType
gst_buffer_format_type_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {GST_BUFTYPE_RAW, "GST_BUFTYPE_RAW", "raw"},
      {GST_BUFTYPE_MU_LAW, "GST_BUFTYPE_MU_LAW", "mu-law"},
      {GST_BUFTYPE_A_LAW, "GST_BUFTYPE_A_LAW", "a-law"},
      {GST_BUFTYPE_IMA_ADPCM, "GST_BUFTYPE_IMA_ADPCM", "ima-adpcm"},
      {GST_BUFTYPE_MPEG, "GST_BUFTYPE_MPEG", "mpeg"},
      {GST_BUFTYPE_GSM, "GST_BUFTYPE_GSM", "gsm"},
      {GST_BUFTYPE_IEC958, "GST_BUFTYPE_IEC958", "iec958"},
      {GST_BUFTYPE_AC3, "GST_BUFTYPE_AC3", "ac3"},
      {GST_BUFTYPE_EAC3, "GST_BUFTYPE_EAC3", "eac3"},
      {GST_BUFTYPE_DTS, "GST_BUFTYPE_DTS", "dts"},
      {GST_BUFTYPE_MPEG2_AAC, "GST_BUFTYPE_MPEG2_AAC", "mpeg2-aac"},
      {GST_BUFTYPE_MPEG4_AAC, "GST_BUFTYPE_MPEG4_AAC", "mpeg4-aac"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_enum_register_static ("GstBufferFormatType", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}
