/* GStreamer
 *
 * Copyright (C) 2014-2015 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_PLAYER_H__
#define __GST_PLAYER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/player/gstplayer-types.h>
#include <gst/player/gstplayer-signal-dispatcher.h>
#include <gst/player/gstplayer-video-renderer.h>
#include <gst/player/gstplayer-media-info.h>

G_BEGIN_DECLS

GType        gst_player_state_get_type                (void);
#define      GST_TYPE_PLAYER_STATE                    (gst_player_state_get_type ())

/**
 * GstPlayerState:
 * @GST_PLAYER_STATE_STOPPED: the player is stopped.
 * @GST_PLAYER_STATE_BUFFERING: the player is buffering.
 * @GST_PLAYER_STATE_PAUSED: the player is paused.
 * @GST_PLAYER_STATE_PLAYING: the player is currently playing a
 * stream.
 */
typedef enum
{
  GST_PLAYER_STATE_STOPPED,
  GST_PLAYER_STATE_BUFFERING,
  GST_PLAYER_STATE_PAUSED,
  GST_PLAYER_STATE_PLAYING
} GstPlayerState;

const gchar *gst_player_state_get_name                (GstPlayerState state);

GQuark       gst_player_error_quark                   (void);
GType        gst_player_error_get_type                (void);
#define      GST_PLAYER_ERROR                         (gst_player_error_quark ())
#define      GST_TYPE_PLAYER_ERROR                    (gst_player_error_get_type ())

/**
 * GstPlayerError:
 * @GST_PLAYER_ERROR_FAILED: generic error.
 */
typedef enum {
  GST_PLAYER_ERROR_FAILED = 0
} GstPlayerError;

const gchar *gst_player_error_get_name                (GstPlayerError error);

GType gst_player_color_balance_type_get_type          (void);
#define GST_TYPE_PLAYER_COLOR_BALANCE_TYPE            (gst_player_color_balance_type_get_type ())

/**
 * GstPlayerColorBalanceType:
 * @GST_PLAYER_COLOR_BALANCE_BRIGHTNESS: brightness or black level.
 * @GST_PLAYER_COLOR_BALANCE_CONTRAST: contrast or luma gain.
 * @GST_PLAYER_COLOR_BALANCE_SATURATION: color saturation or chroma
 * gain.
 * @GST_PLAYER_COLOR_BALANCE_HUE: hue or color balance.
 */
typedef enum
{
  GST_PLAYER_COLOR_BALANCE_BRIGHTNESS,
  GST_PLAYER_COLOR_BALANCE_CONTRAST,
  GST_PLAYER_COLOR_BALANCE_SATURATION,
  GST_PLAYER_COLOR_BALANCE_HUE,
} GstPlayerColorBalanceType;

const gchar *gst_player_color_balance_type_get_name   (GstPlayerColorBalanceType type);

#define GST_TYPE_PLAYER             (gst_player_get_type ())
#define GST_IS_PLAYER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PLAYER))
#define GST_IS_PLAYER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLAYER))
#define GST_PLAYER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PLAYER, GstPlayerClass))
#define GST_PLAYER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PLAYER, GstPlayer))
#define GST_PLAYER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PLAYER, GstPlayerClass))
#define GST_PLAYER_CAST(obj)        ((GstPlayer*)(obj))


GType        gst_player_get_type                      (void);

GstPlayer *  gst_player_new                           (GstPlayerVideoRenderer * video_renderer, GstPlayerSignalDispatcher * signal_dispatcher);

void         gst_player_play                          (GstPlayer    * player);
void         gst_player_pause                         (GstPlayer    * player);
void         gst_player_stop                          (GstPlayer    * player);

void         gst_player_seek                          (GstPlayer    * player,
                                                       GstClockTime   position);
void         gst_player_set_rate                      (GstPlayer    * player,
                                                       gdouble        rate);
gdouble      gst_player_get_rate                      (GstPlayer    * player);

gchar *      gst_player_get_uri                       (GstPlayer    * player);
void         gst_player_set_uri                       (GstPlayer    * player,
                                                       const gchar  * uri);

gchar *      gst_player_get_subtitle_uri              (GstPlayer    * player);
void         gst_player_set_subtitle_uri              (GstPlayer    * player,
                                                       const gchar *uri);

