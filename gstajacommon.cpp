/* GStreamer
 * Copyright (C) 2021 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <semaphore.h>

#include "gstajacommon.h"

GST_DEBUG_CATEGORY_STATIC(gst_aja_debug);

#define GST_CAT_DEFAULT gst_aja_debug

typedef struct {
  GstAjaVideoFormat gst_format;
  NTV2VideoFormat aja_format;
  NTV2VideoFormat quad_format;
} FormatMapEntry;

static const FormatMapEntry format_map[] = {
    {GST_AJA_VIDEO_FORMAT_1080i_5000, NTV2_FORMAT_1080i_5000,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080i_5994, NTV2_FORMAT_1080i_5994,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080i_6000, NTV2_FORMAT_1080i_6000,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_720p_5994, NTV2_FORMAT_720p_5994,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_720p_6000, NTV2_FORMAT_720p_6000,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_2997, NTV2_FORMAT_1080p_2997,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_3000, NTV2_FORMAT_1080p_3000,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_2500, NTV2_FORMAT_1080p_2500,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_2398, NTV2_FORMAT_1080p_2398,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_2400, NTV2_FORMAT_1080p_2400,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_720p_5000, NTV2_FORMAT_720p_5000,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_720p_2398, NTV2_FORMAT_720p_2398,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_720p_5000, NTV2_FORMAT_720p_2500,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_3000, NTV2_FORMAT_1080p_5000_A,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_5994_A, NTV2_FORMAT_1080p_5994_A,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_6000_A, NTV2_FORMAT_1080p_6000_A,
     NTV2_FORMAT_UNKNOWN},

    {GST_AJA_VIDEO_FORMAT_625_5000, NTV2_FORMAT_625_5000, NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_525_5994, NTV2_FORMAT_525_5994, NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_525_2398, NTV2_FORMAT_525_2398, NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_525_2400, NTV2_FORMAT_525_2400, NTV2_FORMAT_UNKNOWN},

    {GST_AJA_VIDEO_FORMAT_1080p_DCI_2398, NTV2_FORMAT_1080p_2K_2398,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_DCI_2400, NTV2_FORMAT_1080p_2K_2400,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_DCI_2500, NTV2_FORMAT_1080p_2K_2500,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_DCI_2997, NTV2_FORMAT_1080p_2K_2997,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_DCI_3000, NTV2_FORMAT_1080p_2K_3000,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_DCI_5000_A, NTV2_FORMAT_1080p_2K_5000_A,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_DCI_5994_A, NTV2_FORMAT_1080p_2K_5994_A,
     NTV2_FORMAT_UNKNOWN},
    {GST_AJA_VIDEO_FORMAT_1080p_DCI_6000_A, NTV2_FORMAT_1080p_2K_6000_A,
     NTV2_FORMAT_UNKNOWN},

    {GST_AJA_VIDEO_FORMAT_2160p_2398, NTV2_FORMAT_3840x2160p_2398,
     NTV2_FORMAT_4x1920x1080p_2398},
    {GST_AJA_VIDEO_FORMAT_2160p_2400, NTV2_FORMAT_3840x2160p_2400,
     NTV2_FORMAT_4x1920x1080p_2400},
    {GST_AJA_VIDEO_FORMAT_2160p_2500, NTV2_FORMAT_3840x2160p_2500,
     NTV2_FORMAT_4x1920x1080p_2500},
    {GST_AJA_VIDEO_FORMAT_2160p_2997, NTV2_FORMAT_3840x2160p_2997,
     NTV2_FORMAT_4x1920x1080p_2997},
    {GST_AJA_VIDEO_FORMAT_2160p_3000, NTV2_FORMAT_3840x2160p_3000,
     NTV2_FORMAT_4x1920x1080p_3000},
    {GST_AJA_VIDEO_FORMAT_2160p_5000, NTV2_FORMAT_3840x2160p_5000,
     NTV2_FORMAT_4x1920x1080p_5000},
    {GST_AJA_VIDEO_FORMAT_2160p_5994, NTV2_FORMAT_3840x2160p_5994,
     NTV2_FORMAT_4x1920x1080p_5994},
    {GST_AJA_VIDEO_FORMAT_2160p_6000, NTV2_FORMAT_3840x2160p_6000,
     NTV2_FORMAT_4x1920x1080p_6000},

    {GST_AJA_VIDEO_FORMAT_2160p_DCI_2398, NTV2_FORMAT_4096x2160p_2398,
     NTV2_FORMAT_4x2048x1080p_2398},
    {GST_AJA_VIDEO_FORMAT_2160p_DCI_2400, NTV2_FORMAT_4096x2160p_2400,
     NTV2_FORMAT_4x2048x1080p_2400},
    {GST_AJA_VIDEO_FORMAT_2160p_DCI_2500, NTV2_FORMAT_4096x2160p_2500,
     NTV2_FORMAT_4x2048x1080p_2500},
    {GST_AJA_VIDEO_FORMAT_2160p_DCI_2997, NTV2_FORMAT_4096x2160p_2997,
     NTV2_FORMAT_4x2048x1080p_2997},
    {GST_AJA_VIDEO_FORMAT_2160p_DCI_3000, NTV2_FORMAT_4096x2160p_3000,
     NTV2_FORMAT_4x2048x1080p_3000},
    {GST_AJA_VIDEO_FORMAT_2160p_DCI_5000, NTV2_FORMAT_4096x2160p_5000,
     NTV2_FORMAT_4x2048x1080p_5000},
    {GST_AJA_VIDEO_FORMAT_2160p_DCI_5994, NTV2_FORMAT_4096x2160p_5994,
     NTV2_FORMAT_4x2048x1080p_5994},
    {GST_AJA_VIDEO_FORMAT_2160p_DCI_6000, NTV2_FORMAT_4096x2160p_6000,
     NTV2_FORMAT_4x2048x1080p_6000},

    {GST_AJA_VIDEO_FORMAT_4320p_2398, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x3840x2160p_2398},
    {GST_AJA_VIDEO_FORMAT_4320p_2400, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x3840x2160p_2400},
    {GST_AJA_VIDEO_FORMAT_4320p_2500, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x3840x2160p_2500},
    {GST_AJA_VIDEO_FORMAT_4320p_2997, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x3840x2160p_2997},
    {GST_AJA_VIDEO_FORMAT_4320p_3000, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x3840x2160p_3000},
    {GST_AJA_VIDEO_FORMAT_4320p_5000, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x3840x2160p_5000},
    {GST_AJA_VIDEO_FORMAT_4320p_5994, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x3840x2160p_5994},
    {GST_AJA_VIDEO_FORMAT_4320p_6000, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x3840x2160p_6000},

    {GST_AJA_VIDEO_FORMAT_4320p_DCI_2398, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x4096x2160p_2398},
    {GST_AJA_VIDEO_FORMAT_4320p_DCI_2400, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x4096x2160p_2400},
    {GST_AJA_VIDEO_FORMAT_4320p_DCI_2500, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x4096x2160p_2500},
    {GST_AJA_VIDEO_FORMAT_4320p_DCI_2997, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x4096x2160p_2997},
    {GST_AJA_VIDEO_FORMAT_4320p_DCI_3000, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x4096x2160p_3000},
    {GST_AJA_VIDEO_FORMAT_4320p_DCI_5000, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x4096x2160p_5000},
    {GST_AJA_VIDEO_FORMAT_4320p_DCI_5994, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x4096x2160p_5994},
    {GST_AJA_VIDEO_FORMAT_4320p_DCI_6000, NTV2_FORMAT_UNKNOWN,
     NTV2_FORMAT_4x4096x2160p_6000},
};

GstCaps *gst_ntv2_supported_caps(NTV2DeviceID device_id) {
  GstCaps *caps = gst_caps_new_empty();

  for (gsize i = 0; i < G_N_ELEMENTS(format_map); i++) {
    const FormatMapEntry &format = format_map[i];
    GstCaps *tmp = NULL;

    if (device_id == DEVICE_ID_INVALID) {
      tmp = gst_aja_video_format_to_caps(format.gst_format);
    } else if ((format.aja_format != NTV2_FORMAT_UNKNOWN &&
                ::NTV2DeviceCanDoVideoFormat(device_id, format.aja_format)) ||
               (format.quad_format != NTV2_FORMAT_UNKNOWN &&
                ::NTV2DeviceCanDoVideoFormat(device_id, format.quad_format))) {
      tmp = gst_aja_video_format_to_caps(format.gst_format);
    }

    if (tmp) {
      // Widescreen PAL/NTSC
      if (format.gst_format == GST_AJA_VIDEO_FORMAT_525_2398 ||
          format.gst_format == GST_AJA_VIDEO_FORMAT_525_2400 ||
          format.gst_format == GST_AJA_VIDEO_FORMAT_525_5994) {
        GstCaps *tmp2 = gst_caps_copy(tmp);
        gst_caps_set_simple(tmp2, "pixel-aspect-ratio", GST_TYPE_FRACTION, 40,
                            33, NULL);
        gst_caps_append(tmp, tmp2);
      } else if (format.gst_format == GST_AJA_VIDEO_FORMAT_625_5000) {
        GstCaps *tmp2 = gst_caps_copy(tmp);
        gst_caps_set_simple(tmp2, "pixel-aspect-ratio", GST_TYPE_FRACTION, 16,
                            11, NULL);
        gst_caps_append(tmp, tmp2);
      }

      gst_caps_append(caps, tmp);
    }
  }

  return caps;
}

GstCaps *gst_aja_video_format_to_caps(GstAjaVideoFormat format) {
  const FormatMapEntry *entry = NULL;

  for (gsize i = 0; i < G_N_ELEMENTS(format_map); i++) {
    const FormatMapEntry &tmp = format_map[i];

    if (tmp.gst_format == format) {
      entry = &tmp;
      break;
    }
  }
  g_assert(entry != NULL);

  if (entry->aja_format != NTV2_FORMAT_UNKNOWN)
    return gst_ntv2_video_format_to_caps(entry->aja_format);
  if (entry->quad_format != NTV2_FORMAT_UNKNOWN)
    return gst_ntv2_video_format_to_caps(entry->quad_format);

  g_assert_not_reached();
}

bool gst_video_info_from_aja_video_format(GstVideoInfo *info,
                                          GstAjaVideoFormat format) {
  const FormatMapEntry *entry = NULL;

  for (gsize i = 0; i < G_N_ELEMENTS(format_map); i++) {
    const FormatMapEntry &tmp = format_map[i];

    if (tmp.gst_format == format) {
      entry = &tmp;
      break;
    }
  }
  g_assert(entry != NULL);

  if (entry->aja_format != NTV2_FORMAT_UNKNOWN)
    return gst_video_info_from_ntv2_video_format(info, entry->aja_format);
  if (entry->quad_format != NTV2_FORMAT_UNKNOWN)
    return gst_video_info_from_ntv2_video_format(info, entry->quad_format);

  g_assert_not_reached();
}

GstCaps *gst_ntv2_video_format_to_caps(NTV2VideoFormat format) {
  GstVideoInfo info;
  GstCaps *caps;

  if (!gst_video_info_from_ntv2_video_format(&info, format)) return NULL;

  caps = gst_video_info_to_caps(&info);
  if (!caps) return caps;

  guint n = gst_caps_get_size(caps);
  for (guint i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure(caps, i);

    gst_structure_remove_fields(s, "chroma-site", "colorimetry", NULL);
  }

  return caps;
}

bool gst_video_info_from_ntv2_video_format(GstVideoInfo *info,
                                           NTV2VideoFormat format) {
  if (format == NTV2_FORMAT_UNKNOWN) return false;

  guint width = ::GetDisplayWidth(format);
  guint height = ::GetDisplayHeight(format);
  NTV2FrameRate fps = ::GetNTV2FrameRateFromVideoFormat(format);
  guint fps_n, fps_d;
  ::GetFramesPerSecond(fps, fps_n, fps_d);

  gst_video_info_set_format(info, GST_VIDEO_FORMAT_v210, width, height);
  info->fps_n = fps_n;
  info->fps_d = fps_d;
  if (NTV2_IS_525_FORMAT(format)) {
    info->par_n = 10;
    info->par_d = 11;
  } else if (NTV2_IS_625_FORMAT(format)) {
    info->par_n = 12;
    info->par_d = 11;
  }
  info->interlace_mode = !::IsProgressiveTransport(format)
                             ? GST_VIDEO_INTERLACE_MODE_INTERLEAVED
                             : GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

  return true;
}

NTV2VideoFormat gst_ntv2_video_format_from_caps(const GstCaps *caps,
                                                bool quad) {
  GstVideoInfo info;

  if (!gst_video_info_from_caps(&info, caps)) return NTV2_FORMAT_UNKNOWN;

  for (gsize i = 0; i < G_N_ELEMENTS(format_map); i++) {
    const FormatMapEntry &format = format_map[i];
    NTV2VideoFormat f = !quad ? format.aja_format : format.quad_format;

    if (f == NTV2_FORMAT_UNKNOWN) continue;

    guint width = ::GetDisplayWidth(f);
    guint height = ::GetDisplayHeight(f);
    NTV2FrameRate fps = ::GetNTV2FrameRateFromVideoFormat(f);
    guint fps_n, fps_d;
    ::GetFramesPerSecond(fps, fps_n, fps_d);

    if (width == (guint)info.width && height == (guint)info.height &&
        (guint)info.fps_n == fps_n && (guint)info.fps_d == fps_d &&
        ((!::IsProgressiveTransport(f) &&
          info.interlace_mode == GST_VIDEO_INTERLACE_MODE_INTERLEAVED) ||
         (::IsProgressiveTransport(f) &&
          info.interlace_mode == GST_VIDEO_INTERLACE_MODE_PROGRESSIVE)))
      return f;
  }

  return NTV2_FORMAT_UNKNOWN;
}

GstAjaVideoFormat gst_aja_video_format_from_caps(const GstCaps *caps) {
  GstVideoInfo info;

  if (!gst_video_info_from_caps(&info, caps))
    return GST_AJA_VIDEO_FORMAT_INVALID;

  for (gsize i = 0; i < G_N_ELEMENTS(format_map); i++) {
    const FormatMapEntry &format = format_map[i];
    NTV2VideoFormat f = (format.aja_format != NTV2_FORMAT_UNKNOWN)
                            ? format.aja_format
                            : format.quad_format;

    if (f == NTV2_FORMAT_UNKNOWN) continue;

    guint width = ::GetDisplayWidth(f);
    guint height = ::GetDisplayHeight(f);
    NTV2FrameRate fps = ::GetNTV2FrameRateFromVideoFormat(f);
    guint fps_n, fps_d;
    ::GetFramesPerSecond(fps, fps_n, fps_d);

    if (width == (guint)info.width && height == (guint)info.height &&
        (guint)info.fps_n == fps_n && (guint)info.fps_d == fps_d &&
        ((!::IsProgressiveTransport(f) &&
          info.interlace_mode == GST_VIDEO_INTERLACE_MODE_INTERLEAVED) ||
         (::IsProgressiveTransport(f) &&
          info.interlace_mode == GST_VIDEO_INTERLACE_MODE_PROGRESSIVE)))
      return format.gst_format;
  }

  return GST_AJA_VIDEO_FORMAT_INVALID;
}

GstAjaVideoFormat gst_aja_video_format_from_ntv2_format(
    NTV2VideoFormat format) {
  if (format == NTV2_FORMAT_UNKNOWN) return GST_AJA_VIDEO_FORMAT_INVALID;

  for (gsize i = 0; i < G_N_ELEMENTS(format_map); i++) {
    const FormatMapEntry &entry = format_map[i];
    if (entry.aja_format == format || entry.quad_format == format)
      return entry.gst_format;
  }

  return GST_AJA_VIDEO_FORMAT_INVALID;
}

NTV2VideoFormat gst_ntv2_video_format_from_aja_format(GstAjaVideoFormat format,
                                                      bool quad) {
  if (format == GST_AJA_VIDEO_FORMAT_INVALID) return NTV2_FORMAT_UNKNOWN;

  for (gsize i = 0; i < G_N_ELEMENTS(format_map); i++) {
    const FormatMapEntry &entry = format_map[i];
    if (entry.gst_format == format) {
      if (!quad && entry.aja_format != NTV2_FORMAT_UNKNOWN)
        return entry.aja_format;
      if (quad && entry.quad_format != NTV2_FORMAT_UNKNOWN)
        return entry.quad_format;
    }
  }

  return NTV2_FORMAT_UNKNOWN;
}

bool gst_ntv2_video_format_is_quad(NTV2VideoFormat format) {
  return (format >= NTV2_FORMAT_FIRST_4K_DEF_FORMAT &&
          format < NTV2_FORMAT_END_4K_DEF_FORMATS) ||
         (format >= NTV2_FORMAT_FIRST_4K_DEF_FORMAT2 &&
          format < NTV2_FORMAT_END_4K_DEF_FORMATS2) ||
         (format >= NTV2_FORMAT_FIRST_UHD2_DEF_FORMAT &&
          format < NTV2_FORMAT_END_UHD2_DEF_FORMATS) ||
         (format >= NTV2_FORMAT_FIRST_UHD2_FULL_DEF_FORMAT &&
          format < NTV2_FORMAT_END_UHD2_FULL_DEF_FORMATS);
}

GType gst_aja_audio_meta_api_get_type(void) {
  static volatile GType type;

  if (g_once_init_enter(&type)) {
    static const gchar *tags[] = {NULL};
    GType _type = gst_meta_api_type_register("GstAjaAudioMetaAPI", tags);
    GST_INFO("registering");
    g_once_init_leave(&type, _type);
  }
  return type;
}

static gboolean gst_aja_audio_meta_transform(GstBuffer *dest, GstMeta *meta,
                                             GstBuffer *buffer, GQuark type,
                                             gpointer data) {
  GstAjaAudioMeta *dmeta, *smeta;

  if (GST_META_TRANSFORM_IS_COPY(type)) {
    smeta = (GstAjaAudioMeta *)meta;

    GST_DEBUG("copy AJA audio metadata");
    dmeta = gst_buffer_add_aja_audio_meta(dest, smeta->buffer);
    if (!dmeta) return FALSE;
  } else {
    /* return FALSE, if transform type is not supported */
    return FALSE;
  }
  return TRUE;
}

