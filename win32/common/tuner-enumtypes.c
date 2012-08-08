


#include "tuner-enumtypes.h"

#include "tuner.h"
#include "tunernorm.h"
#include "tunerchannel.h"

/* enumerations from "tunerchannel.h" */
GType
gst_tuner_channel_flags_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GFlagsValue values[] = {
      {GST_TUNER_CHANNEL_INPUT, "GST_TUNER_CHANNEL_INPUT", "input"},
      {GST_TUNER_CHANNEL_OUTPUT, "GST_TUNER_CHANNEL_OUTPUT", "output"},
      {GST_TUNER_CHANNEL_FREQUENCY, "GST_TUNER_CHANNEL_FREQUENCY", "frequency"},
      {GST_TUNER_CHANNEL_AUDIO, "GST_TUNER_CHANNEL_AUDIO", "audio"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_flags_register_static ("GstTunerChannelFlags", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}
