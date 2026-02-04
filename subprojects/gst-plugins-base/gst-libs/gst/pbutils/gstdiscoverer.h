/* GStreamer
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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

#ifndef _GST_DISCOVERER_H_
#define _GST_DISCOVERER_H_

#include <gst/gst.h>
#include <gst/pbutils/pbutils-prelude.h>

G_BEGIN_DECLS

#define GST_TYPE_DISCOVERER_STREAM_INFO \
  (gst_discoverer_stream_info_get_type ())
#define GST_DISCOVERER_STREAM_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DISCOVERER_STREAM_INFO, GstDiscovererStreamInfo))
#define GST_IS_DISCOVERER_STREAM_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DISCOVERER_STREAM_INFO))
typedef struct _GstDiscovererStreamInfo GstDiscovererStreamInfo;
typedef GObjectClass GstDiscovererStreamInfoClass;

GST_PBUTILS_API
GType gst_discoverer_stream_info_get_type (void);

/**
 * GstDiscovererStreamInfo:
 *
 * Base structure for information concerning a media stream. Depending on the
 * stream type, one can find more media-specific information in
 * #GstDiscovererAudioInfo, #GstDiscovererVideoInfo, and
 * #GstDiscovererContainerInfo.
 *
 * The #GstDiscovererStreamInfo represents the topology of the stream. Siblings
 * can be iterated over with gst_discoverer_stream_info_get_next() and
 * gst_discoverer_stream_info_get_previous(). Children (sub-streams) of a
 * stream can be accessed using the #GstDiscovererContainerInfo API.
 *
 * As a simple example, if you run #GstDiscoverer on an AVI file with one audio
 * and one video stream, you will get a #GstDiscovererContainerInfo
 * corresponding to the AVI container, which in turn will have a
 * #GstDiscovererAudioInfo sub-stream and a #GstDiscovererVideoInfo sub-stream
 * for the audio and video streams respectively.
 */
#define gst_discoverer_stream_info_ref(info) ((GstDiscovererStreamInfo*) g_object_ref((GObject*) info))
#define gst_discoverer_stream_info_unref(info) (g_object_unref((GObject*) info))

GST_PBUTILS_API
GstDiscovererStreamInfo* gst_discoverer_stream_info_get_previous(GstDiscovererStreamInfo* info) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
GstDiscovererStreamInfo* gst_discoverer_stream_info_get_next(GstDiscovererStreamInfo* info) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
GstCaps*                 gst_discoverer_stream_info_get_caps(GstDiscovererStreamInfo* info) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
const GstTagList*        gst_discoverer_stream_info_get_tags(GstDiscovererStreamInfo* info);

GST_PBUTILS_API
const GstToc*            gst_discoverer_stream_info_get_toc(GstDiscovererStreamInfo* info);

GST_PBUTILS_API
const gchar*             gst_discoverer_stream_info_get_stream_id(GstDiscovererStreamInfo* info);

GST_PBUTILS_DEPRECATED_FOR(gst_discoverer_info_get_missing_elements_installer_details)
const GstStructure*      gst_discoverer_stream_info_get_misc(GstDiscovererStreamInfo* info);

GST_PBUTILS_API
const gchar *            gst_discoverer_stream_info_get_stream_type_nick(GstDiscovererStreamInfo* info);

GST_PBUTILS_API
gint                     gst_discoverer_stream_info_get_stream_number(GstDiscovererStreamInfo *info);

/**
 * GstDiscovererContainerInfo:
 *
 * #GstDiscovererStreamInfo specific to container streams.
 */
#define GST_TYPE_DISCOVERER_CONTAINER_INFO \
  (gst_discoverer_container_info_get_type ())
#define GST_DISCOVERER_CONTAINER_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DISCOVERER_CONTAINER_INFO, GstDiscovererContainerInfo))
#define GST_IS_DISCOVERER_CONTAINER_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DISCOVERER_CONTAINER_INFO))
typedef struct _GstDiscovererContainerInfo GstDiscovererContainerInfo;
typedef GObjectClass GstDiscovererContainerInfoClass;

GST_PBUTILS_API
GType gst_discoverer_container_info_get_type (void);

GST_PBUTILS_API
GList *gst_discoverer_container_info_get_streams(GstDiscovererContainerInfo *info) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
const GstTagList* gst_discoverer_container_info_get_tags(const GstDiscovererContainerInfo *info);


/**
 * GstDiscovererAudioInfo:
 *
 * #GstDiscovererStreamInfo specific to audio streams.
 */
#define GST_TYPE_DISCOVERER_AUDIO_INFO \
  (gst_discoverer_audio_info_get_type ())
#define GST_DISCOVERER_AUDIO_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DISCOVERER_AUDIO_INFO, GstDiscovererAudioInfo))
#define GST_IS_DISCOVERER_AUDIO_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DISCOVERER_AUDIO_INFO))
typedef struct _GstDiscovererAudioInfo GstDiscovererAudioInfo;
typedef GObjectClass GstDiscovererAudioInfoClass;