static gboolean gst_aja_audio_meta_init(GstMeta *meta, gpointer params,
                                        GstBuffer *buffer) {
  GstAjaAudioMeta *emeta = (GstAjaAudioMeta *)meta;

  emeta->buffer = NULL;

  return TRUE;
}

static void gst_aja_audio_meta_free(GstMeta *meta, GstBuffer *buffer) {
  GstAjaAudioMeta *emeta = (GstAjaAudioMeta *)meta;

  gst_buffer_replace(&emeta->buffer, NULL);
}

const GstMetaInfo *gst_aja_audio_meta_get_info(void) {
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter((GstMetaInfo **)&meta_info)) {
    const GstMetaInfo *mi = gst_meta_register(
        GST_AJA_AUDIO_META_API_TYPE, "GstAjaAudioMeta", sizeof(GstAjaAudioMeta),
        gst_aja_audio_meta_init, gst_aja_audio_meta_free,
        gst_aja_audio_meta_transform);
    g_once_init_leave((GstMetaInfo **)&meta_info, (GstMetaInfo *)mi);
  }
  return meta_info;
}

GstAjaAudioMeta *gst_buffer_add_aja_audio_meta(GstBuffer *buffer,
                                               GstBuffer *audio_buffer) {
  GstAjaAudioMeta *meta;

  g_return_val_if_fail(buffer != NULL, NULL);
  g_return_val_if_fail(audio_buffer != NULL, NULL);

  meta = (GstAjaAudioMeta *)gst_buffer_add_meta(buffer, GST_AJA_AUDIO_META_INFO,
                                                NULL);

  meta->buffer = gst_buffer_ref(audio_buffer);

  return meta;
}

