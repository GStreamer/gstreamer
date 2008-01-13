
/* Generated data (by glib-mkenums) */

#include "interfaces-enumtypes.h"

#include "colorbalance.h"
#include "colorbalancechannel.h"
#include "mixer.h"
#include "mixeroptions.h"
#include "mixertrack.h"
#include "navigation.h"
#include "propertyprobe.h"
#include "tuner.h"
#include "tunernorm.h"
#include "tunerchannel.h"
#include "videoorientation.h"
#include "xoverlay.h"

/* enumerations from "colorbalance.h" */
GType
gst_color_balance_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_COLOR_BALANCE_HARDWARE, "GST_COLOR_BALANCE_HARDWARE", "hardware"},
      {GST_COLOR_BALANCE_SOFTWARE, "GST_COLOR_BALANCE_SOFTWARE", "software"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstColorBalanceType", values);
  }
  return etype;
}

/* enumerations from "mixer.h" */
GType
gst_mixer_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_MIXER_HARDWARE, "GST_MIXER_HARDWARE", "hardware"},
      {GST_MIXER_SOFTWARE, "GST_MIXER_SOFTWARE", "software"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstMixerType", values);
  }
  return etype;
}

GType
gst_mixer_message_type_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_MIXER_MESSAGE_INVALID, "GST_MIXER_MESSAGE_INVALID", "invalid"},
      {GST_MIXER_MESSAGE_MUTE_TOGGLED, "GST_MIXER_MESSAGE_MUTE_TOGGLED",
            "mute-toggled"},
      {GST_MIXER_MESSAGE_RECORD_TOGGLED, "GST_MIXER_MESSAGE_RECORD_TOGGLED",
            "record-toggled"},
      {GST_MIXER_MESSAGE_VOLUME_CHANGED, "GST_MIXER_MESSAGE_VOLUME_CHANGED",
            "volume-changed"},
      {GST_MIXER_MESSAGE_OPTION_CHANGED, "GST_MIXER_MESSAGE_OPTION_CHANGED",
            "option-changed"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstMixerMessageType", values);
  }
  return etype;
}

GType
gst_mixer_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_MIXER_FLAG_NONE, "GST_MIXER_FLAG_NONE", "none"},
      {GST_MIXER_FLAG_AUTO_NOTIFICATIONS, "GST_MIXER_FLAG_AUTO_NOTIFICATIONS",
            "auto-notifications"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstMixerFlags", values);
  }
  return etype;
}

/* enumerations from "mixertrack.h" */
GType
gst_mixer_track_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_MIXER_TRACK_INPUT, "GST_MIXER_TRACK_INPUT", "input"},
      {GST_MIXER_TRACK_OUTPUT, "GST_MIXER_TRACK_OUTPUT", "output"},
      {GST_MIXER_TRACK_MUTE, "GST_MIXER_TRACK_MUTE", "mute"},
      {GST_MIXER_TRACK_RECORD, "GST_MIXER_TRACK_RECORD", "record"},
      {GST_MIXER_TRACK_MASTER, "GST_MIXER_TRACK_MASTER", "master"},
      {GST_MIXER_TRACK_SOFTWARE, "GST_MIXER_TRACK_SOFTWARE", "software"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstMixerTrackFlags", values);
  }
  return etype;
}

/* enumerations from "tunerchannel.h" */
GType
gst_tuner_channel_flags_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_TUNER_CHANNEL_INPUT, "GST_TUNER_CHANNEL_INPUT", "input"},
      {GST_TUNER_CHANNEL_OUTPUT, "GST_TUNER_CHANNEL_OUTPUT", "output"},
      {GST_TUNER_CHANNEL_FREQUENCY, "GST_TUNER_CHANNEL_FREQUENCY", "frequency"},
      {GST_TUNER_CHANNEL_AUDIO, "GST_TUNER_CHANNEL_AUDIO", "audio"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstTunerChannelFlags", values);
  }
  return etype;
}

/* Generated data ends here */