GST_PBUTILS_API
GType gst_discoverer_audio_info_get_type (void);

GST_PBUTILS_API
guint gst_discoverer_audio_info_get_channels(const GstDiscovererAudioInfo* info);

GST_PBUTILS_API
guint64 gst_discoverer_audio_info_get_channel_mask(const GstDiscovererAudioInfo* info);

GST_PBUTILS_API
guint gst_discoverer_audio_info_get_sample_rate(const GstDiscovererAudioInfo* info);

GST_PBUTILS_API
guint gst_discoverer_audio_info_get_depth(const GstDiscovererAudioInfo* info);

GST_PBUTILS_API
guint gst_discoverer_audio_info_get_bitrate(const GstDiscovererAudioInfo* info);

GST_PBUTILS_API
guint gst_discoverer_audio_info_get_max_bitrate(const GstDiscovererAudioInfo* info);

GST_PBUTILS_API
const gchar * gst_discoverer_audio_info_get_language(const GstDiscovererAudioInfo* info);

/**
 * GstDiscovererVideoInfo:
 *
 * #GstDiscovererStreamInfo specific to video streams (this includes images).
 */
#define GST_TYPE_DISCOVERER_VIDEO_INFO \
  (gst_discoverer_video_info_get_type ())
#define GST_DISCOVERER_VIDEO_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DISCOVERER_VIDEO_INFO, GstDiscovererVideoInfo))
#define GST_IS_DISCOVERER_VIDEO_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DISCOVERER_VIDEO_INFO))
typedef struct _GstDiscovererVideoInfo GstDiscovererVideoInfo;
typedef GObjectClass GstDiscovererVideoInfoClass;

GST_PBUTILS_API
GType gst_discoverer_video_info_get_type (void);

GST_PBUTILS_API
guint           gst_discoverer_video_info_get_width(const GstDiscovererVideoInfo* info);

GST_PBUTILS_API
guint           gst_discoverer_video_info_get_height(const GstDiscovererVideoInfo* info);

GST_PBUTILS_API
guint           gst_discoverer_video_info_get_depth(const GstDiscovererVideoInfo* info);

GST_PBUTILS_API
guint           gst_discoverer_video_info_get_framerate_num(const GstDiscovererVideoInfo* info);

GST_PBUTILS_API
guint           gst_discoverer_video_info_get_framerate_denom(const GstDiscovererVideoInfo* info);

GST_PBUTILS_API
guint           gst_discoverer_video_info_get_par_num(const GstDiscovererVideoInfo* info);

GST_PBUTILS_API
guint           gst_discoverer_video_info_get_par_denom(const GstDiscovererVideoInfo* info);

GST_PBUTILS_API
gboolean        gst_discoverer_video_info_is_interlaced(const GstDiscovererVideoInfo* info);

GST_PBUTILS_API
guint           gst_discoverer_video_info_get_bitrate(const GstDiscovererVideoInfo* info);

GST_PBUTILS_API
guint           gst_discoverer_video_info_get_max_bitrate(const GstDiscovererVideoInfo* info);

GST_PBUTILS_API
gboolean        gst_discoverer_video_info_is_image(const GstDiscovererVideoInfo* info);

/**
 * GstDiscovererSubtitleInfo:
 *
 * #GstDiscovererStreamInfo specific to subtitle streams (this includes text and
 * image based ones).
 */
#define GST_TYPE_DISCOVERER_SUBTITLE_INFO \
  (gst_discoverer_subtitle_info_get_type ())
#define GST_DISCOVERER_SUBTITLE_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DISCOVERER_SUBTITLE_INFO, GstDiscovererSubtitleInfo))
#define GST_IS_DISCOVERER_SUBTITLE_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DISCOVERER_SUBTITLE_INFO))
typedef struct _GstDiscovererSubtitleInfo GstDiscovererSubtitleInfo;
typedef GObjectClass GstDiscovererSubtitleInfoClass;

GST_PBUTILS_API
GType gst_discoverer_subtitle_info_get_type (void);

GST_PBUTILS_API
const gchar *   gst_discoverer_subtitle_info_get_language(const GstDiscovererSubtitleInfo* info);

/**
 * GstDiscovererResult:
 * @GST_DISCOVERER_OK: The discovery was successful
 * @GST_DISCOVERER_URI_INVALID: the URI is invalid
 * @GST_DISCOVERER_ERROR: an error happened and the GError is set
 * @GST_DISCOVERER_TIMEOUT: the discovery timed-out
 * @GST_DISCOVERER_BUSY: the discoverer was already discovering a file
 * @GST_DISCOVERER_MISSING_PLUGINS: Some plugins are missing for full discovery
 *
 * Result values for the discovery process.
 */
