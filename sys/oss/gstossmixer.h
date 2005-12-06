/* GStreamer OSS Mixer implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstossmixer.h: mixer interface implementation for OSS
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


#ifndef __GST_OSS_MIXER_H__
#define __GST_OSS_MIXER_H__


#include <gst/gst.h>
#include <gst/interfaces/mixer.h>

#include "gstosshelper.h"


G_BEGIN_DECLS


#define GST_OSS_MIXER(obj)              ((GstOssMixer*)(obj))


typedef enum {
  GST_OSS_MIXER_CAPTURE = 1<<0,
  GST_OSS_MIXER_PLAYBACK = 1<<1,
  GST_OSS_MIXER_ALL = GST_OSS_MIXER_CAPTURE | GST_OSS_MIXER_PLAYBACK
} GstOssMixerDirection;


typedef struct _GstOssMixer GstOssMixer;


struct _GstOssMixer {
  GList *               tracklist;      /* list of available tracks */

  gint                  mixer_fd;

  gchar *               device;
  gchar *               cardname;

  gint                  recmask;
  gint                  recdevs;
  gint                  stereomask;
  gint                  devmask;
  gint                  mixcaps;

  GstOssMixerDirection dir;
};


GstOssMixer*    gst_ossmixer_new                (const gchar *device,
                                                 GstOssMixerDirection dir);
void            gst_ossmixer_free               (GstOssMixer *mixer);

const GList*    gst_ossmixer_list_tracks        (GstOssMixer * mixer);
void            gst_ossmixer_set_volume         (GstOssMixer * mixer,
                                                 GstMixerTrack * track,
                                                 gint * volumes);
void            gst_ossmixer_get_volume         (GstOssMixer * mixer,
                                                 GstMixerTrack * track,
                                                 gint * volumes);
void            gst_ossmixer_set_record         (GstOssMixer * mixer,
                                                 GstMixerTrack * track,
                                                 gboolean record);
void            gst_ossmixer_set_mute           (GstOssMixer * mixer,
                                                 GstMixerTrack * track,
                                                 gboolean mute);


#define GST_IMPLEMENT_OSS_MIXER_METHODS(Type, interface_as_function)            \
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
  return gst_ossmixer_list_tracks (this->mixer);                                \
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
  gst_ossmixer_set_volume (this->mixer, track, volumes);                        \
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
  gst_ossmixer_get_volume (this->mixer, track, volumes);                        \
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
  gst_ossmixer_set_record (this->mixer, track, record);                         \
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
  gst_ossmixer_set_mute (this->mixer, track, mute);                             \
}                                                                               \
                                                                                \
static void                                                                     \
interface_as_function ## _interface_init (GstMixerClass * klass)                \
{                                                                               \
  GST_MIXER_TYPE (klass) = GST_MIXER_HARDWARE;                                  \
                                                                                \
  /* set up the interface hooks */                                              \
  klass->list_tracks = interface_as_function ## _list_tracks;                   \
  klass->set_volume = interface_as_function ## _set_volume;                     \
  klass->get_volume = interface_as_function ## _get_volume;                     \
  klass->set_mute = interface_as_function ## _set_mute;                         \
  klass->set_record = interface_as_function ## _set_record;                     \
}


G_END_DECLS


#endif /* __GST_OSS_MIXER_H__ */
