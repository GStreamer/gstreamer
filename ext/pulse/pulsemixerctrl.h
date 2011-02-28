/*-*- Mode: C; c-basic-offset: 2 -*-*/

/*
 *  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2004-2008 Lennart Poettering
 *
 *  gst-pulse is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  gst-pulse is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with gst-pulse; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 */

#ifndef __GST_PULSEMIXERCTRL_H__
#define __GST_PULSEMIXERCTRL_H__

#include <gst/gst.h>
#include <gst/interfaces/mixer.h>

#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>

G_BEGIN_DECLS

#define GST_PULSEMIXER_CTRL(obj) ((GstPulseMixerCtrl*)(obj))
typedef struct _GstPulseMixerCtrl GstPulseMixerCtrl;

typedef enum
{
  GST_PULSEMIXER_UNKNOWN,
  GST_PULSEMIXER_SINK,
  GST_PULSEMIXER_SOURCE
} GstPulseMixerType;

struct _GstPulseMixerCtrl
{
  GObject *object;

  GList *tracklist;

  gchar *server, *device;

  pa_threaded_mainloop *mainloop;
  pa_context *context;

  gchar *name, *description;
  pa_channel_map channel_map;

  pa_cvolume volume;
  gboolean muted:1;

  gboolean update_volume:1;
  gboolean update_mute:1;

  gboolean operation_success:1;

  guint32 index;
  GstPulseMixerType type;

  GstMixerTrack *track;

  pa_time_event *time_event;

  int outstandig_queries;
  int ignore_queries;

};

GstPulseMixerCtrl *gst_pulsemixer_ctrl_new (GObject *object, const gchar * server,
    const gchar * device, GstPulseMixerType type);
void gst_pulsemixer_ctrl_free (GstPulseMixerCtrl * mixer);

const GList *gst_pulsemixer_ctrl_list_tracks (GstPulseMixerCtrl * mixer);

void gst_pulsemixer_ctrl_set_volume (GstPulseMixerCtrl * mixer,
    GstMixerTrack * track, gint * volumes);
void gst_pulsemixer_ctrl_get_volume (GstPulseMixerCtrl * mixer,
    GstMixerTrack * track, gint * volumes);
void gst_pulsemixer_ctrl_set_mute (GstPulseMixerCtrl * mixer,
    GstMixerTrack * track, gboolean mute);
void gst_pulsemixer_ctrl_set_record (GstPulseMixerCtrl * mixer,
    GstMixerTrack * track, gboolean record);
GstMixerFlags gst_pulsemixer_ctrl_get_mixer_flags (GstPulseMixerCtrl * mixer);

#define GST_IMPLEMENT_PULSEMIXER_CTRL_METHODS(Type, interface_as_function)     \
static const GList*                                                             \
interface_as_function ## _list_tracks (GstMixer * mixer)                        \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_val_if_fail (this != NULL, NULL);                                    \
  g_return_val_if_fail (this->mixer != NULL, NULL);                             \
                                                                                \
  return gst_pulsemixer_ctrl_list_tracks (this->mixer);                         \
}                                                                               \
static void                                                                     \
interface_as_function ## _set_volume (GstMixer * mixer, GstMixerTrack * track,  \
    gint * volumes)                                                             \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_if_fail (this != NULL);                                              \
  g_return_if_fail (this->mixer != NULL);                                       \
                                                                                \
  gst_pulsemixer_ctrl_set_volume (this->mixer, track, volumes);                 \
}                                                                               \
static void                                                                     \
interface_as_function ## _get_volume (GstMixer * mixer, GstMixerTrack * track,  \
    gint * volumes)                                                             \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_if_fail (this != NULL);                                              \
  g_return_if_fail (this->mixer != NULL);                                       \
                                                                                \
  gst_pulsemixer_ctrl_get_volume (this->mixer, track, volumes);                 \
}                                                                               \
static void                                                                     \
interface_as_function ## _set_record (GstMixer * mixer, GstMixerTrack * track,  \
    gboolean record)                                                            \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_if_fail (this != NULL);                                              \
  g_return_if_fail (this->mixer != NULL);                                       \
                                                                                \
  gst_pulsemixer_ctrl_set_record (this->mixer, track, record);                  \
}                                                                               \
static void                                                                     \
interface_as_function ## _set_mute (GstMixer * mixer, GstMixerTrack * track,    \
    gboolean mute)                                                              \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_if_fail (this != NULL);                                              \
  g_return_if_fail (this->mixer != NULL);                                       \
                                                                                \
  gst_pulsemixer_ctrl_set_mute (this->mixer, track, mute);                      \
}                                                                               \
static GstMixerFlags                                                            \
interface_as_function ## _get_mixer_flags (GstMixer * mixer)                    \
{                                                                               \
  Type *this = (Type*) mixer;                                                   \
                                                                                \
  g_return_val_if_fail (this != NULL, GST_MIXER_FLAG_NONE);                     \
  g_return_val_if_fail (this->mixer != NULL, GST_MIXER_FLAG_NONE);              \
                                                                                \
  return gst_pulsemixer_ctrl_get_mixer_flags (this->mixer);                          \
} \
static void                                                                     \
interface_as_function ## _mixer_interface_init (GstMixerClass * klass)          \
{                                                                               \
  GST_MIXER_TYPE (klass) = GST_MIXER_HARDWARE;                                  \
                                                                                \
  klass->list_tracks = interface_as_function ## _list_tracks;                   \
  klass->set_volume  = interface_as_function ## _set_volume;                    \
  klass->get_volume  = interface_as_function ## _get_volume;                    \
  klass->set_mute    = interface_as_function ## _set_mute;                      \
  klass->set_record  = interface_as_function ## _set_record;                    \
  klass->get_mixer_flags = interface_as_function ## _get_mixer_flags; \
}

G_END_DECLS

#endif
