#include <gst/gst.h>
#include <gst/interfaces/mixertrack.h>

guint
gst__interfacessharp_gst__interfaces_mixertrack_get_label_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstMixerTrack, label);
}

guint
gst__interfacessharp_gst__interfaces_mixertrack_get_flags_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstMixerTrack, flags);
}

guint
gst__interfacessharp_gst__interfaces_mixertrack_get_num_channels_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstMixerTrack, num_channels);
}

guint
gst__interfacessharp_gst__interfaces_mixertrack_get_min_volume_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstMixerTrack, min_volume);
}

guint
gst__interfacessharp_gst__interfaces_mixertrack_get_max_volume_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstMixerTrack, max_volume);
}