typedef enum {
  GST_DISCOVERER_OK               = 0,
  GST_DISCOVERER_URI_INVALID      = 1,
  GST_DISCOVERER_ERROR            = 2,
  GST_DISCOVERER_TIMEOUT          = 3,
  GST_DISCOVERER_BUSY             = 4,
  GST_DISCOVERER_MISSING_PLUGINS  = 5
} GstDiscovererResult;

/**
 * GstDiscovererSerializeFlags:
 * @GST_DISCOVERER_SERIALIZE_BASIC: Serialize only basic information, excluding
 * caps, tags and miscellaneous information
 * @GST_DISCOVERER_SERIALIZE_CAPS: Serialize the caps for each stream
 * @GST_DISCOVERER_SERIALIZE_TAGS: Serialize the tags for each stream
 * @GST_DISCOVERER_SERIALIZE_MISC: Serialize miscellaneous information for each stream
 * @GST_DISCOVERER_SERIALIZE_ALL: Serialize all the available info, including
 * caps, tags and miscellaneous information
 *
 * You can use these flags to control what is serialized by
 * gst_discoverer_info_to_variant()
 *
 * Since: 1.6
 */

typedef enum {
  GST_DISCOVERER_SERIALIZE_BASIC = 0,
  GST_DISCOVERER_SERIALIZE_CAPS  = 1 << 0,
  GST_DISCOVERER_SERIALIZE_TAGS  = 1 << 1,
  GST_DISCOVERER_SERIALIZE_MISC  = 1 << 2,
  GST_DISCOVERER_SERIALIZE_ALL   = GST_DISCOVERER_SERIALIZE_CAPS | GST_DISCOVERER_SERIALIZE_TAGS | GST_DISCOVERER_SERIALIZE_MISC
} GstDiscovererSerializeFlags;

/**
 * GstDiscovererInfo:
 *
 * Structure containing the information of a URI analyzed by #GstDiscoverer.
 */
typedef struct _GstDiscovererInfo GstDiscovererInfo;

#define GST_TYPE_DISCOVERER_INFO \
  (gst_discoverer_info_get_type ())
#define GST_DISCOVERER_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DISCOVERER_INFO, GstDiscovererInfo))
#define GST_IS_DISCOVERER_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DISCOVERER_INFO))
typedef GObjectClass GstDiscovererInfoClass;

GST_PBUTILS_API
GType gst_discoverer_info_get_type (void);

#define gst_discoverer_info_unref(info) (g_object_unref((GObject*)info))
#define gst_discoverer_info_ref(info) (g_object_ref((GObject*)info))

GST_PBUTILS_API
GstDiscovererInfo*        gst_discoverer_info_copy (GstDiscovererInfo * ptr) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
const gchar*              gst_discoverer_info_get_uri(const GstDiscovererInfo* info);

GST_PBUTILS_API
GstDiscovererResult       gst_discoverer_info_get_result(const GstDiscovererInfo* info);

GST_PBUTILS_API
GstDiscovererStreamInfo*  gst_discoverer_info_get_stream_info(GstDiscovererInfo* info) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
GList*                    gst_discoverer_info_get_stream_list(GstDiscovererInfo* info) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
GstClockTime              gst_discoverer_info_get_duration(const GstDiscovererInfo* info);

GST_PBUTILS_API
gboolean                  gst_discoverer_info_get_seekable(const GstDiscovererInfo* info);

GST_PBUTILS_API
gboolean                  gst_discoverer_info_get_live(const GstDiscovererInfo* info);

GST_PBUTILS_DEPRECATED_FOR(gst_discoverer_info_get_missing_elements_installer_details)
const GstStructure*       gst_discoverer_info_get_misc(const GstDiscovererInfo* info);

GST_PBUTILS_DEPRECATED
const GstTagList*         gst_discoverer_info_get_tags(const GstDiscovererInfo* info);
GST_PBUTILS_API
const GstToc*             gst_discoverer_info_get_toc(const GstDiscovererInfo* info);

GST_PBUTILS_API
const gchar**             gst_discoverer_info_get_missing_elements_installer_details(const GstDiscovererInfo* info);

GST_PBUTILS_API
GList *                   gst_discoverer_info_get_streams (GstDiscovererInfo *info,
							   GType streamtype) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
GList *                   gst_discoverer_info_get_audio_streams (GstDiscovererInfo *info) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
GList *                   gst_discoverer_info_get_video_streams (GstDiscovererInfo *info) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
GList *                   gst_discoverer_info_get_subtitle_streams (GstDiscovererInfo *info) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
GList *                   gst_discoverer_info_get_container_streams (GstDiscovererInfo *info) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
GVariant *                gst_discoverer_info_to_variant (GstDiscovererInfo *info,
                                                          GstDiscovererSerializeFlags flags) G_GNUC_WARN_UNUSED_RESULT;

GST_PBUTILS_API
GstDiscovererInfo *       gst_discoverer_info_from_variant (GVariant *variant);

GST_PBUTILS_API
void                      gst_discoverer_stream_info_list_free (GList *infos);