typedef struct {
  GstMemory mem;

  guint8 *data;
} GstAjaMemory;

G_DEFINE_TYPE(GstAjaAllocator, gst_aja_allocator, GST_TYPE_ALLOCATOR);

static inline void _aja_memory_init(GstAjaAllocator *alloc, GstAjaMemory *mem,
                                    GstMemoryFlags flags, GstMemory *parent,
                                    gpointer data, gsize maxsize, gsize offset,
                                    gsize size) {
  gst_memory_init(GST_MEMORY_CAST(mem), flags, GST_ALLOCATOR(alloc), parent,
                  maxsize, 4095, offset, size);

  mem->data = (guint8 *)data;
}

static inline GstAjaMemory *_aja_memory_new(GstAjaAllocator *alloc,
                                            GstMemoryFlags flags,
                                            GstAjaMemory *parent, gpointer data,
                                            gsize maxsize, gsize offset,
                                            gsize size) {
  GstAjaMemory *mem;

  mem = (GstAjaMemory *)g_slice_alloc(sizeof(GstAjaMemory));
  _aja_memory_init(alloc, mem, flags, (GstMemory *)parent, data, maxsize,
                   offset, size);

  return mem;
}

static GstAjaMemory *_aja_memory_new_block(GstAjaAllocator *alloc,
                                           GstMemoryFlags flags, gsize maxsize,
                                           gsize offset, gsize size) {
  GstAjaMemory *mem;
  guint8 *data;

  mem = (GstAjaMemory *)g_slice_alloc(sizeof(GstAjaMemory));

  data = (guint8 *)AJAMemory::AllocateAligned(maxsize, 4096);
  GST_DEBUG_OBJECT(alloc, "Allocated %" G_GSIZE_FORMAT " at %p", maxsize, data);
  if (!alloc->device->device->DMABufferLock((ULWord *)data, maxsize, true)) {
    GST_WARNING_OBJECT(alloc, "Failed to pre-lock memory");
  }

  _aja_memory_init(alloc, mem, flags, NULL, data, maxsize, offset, size);

  return mem;
}

