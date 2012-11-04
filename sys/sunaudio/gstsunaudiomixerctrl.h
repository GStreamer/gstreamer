/*
 * GStreamer - SunAudio mixer interface element.
 * Copyright (C) 2005,2006,2009 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 * Copyright (C) 2009 Sun Microsystems, Inc.,
 *               Garrett D'Amore <garrett.damore@sun.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __GST_SUNAUDIO_MIXER_CTRL_H
#define __GST_SUNAUDIO_MIXER_CTRL_H

#include <sys/audioio.h>

#include <gst/gst.h>
#include <gst/interfaces/mixer.h>

G_BEGIN_DECLS

#define GST_SUNAUDIO_MIXER_CTRL(obj)              ((GstSunAudioMixerCtrl*)(obj))

typedef struct _GstSunAudioMixerCtrl GstSunAudioMixerCtrl;

struct _GstSunAudioMixerCtrl {
  GList *               tracklist;      /* list of available tracks */

  gint                  fd;
  gint                  mixer_fd;

  gchar *               device;
};

GstSunAudioMixerCtrl* gst_sunaudiomixer_ctrl_new          (const gchar *device);
void                  gst_sunaudiomixer_ctrl_free         (GstSunAudioMixerCtrl *mixer);

const GList*          gst_sunaudiomixer_ctrl_list_tracks  (GstSunAudioMixerCtrl * mixer);
void                  gst_sunaudiomixer_ctrl_set_volume   (GstSunAudioMixerCtrl * mixer,
                                                          GstMixerTrack * track,
                                                          gint * volumes);
void                  gst_sunaudiomixer_ctrl_get_volume   (GstSunAudioMixerCtrl * mixer,
                                                          GstMixerTrack * track,
                                                          gint * volumes);
void                  gst_sunaudiomixer_ctrl_set_record   (GstSunAudioMixerCtrl * mixer,
                                                          GstMixerTrack * track,
                                                          gboolean record);
void                  gst_sunaudiomixer_ctrl_set_mute     (GstSunAudioMixerCtrl * mixer,
                                                             GstMixerTrack * track,
                                                          gboolean mute);
void                  gst_sunaudiomixer_ctrl_set_option   (GstSunAudioMixerCtrl * mixer,
							  GstMixerOptions * options,
							  gchar * value);
const gchar *         gst_sunaudiomixer_ctrl_get_option   (GstSunAudioMixerCtrl * mixer,
							  GstMixerOptions * options);
GstMixerFlags	      gst_sunaudiomixer_ctrl_get_mixer_flags	  (GstSunAudioMixerCtrl *mixer);

#define GST_IMPLEMENT_SUNAUDIO_MIXER_CTRL_METHODS(Type, interface_as_function)  \
static gboolean                                                                 \
interface_as_function ## _supported (Type *this, GType iface_type)              \
{                                                                               \
  g_assert (iface_type == GST_TYPE_MIXER);                                      \
                                                                                \
  return (this->mixer != NULL);                                                 \
}                                                                               \
                                                                                \
static const GList*                                                             \
interface_as_function ## _list_tracks (GstMixer * mixer)                        \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_val_if_fail (this != NULL, NULL);                                    \
  g_return_val_if_fail (this->mixer != NULL, NULL);                             \
                                                                                \
  return gst_sunaudiomixer_ctrl_list_tracks (this->mixer);                      \
}                                                                               \
                                                                                \
static void                                                                     \
interface_as_function ## _set_volume (GstMixer * mixer, GstMixerTrack * track,  \
    gint * volumes)                                                             \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_if_fail (this != NULL);                                              \
  g_return_if_fail (this->mixer != NULL);                                       \
                                                                                \
  gst_sunaudiomixer_ctrl_set_volume (this->mixer, track, volumes);              \
}                                                                               \
                                                                                \
static void                                                                     \
interface_as_function ## _get_volume (GstMixer * mixer, GstMixerTrack * track,  \
    gint * volumes)                                                             \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_if_fail (this != NULL);                                              \
  g_return_if_fail (this->mixer != NULL);                                       \
                                                                                \
  gst_sunaudiomixer_ctrl_get_volume (this->mixer, track, volumes);              \
}                                                                               \
                                                                                \
static void                                                                     \
interface_as_function ## _set_record (GstMixer * mixer, GstMixerTrack * track,  \
    gboolean record)                                                            \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_if_fail (this != NULL);                                              \
  g_return_if_fail (this->mixer != NULL);                                       \
                                                                                \
  gst_sunaudiomixer_ctrl_set_record (this->mixer, track, record);               \
}                                                                               \
                                                                                \
static void                                                                     \
interface_as_function ## _set_mute (GstMixer * mixer, GstMixerTrack * track,    \
    gboolean mute)                                                              \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_if_fail (this != NULL);                                              \
  g_return_if_fail (this->mixer != NULL);                                       \
                                                                                \
  gst_sunaudiomixer_ctrl_set_mute (this->mixer, track, mute);                   \
}                                                                               \
										\
static const gchar *								\
interface_as_function ## _get_option (GstMixer * mixer, GstMixerOptions * opts)	\
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_val_if_fail (this != NULL, NULL);					\
  g_return_val_if_fail (this->mixer != NULL, NULL);				\
                                                                                \
  return gst_sunaudiomixer_ctrl_get_option (this->mixer, opts);			\
}                                                                               \
\
static void									\
interface_as_function ## _set_option (GstMixer * mixer, GstMixerOptions * opts,	\
    gchar * value)								\
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_if_fail (this != NULL);                                              \
  g_return_if_fail (this->mixer != NULL);                                       \
                                                                                \
  gst_sunaudiomixer_ctrl_set_option (this->mixer, opts, value);			\
}                                                                               \
\
static GstMixerFlags                                                            \
interface_as_function ## _get_mixer_flags (GstMixer * mixer)                    \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_val_if_fail (this != NULL, GST_MIXER_FLAG_NONE);                     \
  g_return_val_if_fail (this->mixer != NULL, GST_MIXER_FLAG_NONE);              \
                                                                                \
  return gst_sunaudiomixer_ctrl_get_mixer_flags (this->mixer);			\
}                                                                               \
										\
static void                                                                     \
interface_as_function ## _interface_init (GstMixerInterface * iface)                \
{                                                                               \
  GST_MIXER_TYPE (iface) = GST_MIXER_HARDWARE;                                  \
                                                                                \
  /* set up the interface hooks */                                              \
  iface->list_tracks = interface_as_function ## _list_tracks;                   \
  iface->set_volume  = interface_as_function ## _set_volume;                    \
  iface->get_volume  = interface_as_function ## _get_volume;                    \
  iface->set_mute    = interface_as_function ## _set_mute;                      \
  iface->set_record  = interface_as_function ## _set_record;                    \
  iface->get_option  = interface_as_function ## _get_option;			\
  iface->set_option  = interface_as_function ## _set_option;			\
  iface->get_mixer_flags   = interface_as_function ## _get_mixer_flags;		\
}

G_END_DECLS

#endif