#define GST_TYPE_DISCOVERER \
  (gst_discoverer_get_type())
#define GST_DISCOVERER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DISCOVERER,GstDiscoverer))
#define GST_DISCOVERER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DISCOVERER,GstDiscovererClass))
#define GST_IS_DISCOVERER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DISCOVERER))
#define GST_IS_DISCOVERER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DISCOVERER))

typedef struct _GstDiscoverer GstDiscoverer;
typedef struct _GstDiscovererClass GstDiscovererClass;
typedef struct _GstDiscovererPrivate GstDiscovererPrivate;

/**
 * GstDiscoverer:
 *
 * The #GstDiscoverer structure.
 **/
struct _GstDiscoverer {
  GObject parent;

  /*< private >*/
  GstDiscovererPrivate *priv;

  gpointer _reserved[GST_PADDING];
};

struct _GstDiscovererClass {
  GObjectClass parentclass;

  /* signals */
  void        (*finished)        (GstDiscoverer *discoverer);
  void        (*starting)        (GstDiscoverer *discoverer);
  void        (*discovered)      (GstDiscoverer *discoverer,
                                  GstDiscovererInfo *info,
                                  const GError *err);
  void        (*source_setup)    (GstDiscoverer *discoverer,
                                  GstElement *source);
  /**
   * GstDiscovererClass::load_serialize_info:
   * @dc: the #GstDiscoverer
   * @uri: the uri to load the info from
   *
   * Loads the serialized info from the given uri.
   *
   * Returns: (transfer full): the #GstDiscovererInfo or %NULL if it could not be loaded
   *
   * Since: 1.24
   */
  GstDiscovererInfo *
  (*load_serialize_info)          (GstDiscoverer *dc,
                                   gchar *uri);

  gpointer _reserved[GST_PADDING - 1];
};

GST_PBUTILS_API
GType          gst_discoverer_get_type (void);

GST_PBUTILS_API
GstDiscoverer *gst_discoverer_new (GstClockTime timeout, GError **err) G_GNUC_WARN_UNUSED_RESULT;

/* Asynchronous API */

GST_PBUTILS_API
void           gst_discoverer_start (GstDiscoverer *discoverer);

GST_PBUTILS_API
void           gst_discoverer_stop (GstDiscoverer *discoverer);

GST_PBUTILS_API
gboolean       gst_discoverer_discover_uri_async (GstDiscoverer *discoverer,
						  const gchar *uri);

/* Synchronous API */

GST_PBUTILS_API
GstDiscovererInfo *
gst_discoverer_discover_uri (GstDiscoverer * discoverer,
			     const gchar * uri,
			     GError ** err) G_GNUC_WARN_UNUSED_RESULT;

/* === Builder API === */

/**
 * GstDiscovererInfoBuilder:
 *
 * Opaque structure for building #GstDiscovererInfo objects programmatically.
 *
 * Since: 1.30
 */
typedef struct _GstDiscovererInfoBuilder GstDiscovererInfoBuilder;

/**
 * GstDiscovererContainerInfoBuilder:
 *
 * Opaque structure for building #GstDiscovererContainerInfo objects programmatically.
 *
 * Since: 1.30
 */
typedef struct _GstDiscovererContainerInfoBuilder GstDiscovererContainerInfoBuilder;

/**
 * GstDiscovererAudioInfoBuilder:
 *
 * Opaque structure for building #GstDiscovererAudioInfo objects programmatically.
 *
 * Since: 1.30
 */
typedef struct _GstDiscovererAudioInfoBuilder GstDiscovererAudioInfoBuilder;

/**
 * GstDiscovererVideoInfoBuilder:
 *
 * Opaque structure for building #GstDiscovererVideoInfo objects programmatically.
 *
 * Since: 1.30
 */
typedef struct _GstDiscovererVideoInfoBuilder GstDiscovererVideoInfoBuilder;

/**
 * GstDiscovererSubtitleInfoBuilder:
 *
 * Opaque structure for building #GstDiscovererSubtitleInfo objects programmatically.
 *
 * Since: 1.30
 */
typedef struct _GstDiscovererSubtitleInfoBuilder GstDiscovererSubtitleInfoBuilder;

/* Info Builder */

GST_PBUTILS_API
GstDiscovererInfoBuilder * gst_discoverer_info_builder_new (const gchar *uri,
    GstDiscovererStreamInfo *stream_info);

GST_PBUTILS_API
GstDiscovererInfo * gst_discoverer_info_builder_build (GstDiscovererInfoBuilder *builder);

GST_PBUTILS_API
void gst_discoverer_info_builder_free (GstDiscovererInfoBuilder *builder);

GST_PBUTILS_API
GstDiscovererInfoBuilder * gst_discoverer_info_builder_set_duration (
    GstDiscovererInfoBuilder *builder, GstClockTime duration);