static gpointer _aja_memory_map(GstAjaMemory *mem, gsize maxsize,
                                GstMapFlags flags) {
  return mem->data;
}

static gboolean _aja_memory_unmap(GstAjaMemory *mem) { return TRUE; }

static GstMemory *_aja_memory_copy(GstAjaMemory *mem, gssize offset,
                                   gsize size) {
  GstMemory *copy;
  GstMapInfo map;

  if (size == (gsize)-1)
    size = mem->mem.size > (gsize)offset ? mem->mem.size - offset : 0;

  copy = gst_allocator_alloc(mem->mem.allocator, size, NULL);
  gst_memory_map(copy, &map, GST_MAP_READ);
  GST_DEBUG("memcpy %" G_GSIZE_FORMAT " memory %p -> %p", size, mem, copy);
  memcpy(map.data, mem->data + mem->mem.offset + offset, size);
  gst_memory_unmap(copy, &map);

  return copy;
}

static GstAjaMemory *_aja_memory_share(GstAjaMemory *mem, gssize offset,
                                       gsize size) {
  GstAjaMemory *sub;
  GstAjaMemory *parent;

  /* find the real parent */
  if ((parent = (GstAjaMemory *)mem->mem.parent) == NULL)
    parent = (GstAjaMemory *)mem;

  if (size == (gsize)-1) size = mem->mem.size - offset;

  sub = _aja_memory_new(GST_AJA_ALLOCATOR(parent->mem.allocator),
                        (GstMemoryFlags)(GST_MINI_OBJECT_FLAGS(parent) |
                                         GST_MINI_OBJECT_FLAG_LOCK_READONLY),
                        parent, parent->data, mem->mem.maxsize,
                        mem->mem.offset + offset, size);

  return sub;
}

