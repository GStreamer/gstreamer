#ifndef __GST_SUNAUDIO_MIXER_H
#define __GST_SUNAUDIO_MIXER_H

#include <gst/gst.h>
#include <gst/mixer/mixer.h>
#include "gstsunelement.h"
                                                                                
G_BEGIN_DECLS
                                                                                
#define GST_TYPE_SUNAUDIOMIXER_TRACK \
  (gst_sunaudiomixer_track_get_type ())
#define GST_SUNAUDIOMIXER_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SUNAUDIOMIXER_TRACK, \
                               GstSunAudioMixerTrack))
#define GST_SUNAUDIOMIXER_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SUNAUDIOMIXER_TRACK, \
                            GstSunAudioMixerTrackClass))
#define GST_IS_SUNAUDIOMIXER_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SUNAUDIOMIXER_TRACK))
#define GST_IS_SUNAUDIOMIXER_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SUNAUDIOMIXER_TRACK))
                                                                                
typedef struct _GstSunAudioMixerTrack {
  GstMixerTrack parent;
                                                                                
  gint          lvol, rvol;
  gint          track_num;
} GstSunAudioMixerTrack;
                                                                                
typedef struct _GstSunAudioMixerTrackClass {
  GstMixerTrackClass parent;
} GstSunAudioMixerTrackClass;
                                                                                
GType   gst_sunaudiomixer_track_get_type     (void);
 
void    gst_sunaudiomixer_interface_init     (GstMixerClass *klass);
void    gst_sunaudio_interface_init          (GstImplementsInterfaceClass *klass);
void    gst_sunaudiomixer_build_list         (GstSunAudioElement *sunaudio);
void    gst_sunaudiomixer_free_list          (GstSunAudioElement *sunaudio);
 
G_END_DECLS

#endif