GST_PBUTILS_API
GstDiscovererInfoBuilder * gst_discoverer_info_builder_set_seekable (
    GstDiscovererInfoBuilder *builder, gboolean seekable);

GST_PBUTILS_API
GstDiscovererInfoBuilder * gst_discoverer_info_builder_set_live (
    GstDiscovererInfoBuilder *builder, gboolean live);

GST_PBUTILS_API
GstDiscovererInfoBuilder * gst_discoverer_info_builder_set_result (
    GstDiscovererInfoBuilder *builder, GstDiscovererResult result);

GST_PBUTILS_API
GstDiscovererInfoBuilder * gst_discoverer_info_builder_set_tags (
    GstDiscovererInfoBuilder *builder, GstTagList *tags);

/* Container Info Builder */

GST_PBUTILS_API
GstDiscovererContainerInfoBuilder * gst_discoverer_container_info_builder_new (
    GstCaps *caps);

GST_PBUTILS_API
GstDiscovererContainerInfo * gst_discoverer_container_info_builder_build (
    GstDiscovererContainerInfoBuilder *builder);

GST_PBUTILS_API
void gst_discoverer_container_info_builder_free (
    GstDiscovererContainerInfoBuilder *builder);

GST_PBUTILS_API
GstDiscovererContainerInfoBuilder * gst_discoverer_container_info_builder_set_tags (
    GstDiscovererContainerInfoBuilder *builder, GstTagList *tags);

GST_PBUTILS_API
GstDiscovererContainerInfoBuilder * gst_discoverer_container_info_builder_add_stream (
    GstDiscovererContainerInfoBuilder *builder, GstDiscovererStreamInfo *stream_info);

/* Audio Info Builder */
GST_PBUTILS_API
GstDiscovererAudioInfoBuilder * gst_discoverer_audio_info_builder_new (
    const gchar *stream_id, GstCaps *caps);

GST_PBUTILS_API
GstDiscovererAudioInfo * gst_discoverer_audio_info_builder_build (
    GstDiscovererAudioInfoBuilder *builder);

GST_PBUTILS_API
void gst_discoverer_audio_info_builder_free (GstDiscovererAudioInfoBuilder *builder);

GST_PBUTILS_API
GstDiscovererAudioInfoBuilder * gst_discoverer_audio_info_builder_set_tags (
    GstDiscovererAudioInfoBuilder *builder, GstTagList *tags);

GST_PBUTILS_API
GstDiscovererAudioInfoBuilder * gst_discoverer_audio_info_builder_set_bitrate (
    GstDiscovererAudioInfoBuilder *builder, guint bitrate);

GST_PBUTILS_API
GstDiscovererAudioInfoBuilder * gst_discoverer_audio_info_builder_set_max_bitrate (
    GstDiscovererAudioInfoBuilder *builder, guint max_bitrate);

GST_PBUTILS_API
GstDiscovererAudioInfoBuilder * gst_discoverer_audio_info_builder_set_language (
    GstDiscovererAudioInfoBuilder *builder, const gchar *language);

/* Video Info Builder */
GST_PBUTILS_API
GstDiscovererVideoInfoBuilder * gst_discoverer_video_info_builder_new (
    const gchar *stream_id, GstCaps *caps);

GST_PBUTILS_API
GstDiscovererVideoInfo * gst_discoverer_video_info_builder_build (
    GstDiscovererVideoInfoBuilder *builder);

GST_PBUTILS_API
void gst_discoverer_video_info_builder_free (GstDiscovererVideoInfoBuilder *builder);

GST_PBUTILS_API
GstDiscovererVideoInfoBuilder * gst_discoverer_video_info_builder_set_tags (
    GstDiscovererVideoInfoBuilder *builder, GstTagList *tags);

GST_PBUTILS_API
GstDiscovererVideoInfoBuilder * gst_discoverer_video_info_builder_set_bitrate (
    GstDiscovererVideoInfoBuilder *builder, guint bitrate);

GST_PBUTILS_API
GstDiscovererVideoInfoBuilder * gst_discoverer_video_info_builder_set_max_bitrate (
    GstDiscovererVideoInfoBuilder *builder, guint max_bitrate);

GST_PBUTILS_API
GstDiscovererVideoInfoBuilder * gst_discoverer_video_info_builder_set_interlaced (
    GstDiscovererVideoInfoBuilder *builder, gboolean interlaced);

GST_PBUTILS_API
GstDiscovererVideoInfoBuilder * gst_discoverer_video_info_builder_set_is_image (
    GstDiscovererVideoInfoBuilder *builder, gboolean is_image);

/* Subtitle Info Builder */
GST_PBUTILS_API
GstDiscovererSubtitleInfoBuilder * gst_discoverer_subtitle_info_builder_new (
    const gchar *stream_id, GstCaps *caps);

GST_PBUTILS_API
GstDiscovererSubtitleInfo * gst_discoverer_subtitle_info_builder_build (
    GstDiscovererSubtitleInfoBuilder *builder);