static GstMemory *gst_aja_allocator_alloc(GstAllocator *alloc, gsize size,
                                          GstAllocationParams *params) {
  g_warn_if_fail(params->prefix == 0);
  g_warn_if_fail(params->padding == 0);

  return (GstMemory *)_aja_memory_new_block(GST_AJA_ALLOCATOR(alloc),
                                            params->flags, size, 0, size);
}

static void gst_aja_allocator_free(GstAllocator *alloc, GstMemory *mem) {
  GstAjaMemory *dmem = (GstAjaMemory *)mem;

  if (!mem->parent) {
    GstAjaAllocator *aja_alloc = GST_AJA_ALLOCATOR(alloc);

    GST_DEBUG_OBJECT(alloc, "Freeing memory at %p", dmem->data);
    aja_alloc->device->device->DMABufferUnlock((ULWord *)dmem->data,
                                               mem->maxsize);
    AJAMemory::FreeAligned(dmem->data);
  }

  g_slice_free1(sizeof(GstAjaMemory), dmem);
}

static void gst_aja_allocator_finalize(GObject *alloc) {
  GstAjaAllocator *aja_alloc = GST_AJA_ALLOCATOR(alloc);

  GST_DEBUG_OBJECT(alloc, "Freeing allocator");

  gst_aja_device_unref(aja_alloc->device);

  G_OBJECT_CLASS(gst_aja_allocator_parent_class)->finalize(alloc);
}

static void gst_aja_allocator_class_init(GstAjaAllocatorClass *klass) {
  GObjectClass *gobject_class;
  GstAllocatorClass *allocator_class;

  gobject_class = (GObjectClass *)klass;
  allocator_class = (GstAllocatorClass *)klass;

  gobject_class->finalize = gst_aja_allocator_finalize;

  allocator_class->alloc = gst_aja_allocator_alloc;
  allocator_class->free = gst_aja_allocator_free;
}

static void gst_aja_allocator_init(GstAjaAllocator *aja_alloc) {
  GstAllocator *alloc = GST_ALLOCATOR_CAST(aja_alloc);

  alloc->mem_type = GST_AJA_ALLOCATOR_MEMTYPE;
  alloc->mem_map = (GstMemoryMapFunction)_aja_memory_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction)_aja_memory_unmap;
  alloc->mem_copy = (GstMemoryCopyFunction)_aja_memory_copy;
  alloc->mem_share = (GstMemoryShareFunction)_aja_memory_share;
}

GstAllocator *gst_aja_allocator_new(GstAjaDevice *device) {
  GstAjaAllocator *alloc =
      (GstAjaAllocator *)g_object_new(GST_TYPE_AJA_ALLOCATOR, NULL);

  alloc->device = gst_aja_device_ref(device);

  GST_DEBUG_OBJECT(alloc, "Creating allocator for device %d",
                   device->device->GetIndexNumber());

  return GST_ALLOCATOR(alloc);
}

GstAjaDevice *gst_aja_device_obtain(const gchar *device_identifier) {
  CNTV2Device *device = new CNTV2Device();

  if (!CNTV2DeviceScanner::GetFirstDeviceFromArgument(device_identifier,
                                                      *device)) {
    delete device;
    return NULL;
  }

  GstAjaDevice *dev = g_atomic_rc_box_new0(GstAjaDevice);
  dev->device = device;

  return dev;
}

GstAjaDevice *gst_aja_device_ref(GstAjaDevice *device) {
  return (GstAjaDevice *)g_atomic_rc_box_acquire(device);
}

void gst_aja_device_unref(GstAjaDevice *device) {
  g_atomic_rc_box_release_full(device, [](gpointer data) {
    GstAjaDevice *dev = (GstAjaDevice *)data;

    delete dev->device;
  });
}

static gpointer init_setup_mutex(gpointer data) {
  sem_t *s = SEM_FAILED;
  s = sem_open("/gstreamer-aja-sem", O_CREAT, S_IRUSR | S_IWUSR, 1);
  if (s == SEM_FAILED) {
    g_critical("Failed to create SHM semaphore for GStreamer AJA plugin: %s",
               g_strerror(errno));
  }
  return s;
}

static sem_t *get_setup_mutex(void) {
  static GOnce once = G_ONCE_INIT;

  g_once(&once, init_setup_mutex, NULL);

  return (sem_t *)once.retval;
}

