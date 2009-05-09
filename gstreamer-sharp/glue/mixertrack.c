#include <gst/gst.h>
#include <gst/interfaces/mixertrack.h>

uint
gst__interfacessharp_gst__interfaces_mixertrack_get_label_offset (void) {
  return (uint) G_STRUCT_OFFSET (GstMixerTrack, label);
}

uint
gst__interfacessharp_gst__interfaces_mixertrack_get_flags_offset (void) {
  return (uint) G_STRUCT_OFFSET (GstMixerTrack, flags);
}

uint
gst__interfacessharp_gst__interfaces_mixertrack_get_num_channels_offset (void) {
  return (uint) G_STRUCT_OFFSET (GstMixerTrack, num_channels);
}

uint
gst__interfacessharp_gst__interfaces_mixertrack_get_min_volume_offset (void) {
  return (uint) G_STRUCT_OFFSET (GstMixerTrack, min_volume);
}

uint
gst__interfacessharp_gst__interfaces_mixertrack_get_max_volume_offset (void) {
  return (uint) G_STRUCT_OFFSET (GstMixerTrack, max_volume);
}