GST_PBUTILS_API
void gst_discoverer_subtitle_info_builder_free (GstDiscovererSubtitleInfoBuilder *builder);

GST_PBUTILS_API
GstDiscovererSubtitleInfoBuilder * gst_discoverer_subtitle_info_builder_set_tags (
    GstDiscovererSubtitleInfoBuilder *builder, GstTagList *tags);

GST_PBUTILS_API
GstDiscovererSubtitleInfoBuilder * gst_discoverer_subtitle_info_builder_set_language (
    GstDiscovererSubtitleInfoBuilder *builder, const gchar *language);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstDiscovererInfoBuilder, gst_discoverer_info_builder_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstDiscovererContainerInfoBuilder, gst_discoverer_container_info_builder_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstDiscovererAudioInfoBuilder, gst_discoverer_audio_info_builder_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstDiscovererVideoInfoBuilder, gst_discoverer_video_info_builder_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstDiscovererSubtitleInfoBuilder, gst_discoverer_subtitle_info_builder_free)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstDiscoverer, gst_object_unref)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstDiscovererAudioInfo, gst_object_unref)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstDiscovererContainerInfo, gst_object_unref)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstDiscovererInfo, gst_object_unref)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstDiscovererStreamInfo, gst_object_unref)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstDiscovererSubtitleInfo, gst_object_unref)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstDiscovererVideoInfo, gst_object_unref)

/* === Builder Macro DSL ===
 *
 * The DSL macros below rely on GCC statement expressions and are therefore
 * only available with GCC/Clang. Code targeting MSVC must use the function
 * builder API directly. */
#if defined(__GNUC__) || defined(__clang__)

/**
 * GST_DISCOVERER_INFO_BUILD:
 * @uri: the URI for the discoverer info
 * @stream: the stream info (single stream or container built with GST_DISCOVERER_CONTAINER_BUILD)
 * @...: statements using GST_DISCOVERER_INFO_* macros
 *
 * Macro to build a #GstDiscovererInfo using a declarative DSL.
 *
 * The @stream parameter is REQUIRED and determines the structure:
 * - For single-stream files: pass a stream directly (audio/video/subtitle)
 * - For container files: pass a container built with GST_DISCOVERER_CONTAINER_BUILD()
 *
 * Example for single stream:
 * |[<!-- language="C" -->
 * GstCaps *audio_caps = gst_caps_from_string ("audio/x-raw, rate=48000");
 * GstDiscovererInfo *info = GST_DISCOVERER_INFO_BUILD ("file:///test.flac",
 *     GST_DISCOVERER_AUDIO_STREAM_BUILD ("audio_0", audio_caps,
 *         GST_DISCOVERER_AUDIO_LANGUAGE ("en");
 *     ),
 *     GST_DISCOVERER_INFO_DURATION (10 * GST_SECOND);
 *     GST_DISCOVERER_INFO_SEEKABLE (TRUE);
 * );
 * gst_caps_unref (audio_caps);
 * ]|
 *
 * Example for container with multiple streams:
 * |[<!-- language="C" -->
 * GstCaps *container_caps = gst_caps_from_string ("video/quicktime");
 * GstDiscovererInfo *info = GST_DISCOVERER_INFO_BUILD ("file:///test.mp4",
 *     GST_DISCOVERER_CONTAINER_BUILD (container_caps,
 *         GST_DISCOVERER_CONTAINER_ADD_STREAM (
 *             GST_DISCOVERER_VIDEO_STREAM_BUILD ("video_0", video_caps, ));
 *         GST_DISCOVERER_CONTAINER_ADD_STREAM (
 *             GST_DISCOVERER_AUDIO_STREAM_BUILD ("audio_0", audio_caps, ));
 *     ),
 *     GST_DISCOVERER_INFO_DURATION (10 * GST_SECOND);
 *     GST_DISCOVERER_INFO_SEEKABLE (TRUE);
 * );
 * gst_caps_unref (container_caps);
 * ]|
 *
 * Returns: (transfer full): a new #GstDiscovererInfo
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_INFO_BUILD(uri, stream, ...) \
  ({ \
    GstDiscovererInfoBuilder *_gsdi_b = gst_discoverer_info_builder_new (uri, \
        GST_DISCOVERER_STREAM_INFO (stream)); \
    __VA_ARGS__; \
    gst_discoverer_info_builder_build (_gsdi_b); \
  })

/**
 * GST_DISCOVERER_INFO_DURATION:
 * @d: the duration in #GstClockTime
 *
 * Sets the duration on the info builder. Use within GST_DISCOVERER_INFO_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_INFO_DURATION(d) \
    _gsdi_b = gst_discoverer_info_builder_set_duration (_gsdi_b, d)

/**
 * GST_DISCOVERER_INFO_SEEKABLE:
 * @s: %TRUE if seekable
 *
 * Sets whether the stream is seekable. Use within GST_DISCOVERER_INFO_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_INFO_SEEKABLE(s) \
    _gsdi_b = gst_discoverer_info_builder_set_seekable (_gsdi_b, s)

/**
 * GST_DISCOVERER_INFO_LIVE:
 * @l: %TRUE if live
 *
 * Sets whether the stream is live. Use within GST_DISCOVERER_INFO_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_INFO_LIVE(l) \
    _gsdi_b = gst_discoverer_info_builder_set_live (_gsdi_b, l)

/**
 * GST_DISCOVERER_INFO_TAGS:
 * @t: a #GstTagList
 *
 * Sets the tags on the info builder. Use within GST_DISCOVERER_INFO_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_INFO_TAGS(t) \
    _gsdi_b = gst_discoverer_info_builder_set_tags (_gsdi_b, t)

/**
 * GST_DISCOVERER_INFO_RESULT:
 * @r: a #GstDiscovererResult
 *
 * Sets the result on the info builder. Use within GST_DISCOVERER_INFO_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_INFO_RESULT(r) \
    _gsdi_b = gst_discoverer_info_builder_set_result (_gsdi_b, r)

/* === Container Builder Macros === */