ShmMutexLocker::ShmMutexLocker() {
  sem_t *s = get_setup_mutex();
  if (s != SEM_FAILED) sem_wait(s);
}

ShmMutexLocker::~ShmMutexLocker() {
  sem_t *s = get_setup_mutex();
  if (s != SEM_FAILED) sem_post(s);
}

GType gst_aja_audio_system_get_type(void) {
  static gsize id = 0;
  static const GEnumValue modes[] = {
      {GST_AJA_AUDIO_SYSTEM_AUTO, "auto", "Auto (based on selected channel)"},
      {GST_AJA_AUDIO_SYSTEM_1, "1", "Audio system 1"},
      {GST_AJA_AUDIO_SYSTEM_2, "2", "Audio system 2"},
      {GST_AJA_AUDIO_SYSTEM_3, "3", "Audio system 3"},
      {GST_AJA_AUDIO_SYSTEM_4, "4", "Audio system 4"},
      {GST_AJA_AUDIO_SYSTEM_5, "5", "Audio system 5"},
      {GST_AJA_AUDIO_SYSTEM_6, "6", "Audio system 6"},
      {GST_AJA_AUDIO_SYSTEM_7, "7", "Audio system 7"},
      {GST_AJA_AUDIO_SYSTEM_8, "8", "Audio system 8"},
      {0, NULL, NULL}};

  if (g_once_init_enter(&id)) {
    GType tmp = g_enum_register_static("GstAjaAudioSystem", modes);
    g_once_init_leave(&id, tmp);
  }

  return (GType)id;
}

GType gst_aja_output_destination_get_type(void) {
  static gsize id = 0;
  static const GEnumValue modes[] = {
      {GST_AJA_OUTPUT_DESTINATION_AUTO, "auto",
       "Auto (based on selected channel)"},
      {GST_AJA_OUTPUT_DESTINATION_ANALOG, "analog", "Analog Output"},
      {GST_AJA_OUTPUT_DESTINATION_SDI1, "sdi-1", "SDI Output 1"},
      {GST_AJA_OUTPUT_DESTINATION_SDI2, "sdi-2", "SDI Output 2"},
      {GST_AJA_OUTPUT_DESTINATION_SDI3, "sdi-3", "SDI Output 3"},
      {GST_AJA_OUTPUT_DESTINATION_SDI4, "sdi-4", "SDI Output 4"},
      {GST_AJA_OUTPUT_DESTINATION_SDI5, "sdi-5", "SDI Output 5"},
      {GST_AJA_OUTPUT_DESTINATION_SDI6, "sdi-6", "SDI Output 6"},
      {GST_AJA_OUTPUT_DESTINATION_SDI7, "sdi-7", "SDI Output 7"},
      {GST_AJA_OUTPUT_DESTINATION_SDI8, "sdi-8", "SDI Output 8"},
      {GST_AJA_OUTPUT_DESTINATION_HDMI, "hdmi", "HDMI Output"},
      {0, NULL, NULL}};

  if (g_once_init_enter(&id)) {
    GType tmp = g_enum_register_static("GstAjaOutputDestination", modes);
    g_once_init_leave(&id, tmp);
  }

  return (GType)id;
}

GType gst_aja_reference_source_get_type(void) {
  static gsize id = 0;
  static const GEnumValue modes[] = {
      {GST_AJA_REFERENCE_SOURCE_AUTO, "auto", "Auto"},
      {GST_AJA_REFERENCE_SOURCE_FREERUN, "freerun", "Freerun"},
      {GST_AJA_REFERENCE_SOURCE_EXTERNAL, "external", "External"},
      {GST_AJA_REFERENCE_SOURCE_INPUT_1, "input-1", "SDI Input 1"},
      {GST_AJA_REFERENCE_SOURCE_INPUT_2, "input-2", "SDI Input 2"},
      {GST_AJA_REFERENCE_SOURCE_INPUT_3, "input-3", "SDI Input 3"},
      {GST_AJA_REFERENCE_SOURCE_INPUT_4, "input-4", "SDI Input 4"},
      {GST_AJA_REFERENCE_SOURCE_INPUT_5, "input-5", "SDI Input 5"},
      {GST_AJA_REFERENCE_SOURCE_INPUT_6, "input-6", "SDI Input 6"},
      {GST_AJA_REFERENCE_SOURCE_INPUT_7, "input-7", "SDI Input 7"},
      {GST_AJA_REFERENCE_SOURCE_INPUT_8, "input-8", "SDI Input 8"},
      {0, NULL, NULL}};

  if (g_once_init_enter(&id)) {
    GType tmp = g_enum_register_static("GstAjaReferenceSource", modes);
    g_once_init_leave(&id, tmp);
  }

  return (GType)id;
}

GType gst_aja_input_source_get_type(void) {
  static gsize id = 0;
  static const GEnumValue modes[] = {
      {GST_AJA_INPUT_SOURCE_AUTO, "auto", "Auto (based on selected channel)"},
      {GST_AJA_INPUT_SOURCE_ANALOG1, "analog-1", "Analog Input 1"},
      {GST_AJA_INPUT_SOURCE_SDI1, "sdi-1", "SDI Input 1"},
      {GST_AJA_INPUT_SOURCE_SDI2, "sdi-2", "SDI Input 2"},
      {GST_AJA_INPUT_SOURCE_SDI3, "sdi-3", "SDI Input 3"},
      {GST_AJA_INPUT_SOURCE_SDI4, "sdi-4", "SDI Input 4"},
      {GST_AJA_INPUT_SOURCE_SDI5, "sdi-5", "SDI Input 5"},
      {GST_AJA_INPUT_SOURCE_SDI6, "sdi-6", "SDI Input 6"},
      {GST_AJA_INPUT_SOURCE_SDI7, "sdi-7", "SDI Input 7"},
      {GST_AJA_INPUT_SOURCE_SDI8, "sdi-8", "SDI Input 8"},
      {GST_AJA_INPUT_SOURCE_HDMI1, "hdmi-1", "HDMI Input 1"},
      {GST_AJA_INPUT_SOURCE_HDMI2, "hdmi-2", "HDMI Input 2"},
      {GST_AJA_INPUT_SOURCE_HDMI3, "hdmi-3", "HDMI Input 3"},
      {GST_AJA_INPUT_SOURCE_HDMI4, "hdmi-4", "HDMI Input 4"},
      {0, NULL, NULL}};

  if (g_once_init_enter(&id)) {
    GType tmp = g_enum_register_static("GstAjaInputSource", modes);
    g_once_init_leave(&id, tmp);
  }

  return (GType)id;
}

