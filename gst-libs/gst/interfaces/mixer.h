/* GStreamer Mixer
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * mixer.h: mixer interface design
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

#ifndef __GST_MIXER_H__
#define __GST_MIXER_H__

#include <gst/gst.h>
#include <gst/mixer/mixertrack.h>
#include <gst/mixer/mixer-enumtypes.h>

G_BEGIN_DECLS
#define GST_TYPE_MIXER \
  (gst_mixer_get_type ())
#define GST_MIXER(obj) \
  (GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MIXER, GstMixer))
#define GST_MIXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MIXER, GstMixerClass))
#define GST_IS_MIXER(obj) \
  (GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MIXER))
#define GST_IS_MIXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MIXER))
#define GST_MIXER_GET_CLASS(inst) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GST_TYPE_MIXER, GstMixerClass))
#define GST_MIXER_TYPE(klass) (klass->mixer_type)
typedef struct _GstMixer GstMixer;

typedef enum
{
  GST_MIXER_HARDWARE,
  GST_MIXER_SOFTWARE
} GstMixerType;

typedef struct _GstMixerClass
{
  GTypeInterface klass;

  GstMixerType mixer_type;

  /* virtual functions */
  const GList *(*list_tracks) (GstMixer * mixer);

  void (*set_volume) (GstMixer * mixer, GstMixerTrack * track, gint * volumes);
  void (*get_volume) (GstMixer * mixer, GstMixerTrack * track, gint * volumes);

  void (*set_mute) (GstMixer * mixer, GstMixerTrack * track, gboolean mute);
  void (*set_record) (GstMixer * mixer, GstMixerTrack * track, gboolean record);

  /* signals */
  void (*mute_toggled) (GstMixer * mixer,
      GstMixerTrack * channel, gboolean mute);
  void (*record_toggled) (GstMixer * mixer,
      GstMixerTrack * channel, gboolean record);
  void (*volume_changed) (GstMixer * mixer,
      GstMixerTrack * channel, gint * volumes);

  gpointer _gst_reserved[GST_PADDING];
} GstMixerClass;

GType gst_mixer_get_type (void);

/* virtual class function wrappers */
const GList *gst_mixer_list_tracks (GstMixer * mixer);
void gst_mixer_set_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes);
void gst_mixer_get_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes);
void gst_mixer_set_mute (GstMixer * mixer,
    GstMixerTrack * track, gboolean mute);
void gst_mixer_set_record (GstMixer * mixer,
    GstMixerTrack * track, gboolean record);

/* trigger signals */
void gst_mixer_mute_toggled (GstMixer * mixer,
    GstMixerTrack * track, gboolean mute);
void gst_mixer_record_toggled (GstMixer * mixer,
    GstMixerTrack * track, gboolean record);
void gst_mixer_volume_changed (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes);

G_END_DECLS
#endif /* __GST_MIXER_H__ */