/**
 * GST_DISCOVERER_CONTAINER_BUILD:
 * @caps: the container #GstCaps
 * @...: statements using GST_DISCOVERER_CONTAINER_* macros
 *
 * Macro to build a #GstDiscovererContainerInfo with multiple streams.
 *
 * Example:
 * |[<!-- language="C" -->
 * GstCaps *container_caps = gst_caps_from_string ("video/quicktime");
 * GstDiscovererContainerInfo *container = GST_DISCOVERER_CONTAINER_BUILD (
 *     container_caps,
 *     GST_DISCOVERER_CONTAINER_ADD_STREAM (
 *         GST_DISCOVERER_VIDEO_STREAM_BUILD ("video_0", video_caps, ));
 *     GST_DISCOVERER_CONTAINER_ADD_STREAM (
 *         GST_DISCOVERER_AUDIO_STREAM_BUILD ("audio_0", audio_caps, ));
 * );
 * ]|
 *
 * Returns: (transfer full): a new #GstDiscovererContainerInfo
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_CONTAINER_BUILD(caps, ...) \
  ({ \
    GstDiscovererContainerInfoBuilder *_gsdi_c = \
        gst_discoverer_container_info_builder_new (caps); \
    __VA_ARGS__; \
    gst_discoverer_container_info_builder_build (_gsdi_c); \
  })

/**
 * GST_DISCOVERER_CONTAINER_TAGS:
 * @t: a #GstTagList
 *
 * Sets the container tags. Use within GST_DISCOVERER_CONTAINER_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_CONTAINER_TAGS(t) \
    _gsdi_c = gst_discoverer_container_info_builder_set_tags (_gsdi_c, t)

/**
 * GST_DISCOVERER_CONTAINER_ADD_STREAM:
 * @stream_info: a #GstDiscovererStreamInfo
 *
 * Adds a stream to the container. Use within GST_DISCOVERER_CONTAINER_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_CONTAINER_ADD_STREAM(stream_info) \
    _gsdi_c = gst_discoverer_container_info_builder_add_stream (_gsdi_c, \
        GST_DISCOVERER_STREAM_INFO (stream_info))

/* === Audio Stream Builder Macros === */

/**
 * GST_DISCOVERER_AUDIO_STREAM_BUILD:
 * @stream_id: the stream ID
 * @caps: the #GstCaps for the audio stream
 * @...: statements using GST_DISCOVERER_AUDIO_* macros
 *
 * Macro to build a #GstDiscovererAudioInfo.
 *
 * Returns: (transfer full): a new #GstDiscovererAudioInfo
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_AUDIO_STREAM_BUILD(stream_id, caps, ...) \
  ({ \
    GstDiscovererAudioInfoBuilder *_gsdi_as = \
        gst_discoverer_audio_info_builder_new (stream_id, caps); \
    __VA_ARGS__; \
    gst_discoverer_audio_info_builder_build (_gsdi_as); \
  })

/**
 * GST_DISCOVERER_AUDIO_LANGUAGE:
 * @l: the language code
 *
 * Sets the language. Use within GST_DISCOVERER_AUDIO_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_AUDIO_LANGUAGE(l) \
    _gsdi_as = gst_discoverer_audio_info_builder_set_language (_gsdi_as, l)

/**
 * GST_DISCOVERER_AUDIO_BITRATE:
 * @br: the bitrate in bits/second
 *
 * Sets the bitrate. Use within GST_DISCOVERER_AUDIO_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_AUDIO_BITRATE(br) \
    _gsdi_as = gst_discoverer_audio_info_builder_set_bitrate (_gsdi_as, br)

/**
 * GST_DISCOVERER_AUDIO_MAX_BITRATE:
 * @br: the maximum bitrate in bits/second
 *
 * Sets the maximum bitrate. Use within GST_DISCOVERER_AUDIO_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_AUDIO_MAX_BITRATE(br) \
    _gsdi_as = gst_discoverer_audio_info_builder_set_max_bitrate (_gsdi_as, br)

/**
 * GST_DISCOVERER_AUDIO_TAGS:
 * @t: a #GstTagList
 *
 * Sets the stream tags. Use within GST_DISCOVERER_AUDIO_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_AUDIO_TAGS(t) \
    _gsdi_as = gst_discoverer_audio_info_builder_set_tags (_gsdi_as, t)

/* === Video Stream Builder Macros === */