GType gst_aja_sdi_mode_get_type(void) {
  static gsize id = 0;
  static const GEnumValue modes[] = {
      {GST_AJA_SDI_MODE_SINGLE_LINK, "single-link", "Single Link"},
      {GST_AJA_SDI_MODE_QUAD_LINK_SQD, "quad-link-sqd", "Quad Link SQD"},
      {GST_AJA_SDI_MODE_QUAD_LINK_TSI, "quad-link-tsi", "Quad Link TSI"},
      {0, NULL, NULL}};

  if (g_once_init_enter(&id)) {
    GType tmp = g_enum_register_static("GstAjaSdiMode", modes);
    g_once_init_leave(&id, tmp);
  }

  return (GType)id;
}

GType gst_aja_video_format_get_type(void) {
  static gsize id = 0;
  static const GEnumValue modes[] = {
      {GST_AJA_VIDEO_FORMAT_AUTO, "auto", "Auto detect format"},
      {GST_AJA_VIDEO_FORMAT_1080i_5000, "1080i-5000", "1080i 5000"},
      {GST_AJA_VIDEO_FORMAT_1080i_5994, "1080i-5994", "1080i 5994"},
      {GST_AJA_VIDEO_FORMAT_1080i_6000, "1080i-6000", "1080i 6000"},
      {GST_AJA_VIDEO_FORMAT_720p_5994, "720p-5994", "720p 5994"},
      {GST_AJA_VIDEO_FORMAT_720p_6000, "720p-6000", "720p 6000"},
      {GST_AJA_VIDEO_FORMAT_1080p_2997, "1080p-2997", "1080p 2997"},
      {GST_AJA_VIDEO_FORMAT_1080p_3000, "1080p-3000", "1080p 3000"},
      {GST_AJA_VIDEO_FORMAT_1080p_2500, "1080p-2500", "1080p 2500"},
      {GST_AJA_VIDEO_FORMAT_1080p_2398, "1080p-2398", "1080p 2398"},
      {GST_AJA_VIDEO_FORMAT_1080p_2400, "1080p-2400", "1080p 2400"},
      {GST_AJA_VIDEO_FORMAT_720p_5000, "720p-5000", "720p 5000"},
      {GST_AJA_VIDEO_FORMAT_720p_2398, "720p-2398", "720p 2398"},
      {GST_AJA_VIDEO_FORMAT_720p_2500, "720p-2500", "720p 2500"},
      {GST_AJA_VIDEO_FORMAT_1080p_5000_A, "1080p-5000-a", "1080p 5000 A"},
      {GST_AJA_VIDEO_FORMAT_1080p_5994_A, "1080p-5994-a", "1080p 5994 A"},
      {GST_AJA_VIDEO_FORMAT_1080p_6000_A, "1080p-6000-a", "1080p 6000 A"},

      {GST_AJA_VIDEO_FORMAT_625_5000, "625-5000", "625 5000"},
      {GST_AJA_VIDEO_FORMAT_525_5994, "525-5994", "525 5994"},
      {GST_AJA_VIDEO_FORMAT_525_2398, "525-2398", "525 2398"},
      {GST_AJA_VIDEO_FORMAT_525_2400, "525-2400", "525 2400"},

      {GST_AJA_VIDEO_FORMAT_1080p_DCI_2398, "1080p-dci-2398", "1080p DCI 2398"},
      {GST_AJA_VIDEO_FORMAT_1080p_DCI_2400, "1080p-dci-2400", "1080p DCI 2400"},
      {GST_AJA_VIDEO_FORMAT_1080p_DCI_2500, "1080p-dci-2500", "1080p DCI 2500"},
      {GST_AJA_VIDEO_FORMAT_1080p_DCI_2997, "1080p-dci-2997", "1080p DCI 2997"},
      {GST_AJA_VIDEO_FORMAT_1080p_DCI_3000, "1080p-dci-3000", "1080p DCI 3000"},
      {GST_AJA_VIDEO_FORMAT_1080p_DCI_5000_A, "1080p-dci-5000-a",
       "1080p DCI 5000 A"},
      {GST_AJA_VIDEO_FORMAT_1080p_DCI_5994_A, "1080p-dci-5994-a",
       "1080p DCI 5994 A"},
      {GST_AJA_VIDEO_FORMAT_1080p_DCI_6000_A, "1080p-dci-6000-a",
       "1080p DCI 6000 A"},

      {GST_AJA_VIDEO_FORMAT_2160p_2398, "2160p-2398", "2160p 2398"},
      {GST_AJA_VIDEO_FORMAT_2160p_2400, "2160p-2400", "2160p 2400"},
      {GST_AJA_VIDEO_FORMAT_2160p_2500, "2160p-2500", "2160p 2500"},
      {GST_AJA_VIDEO_FORMAT_2160p_2997, "2160p-2997", "2160p 2997"},
      {GST_AJA_VIDEO_FORMAT_2160p_3000, "2160p-3000", "2160p 3000"},
      {GST_AJA_VIDEO_FORMAT_2160p_5000, "2160p-5000", "2160p 5000"},
      {GST_AJA_VIDEO_FORMAT_2160p_5994, "2160p-5994", "2160p 5994"},
      {GST_AJA_VIDEO_FORMAT_2160p_6000, "2160p-6000", "2160p 6000"},

      {GST_AJA_VIDEO_FORMAT_2160p_DCI_2398, "2160p-dci-2398", "2160p DCI 2398"},
      {GST_AJA_VIDEO_FORMAT_2160p_DCI_2400, "2160p-dci-2400", "2160p DCI 2400"},
      {GST_AJA_VIDEO_FORMAT_2160p_DCI_2500, "2160p-dci-2500", "2160p DCI 2500"},
      {GST_AJA_VIDEO_FORMAT_2160p_DCI_2997, "2160p-dci-2997", "2160p DCI 2997"},
      {GST_AJA_VIDEO_FORMAT_2160p_DCI_3000, "2160p-dci-3000", "2160p DCI 3000"},
      {GST_AJA_VIDEO_FORMAT_2160p_DCI_5000, "2160p-dci-5000", "2160p DCI 5000"},
      {GST_AJA_VIDEO_FORMAT_2160p_DCI_5994, "2160p-dci-5994", "2160p DCI 5994"},
      {GST_AJA_VIDEO_FORMAT_2160p_DCI_6000, "2160p-dci-6000", "2160p DCI 6000"},

      {GST_AJA_VIDEO_FORMAT_4320p_2398, "4320p-2398", "4320p 2398"},
      {GST_AJA_VIDEO_FORMAT_4320p_2400, "4320p-2400", "4320p 2400"},
      {GST_AJA_VIDEO_FORMAT_4320p_2500, "4320p-2500", "4320p 2500"},
      {GST_AJA_VIDEO_FORMAT_4320p_2997, "4320p-2997", "4320p 2997"},
      {GST_AJA_VIDEO_FORMAT_4320p_3000, "4320p-3000", "4320p 3000"},
      {GST_AJA_VIDEO_FORMAT_4320p_5000, "4320p-5000", "4320p 5000"},
      {GST_AJA_VIDEO_FORMAT_4320p_5994, "4320p-5994", "4320p 5994"},
      {GST_AJA_VIDEO_FORMAT_4320p_6000, "4320p-6000", "4320p 6000"},

      {GST_AJA_VIDEO_FORMAT_4320p_DCI_2398, "4320p-dci-2398", "4320p DCI 2398"},
      {GST_AJA_VIDEO_FORMAT_4320p_DCI_2400, "4320p-dci-2400", "4320p DCI 2400"},
      {GST_AJA_VIDEO_FORMAT_4320p_DCI_2500, "4320p-dci-2500", "4320p DCI 2500"},
      {GST_AJA_VIDEO_FORMAT_4320p_DCI_2997, "4320p-dci-2997", "4320p DCI 2997"},
      {GST_AJA_VIDEO_FORMAT_4320p_DCI_3000, "4320p-dci-3000", "4320p DCI 3000"},
      {GST_AJA_VIDEO_FORMAT_4320p_DCI_5000, "4320p-dci-5000", "4320p DCI 5000"},
      {GST_AJA_VIDEO_FORMAT_4320p_DCI_5994, "4320p-dci-5994", "4320p DCI 5994"},
      {GST_AJA_VIDEO_FORMAT_4320p_DCI_6000, "4320p-dci-6000", "4320p DCI 6000"},

      {0, NULL, NULL}};

  if (g_once_init_enter(&id)) {
    GType tmp = g_enum_register_static("GstAjaVideoFormat", modes);
    g_once_init_leave(&id, tmp);
  }

  return (GType)id;
}