GstClockTime gst_player_get_position                  (GstPlayer    * player);
GstClockTime gst_player_get_duration                  (GstPlayer    * player);

gdouble      gst_player_get_volume                    (GstPlayer    * player);
void         gst_player_set_volume                    (GstPlayer    * player,
                                                       gdouble        val);

gboolean     gst_player_get_mute                      (GstPlayer    * player);
void         gst_player_set_mute                      (GstPlayer    * player,
                                                       gboolean       val);

GstElement * gst_player_get_pipeline                  (GstPlayer    * player);

void         gst_player_set_video_track_enabled       (GstPlayer    * player,
                                                       gboolean enabled);

void         gst_player_set_audio_track_enabled       (GstPlayer    * player,
                                                       gboolean enabled);

void         gst_player_set_subtitle_track_enabled    (GstPlayer    * player,
                                                       gboolean enabled);

gboolean     gst_player_set_audio_track               (GstPlayer    *player,
                                                       gint stream_index);

gboolean     gst_player_set_video_track               (GstPlayer    *player,
                                                       gint stream_index);

gboolean     gst_player_set_subtitle_track            (GstPlayer    *player,
                                                       gint stream_index);

GstPlayerMediaInfo * gst_player_get_media_info        (GstPlayer    * player);

GstPlayerAudioInfo * gst_player_get_current_audio_track
                                                      (GstPlayer    * player);

GstPlayerVideoInfo * gst_player_get_current_video_track
                                                      (GstPlayer    * player);

GstPlayerSubtitleInfo * gst_player_get_current_subtitle_track
                                                      (GstPlayer    * player);

gboolean     gst_player_set_visualization             (GstPlayer    * player,
                                                       const gchar *name);

void         gst_player_set_visualization_enabled     (GstPlayer    * player,
                                                       gboolean enabled);

gchar *      gst_player_get_current_visualization     (GstPlayer    * player);

gboolean     gst_player_has_color_balance             (GstPlayer    * player);
void         gst_player_set_color_balance             (GstPlayer    * player,
                                                       GstPlayerColorBalanceType type,
                                                       gdouble value);
gdouble      gst_player_get_color_balance             (GstPlayer    * player,
                                                       GstPlayerColorBalanceType type);


GstVideoMultiviewMode	 gst_player_get_multiview_mode (GstPlayer    * player);
void                     gst_player_set_multiview_mode (GstPlayer    * player,
                                                        GstVideoMultiviewMode mode);

GstVideoMultiviewFlags  gst_player_get_multiview_flags  (GstPlayer  * player);
void                    gst_player_set_multiview_flags  (GstPlayer  * player,
                                                         GstVideoMultiviewFlags flags);

gint64       gst_player_get_audio_video_offset        (GstPlayer    * player);
void         gst_player_set_audio_video_offset        (GstPlayer    * player,
                                                       gint64 offset);

gboolean       gst_player_set_config                  (GstPlayer * player,
                                                       GstStructure * config);
GstStructure * gst_player_get_config                  (GstPlayer * player);

/* helpers for configuring the config structure */

void           gst_player_config_set_user_agent       (GstStructure * config,
                                                       const gchar * agent);
gchar *        gst_player_config_get_user_agent       (const GstStructure * config);

void           gst_player_config_set_position_update_interval  (GstStructure * config,
                                                                guint          interval);
guint          gst_player_config_get_position_update_interval  (const GstStructure * config);

void           gst_player_config_set_seek_accurate (GstPlayer * player, gboolean accurate);
gboolean       gst_player_config_get_seek_accurate (const GstStructure * config);

typedef enum
{
  GST_PLAYER_THUMBNAIL_RAW_NATIVE = 0,
  GST_PLAYER_THUMBNAIL_RAW_xRGB,
  GST_PLAYER_THUMBNAIL_RAW_BGRx,
  GST_PLAYER_THUMBNAIL_JPG,
  GST_PLAYER_THUMBNAIL_PNG
} GstPlayerSnapshotFormat;

GstSample * gst_player_get_video_snapshot (GstPlayer * player,
    GstPlayerSnapshotFormat format, GstStructure * config);

G_END_DECLS

#endif /* __GST_PLAYER_H__ */