/**
 * GST_DISCOVERER_VIDEO_STREAM_BUILD:
 * @stream_id: the stream ID
 * @caps: the #GstCaps for the video stream
 * @...: statements using GST_DISCOVERER_VIDEO_* macros
 *
 * Macro to build a #GstDiscovererVideoInfo.
 *
 * Returns: (transfer full): a new #GstDiscovererVideoInfo
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_VIDEO_STREAM_BUILD(stream_id, caps, ...) \
  ({ \
    GstDiscovererVideoInfoBuilder *_gsdi_vs = \
        gst_discoverer_video_info_builder_new (stream_id, caps); \
    __VA_ARGS__; \
    gst_discoverer_video_info_builder_build (_gsdi_vs); \
  })

/**
 * GST_DISCOVERER_VIDEO_BITRATE:
 * @br: the bitrate in bits/second
 *
 * Sets the bitrate. Use within GST_DISCOVERER_VIDEO_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_VIDEO_BITRATE(br) \
    _gsdi_vs = gst_discoverer_video_info_builder_set_bitrate (_gsdi_vs, br)

/**
 * GST_DISCOVERER_VIDEO_MAX_BITRATE:
 * @br: the maximum bitrate in bits/second
 *
 * Sets the maximum bitrate. Use within GST_DISCOVERER_VIDEO_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_VIDEO_MAX_BITRATE(br) \
    _gsdi_vs = gst_discoverer_video_info_builder_set_max_bitrate (_gsdi_vs, br)

/**
 * GST_DISCOVERER_VIDEO_INTERLACED:
 * @i: %TRUE if interlaced
 *
 * Sets whether the video is interlaced. Use within GST_DISCOVERER_VIDEO_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_VIDEO_INTERLACED(i) \
    _gsdi_vs = gst_discoverer_video_info_builder_set_interlaced (_gsdi_vs, i)

/**
 * GST_DISCOVERER_VIDEO_IS_IMAGE:
 * @i: %TRUE if this is an image
 *
 * Sets whether this is an image. Use within GST_DISCOVERER_VIDEO_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_VIDEO_IS_IMAGE(i) \
    _gsdi_vs = gst_discoverer_video_info_builder_set_is_image (_gsdi_vs, i)

/**
 * GST_DISCOVERER_VIDEO_TAGS:
 * @t: a #GstTagList
 *
 * Sets the stream tags. Use within GST_DISCOVERER_VIDEO_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_VIDEO_TAGS(t) \
    _gsdi_vs = gst_discoverer_video_info_builder_set_tags (_gsdi_vs, t)

/* === Subtitle Stream Builder Macros === */

/**
 * GST_DISCOVERER_SUBTITLE_STREAM_BUILD:
 * @stream_id: the stream ID
 * @caps: the #GstCaps for the subtitle stream
 * @...: statements using GST_DISCOVERER_SUBTITLE_* macros
 *
 * Macro to build a #GstDiscovererSubtitleInfo.
 *
 * Returns: (transfer full): a new #GstDiscovererSubtitleInfo
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_SUBTITLE_STREAM_BUILD(stream_id, caps, ...) \
  ({ \
    GstDiscovererSubtitleInfoBuilder *_gsdi_ss = \
        gst_discoverer_subtitle_info_builder_new (stream_id, caps); \
    __VA_ARGS__; \
    gst_discoverer_subtitle_info_builder_build (_gsdi_ss); \
  })

/**
 * GST_DISCOVERER_SUBTITLE_LANGUAGE:
 * @l: the language code
 *
 * Sets the language. Use within GST_DISCOVERER_SUBTITLE_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_SUBTITLE_LANGUAGE(l) \
    _gsdi_ss = gst_discoverer_subtitle_info_builder_set_language (_gsdi_ss, l)

/**
 * GST_DISCOVERER_SUBTITLE_TAGS:
 * @t: a #GstTagList
 *
 * Sets the stream tags. Use within GST_DISCOVERER_SUBTITLE_STREAM_BUILD().
 *
 * Since: 1.30
 */
#define GST_DISCOVERER_SUBTITLE_TAGS(t) \
    _gsdi_ss = gst_discoverer_subtitle_info_builder_set_tags (_gsdi_ss, t)

#endif /* defined(__GNUC__) || defined(__clang__) */

G_END_DECLS

#endif /* _GST_DISCOVERER_H */