GType gst_aja_audio_source_get_type(void) {
  static gsize id = 0;
  static const GEnumValue modes[] = {
      {GST_AJA_AUDIO_SOURCE_EMBEDDED, "embedded", "Embedded"},
      {GST_AJA_AUDIO_SOURCE_AES, "aes", "AES"},
      {GST_AJA_AUDIO_SOURCE_ANALOG, "analog", "Analog"},
      {GST_AJA_AUDIO_SOURCE_HDMI, "hdmi", "HDMI"},
      {GST_AJA_AUDIO_SOURCE_MIC, "mic", "Microphone"},
      {0, NULL, NULL}};

  if (g_once_init_enter(&id)) {
    GType tmp = g_enum_register_static("GstAjaAudioSource", modes);
    g_once_init_leave(&id, tmp);
  }

  return (GType)id;
}

GType gst_aja_timecode_index_get_type(void) {
  static gsize id = 0;
  static const GEnumValue modes[] = {
      {GST_AJA_TIMECODE_INDEX_VITC, "vitc", "Embedded SDI VITC"},
      {GST_AJA_TIMECODE_INDEX_ATC_LTC, "atc-ltc", "Embedded SDI ATC LTC"},
      {GST_AJA_TIMECODE_INDEX_LTC1, "ltc-1", "Analog LTC 1"},
      {GST_AJA_TIMECODE_INDEX_LTC2, "ltc-2", "Analog LTC 2"},
      {0, NULL, NULL}};

  if (g_once_init_enter(&id)) {
    GType tmp = g_enum_register_static("GstAjaTimecodeIndex", modes);
    g_once_init_leave(&id, tmp);
  }

  return (GType)id;
}

void gst_aja_common_init(void) {
  GST_DEBUG_CATEGORY_INIT(gst_aja_debug, "aja", 0,
                          "Debug category for AJA plugin");
}
