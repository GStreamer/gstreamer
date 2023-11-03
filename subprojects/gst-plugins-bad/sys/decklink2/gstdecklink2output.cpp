/*
 * GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdecklink2output.h"
#include "gstdecklink2device.h"
#include <gst/base/base.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <new>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <string.h>
#include <string>

GST_DEBUG_CATEGORY_STATIC (gst_decklink2_output_debug);
#define GST_CAT_DEFAULT gst_decklink2_output_debug

class IGstDeckLinkVideoOutputCallback;

class IGstDeckLinkTimecode:public IDeckLinkTimecode
{
public:
  IGstDeckLinkTimecode (GstVideoTimeCode * timecode):ref_count_ (1)
  {
    g_assert (timecode);

    timecode_ = gst_video_time_code_copy (timecode);
  }

  /* IUnknown */
  HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void **object)
  {
    if (riid == IID_IUnknown) {
      *object = static_cast < IUnknown * >(this);
    } else if (riid == IID_IDeckLinkTimecode) {
      *object = static_cast < IDeckLinkTimecode * >(this);
    } else {
      *object = NULL;
      return E_NOINTERFACE;
    }

    AddRef ();

    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef (void)
  {
    ULONG cnt = ref_count_.fetch_add (1);

    return cnt + 1;
  }

  ULONG STDMETHODCALLTYPE Release (void)
  {
    ULONG cnt = ref_count_.fetch_sub (1);
    if (cnt == 1)
      delete this;

    return cnt - 1;
  }

  /* IDeckLinkTimecode */
  BMDTimecodeBCD STDMETHODCALLTYPE GetBCD (void)
  {
    BMDTimecodeBCD bcd = 0;

    guint frames = timecode_->frames;
    if (((double) timecode_->config.fps_n) /
        ((double) timecode_->config.fps_d) > 30.0)
      frames /= 2;

    bcd |= (frames % 10) << 0;
    bcd |= ((frames / 10) & 0x0f) << 4;
    bcd |= (timecode_->seconds % 10) << 8;
    bcd |= ((timecode_->seconds / 10) & 0x0f) << 12;
    bcd |= (timecode_->minutes % 10) << 16;
    bcd |= ((timecode_->minutes / 10) & 0x0f) << 20;
    bcd |= (timecode_->hours % 10) << 24;
    bcd |= ((timecode_->hours / 10) & 0x0f) << 28;

    return bcd;
  }

  HRESULT STDMETHODCALLTYPE
      GetComponents (unsigned char *hours, unsigned char *minutes,
      unsigned char *seconds, unsigned char *frames)
  {
    *hours = timecode_->hours;
    *minutes = timecode_->minutes;
    *seconds = timecode_->seconds;
    *frames = timecode_->frames;
    if (((double) timecode_->config.fps_n) /
        ((double) timecode_->config.fps_d) > 30.0)
      *frames /= 2;

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetString (dlstring_t * timecode)
  {
    gchar *s = gst_video_time_code_to_string (timecode_);
    *timecode = StdToDlString (s);
    g_free (s);

    return S_OK;
  }

  BMDTimecodeFlags STDMETHODCALLTYPE GetFlags (void)
  {
    BMDTimecodeFlags flags = (BMDTimecodeFlags) 0;

    if ((timecode_->config.flags & GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME) != 0)
      flags = (BMDTimecodeFlags) (flags | bmdTimecodeIsDropFrame);
    else
      flags = (BMDTimecodeFlags) (flags | bmdTimecodeFlagDefault);

    if (((double) timecode_->config.fps_n) /
        ((double) timecode_->config.fps_d) > 30.0) {
      if (timecode_->frames % 2 == 1)
        flags = (BMDTimecodeFlags) (flags | bmdTimecodeFieldMark);
    } else if (timecode_->field_count == 2) {
      flags = (BMDTimecodeFlags) (flags | bmdTimecodeFieldMark);
    }

    return flags;
  }

  HRESULT STDMETHODCALLTYPE GetTimecodeUserBits (BMDTimecodeUserBits * userBits)
  {
    *userBits = 0;
    return S_OK;
  }

private:
  virtual ~ IGstDeckLinkTimecode () {
    gst_video_time_code_free (timecode_);
  }

private:
  GstVideoTimeCode * timecode_;
  std::atomic < ULONG > ref_count_;
};

/* DeckLink SDK uses separate IIDs rather than extending the same COM interface,
 * so implement both versions. */
class IGstDeckLinkVideoFrame:public IDeckLinkVideoFrame,
    public IDeckLinkVideoFrameMetadataExtensions,
    public IDeckLinkVideoFrameMetadataExtensions_v11_5
{
public:
  IGstDeckLinkVideoFrame (GstVideoFrame * frame):ref_count_ (1)
  {
    frame_ = *frame;
  }

  /* IUnknown */
  HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void **object)
  {
    if (riid == IID_IUnknown) {
      *object = static_cast < IDeckLinkVideoFrame * >(this);
    } else if (riid == IID_IDeckLinkVideoFrame) {
      *object = static_cast < IDeckLinkVideoFrame * >(this);
    } else if (riid == IID_IDeckLinkVideoFrameMetadataExtensions) {
      *object = static_cast < IDeckLinkVideoFrameMetadataExtensions * >(this);
    } else if (riid == IID_IDeckLinkVideoFrameMetadataExtensions_v11_5) {
      *object =
          static_cast < IDeckLinkVideoFrameMetadataExtensions_v11_5 * >(this);
    } else {
      *object = NULL;
      return E_NOINTERFACE;
    }

    AddRef ();

    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef (void)
  {
    ULONG cnt = ref_count_.fetch_add (1);

    return cnt + 1;
  }

  ULONG STDMETHODCALLTYPE Release (void)
  {
    ULONG cnt = ref_count_.fetch_sub (1);
    if (cnt == 1)
      delete this;

    return cnt - 1;
  }

  /* IDeckLinkVideoFrame */
  long STDMETHODCALLTYPE GetWidth (void)
  {
    return GST_VIDEO_FRAME_WIDTH (&frame_);
  }

  long STDMETHODCALLTYPE GetHeight (void)
  {
    return GST_VIDEO_FRAME_HEIGHT (&frame_);
  }

  long STDMETHODCALLTYPE GetRowBytes (void)
  {
    return GST_VIDEO_FRAME_PLANE_STRIDE (&frame_, 0);
  }

  BMDPixelFormat STDMETHODCALLTYPE GetPixelFormat (void)
  {
    BMDPixelFormat format = bmdFormatUnspecified;
    switch (GST_VIDEO_FRAME_FORMAT (&frame_)) {
      case GST_VIDEO_FORMAT_UYVY:
        format = bmdFormat8BitYUV;
        break;
      case GST_VIDEO_FORMAT_v210:
        format = bmdFormat10BitYUV;
        break;
      case GST_VIDEO_FORMAT_ARGB:
        format = bmdFormat8BitARGB;
        break;
      case GST_VIDEO_FORMAT_BGRA:
        format = bmdFormat8BitBGRA;
        break;
      case GST_VIDEO_FORMAT_r210:
        format = bmdFormat10BitRGB;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    return format;
  }

  BMDFrameFlags STDMETHODCALLTYPE GetFlags (void)
  {
    if (hdr_metadata_.have_mastering_info || hdr_metadata_.have_light_level ||
        frame_.info.colorimetry.transfer == GST_VIDEO_TRANSFER_SMPTE2084 ||
        frame_.info.colorimetry.transfer == GST_VIDEO_TRANSFER_ARIB_STD_B67)
      return bmdFrameContainsHDRMetadata;
    return bmdFrameFlagDefault;
  }

  HRESULT STDMETHODCALLTYPE GetBytes (void **buffer)
  {
    *buffer = GST_VIDEO_FRAME_PLANE_DATA (&frame_, 0);

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
      GetTimecode (BMDTimecodeFormat format, IDeckLinkTimecode ** timecode)
  {
    if (format != timecode_format_)
      return S_FALSE;

    if (timecode_) {
      *timecode = timecode_;
      timecode_->AddRef ();
      return S_OK;
    }

    return S_FALSE;
  }

  HRESULT STDMETHODCALLTYPE
      GetAncillaryData (IDeckLinkVideoFrameAncillary ** ancillary)
  {
    if (ancillary_) {
      *ancillary = ancillary_;
      ancillary_->AddRef ();
      return S_OK;
    }

    return S_FALSE;
  }

  HRESULT STDMETHODCALLTYPE GetInt (BMDDeckLinkFrameMetadataID metadata_id,
      int64_t * value)
  {
    switch (metadata_id) {
      case bmdDeckLinkFrameMetadataColorspace:
      {
        switch (frame_.info.colorimetry.matrix) {
          case GST_VIDEO_COLOR_MATRIX_BT601:
            *value = bmdColorspaceRec601;
            return S_OK;
          case GST_VIDEO_COLOR_MATRIX_BT709:
            *value = bmdColorspaceRec709;
            return S_OK;
          case GST_VIDEO_COLOR_MATRIX_BT2020:
            *value = bmdColorspaceRec2020;
            return S_OK;
          default:
            GST_DEBUG ("no mapping from video color matrix 0x%x to BMD",
                frame_.info.colorimetry.matrix);
            return E_INVALIDARG;
        }
        break;
      }
      case bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc:
        switch (frame_.info.colorimetry.transfer) {
          case GST_VIDEO_TRANSFER_BT601:
          case GST_VIDEO_TRANSFER_BT709:
          case GST_VIDEO_TRANSFER_BT2020_10:
            if (hdr_metadata_.have_mastering_info &&
                hdr_metadata_.have_light_level)
              *value = 1;
            else
              *value = 0;
            return S_OK;
          case GST_VIDEO_TRANSFER_SMPTE2084:
            *value = 2;
            return S_OK;
          case GST_VIDEO_TRANSFER_ARIB_STD_B67:
            *value = 3;
            return S_OK;
          default:
            return E_INVALIDARG;
        }
        break;
      default:
        return E_INVALIDARG;
    }
  }

  HRESULT STDMETHODCALLTYPE GetFloat (BMDDeckLinkFrameMetadataID metadata_id,
      double *value)
  {
    switch (metadata_id) {
      case bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedX:
        if (hdr_metadata_.have_mastering_info) {
          *value = (double)
              hdr_metadata_.mastering_info.display_primaries[0].x / 50000.0;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedY:
        if (hdr_metadata_.have_mastering_info) {
          *value = (double)
              hdr_metadata_.mastering_info.display_primaries[0].y / 50000.0;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenX:
        if (hdr_metadata_.have_mastering_info) {
          *value = (double)
              hdr_metadata_.mastering_info.display_primaries[1].x / 50000.0;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenY:
        if (hdr_metadata_.have_mastering_info) {
          *value = (double)
              hdr_metadata_.mastering_info.display_primaries[1].y / 50000.0;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueX:
        if (hdr_metadata_.have_mastering_info) {
          *value = (double)
              hdr_metadata_.mastering_info.display_primaries[2].x / 50000.0;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueY:
        if (hdr_metadata_.have_mastering_info) {
          *value = (double)
              hdr_metadata_.mastering_info.display_primaries[2].y / 50000.0;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRWhitePointX:
        if (hdr_metadata_.have_mastering_info) {
          *value = (double)
              hdr_metadata_.mastering_info.white_point.x / 50000.0;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRWhitePointY:
        if (hdr_metadata_.have_mastering_info) {
          *value = (double)
              hdr_metadata_.mastering_info.white_point.y / 50000.0;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance:
        if (hdr_metadata_.have_mastering_info) {
          *value = (double)
              hdr_metadata_.mastering_info.max_display_mastering_luminance
              * 65535.0 / 10000.0;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRMinDisplayMasteringLuminance:
        if (hdr_metadata_.have_mastering_info) {
          *value = (double)
              hdr_metadata_.mastering_info.min_display_mastering_luminance
              * 6.5535 / 10000.0;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel:
        if (hdr_metadata_.have_light_level) {
          *value = (double) hdr_metadata_.light_level.max_content_light_level;
        } else {
          *value = 0.0;
        }
        return S_OK;
      case bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel:
        if (hdr_metadata_.have_light_level) {
          *value =
              (double) hdr_metadata_.light_level.max_frame_average_light_level;
        } else {
          *value = 0.0;
        }
        return S_OK;
      default:
        return E_INVALIDARG;
    }
  }

  HRESULT STDMETHODCALLTYPE GetFlag (BMDDeckLinkFrameMetadataID metadata_id,
      dlbool_t * value)
  {
    return E_INVALIDARG;
  }

  HRESULT STDMETHODCALLTYPE GetString (BMDDeckLinkFrameMetadataID metadata_id,
      dlstring_t * value)
  {
    return E_INVALIDARG;
  }

  HRESULT STDMETHODCALLTYPE GetBytes (BMDDeckLinkFrameMetadataID metadata_id,
      void *buffer, uint32_t * buffer_size)
  {
    return E_INVALIDARG;
  }

#ifdef G_OS_WIN32
  /* Metadata ID types are distinct enum types on Windows */
  HRESULT STDMETHODCALLTYPE GetInt (BMDDeckLinkFrameMetadataID_v11_5 id,
      int64_t * value)
  {
    return GetInt ((BMDDeckLinkFrameMetadataID) id, value);
  }

  HRESULT STDMETHODCALLTYPE GetFloat (BMDDeckLinkFrameMetadataID_v11_5 id,
      double *value)
  {
    return GetFloat ((BMDDeckLinkFrameMetadataID) id, value);
  }

  HRESULT STDMETHODCALLTYPE GetFlag (BMDDeckLinkFrameMetadataID_v11_5 id,
      dlbool_t * value)
  {
    return E_INVALIDARG;
  }

  HRESULT STDMETHODCALLTYPE GetString (BMDDeckLinkFrameMetadataID_v11_5 id,
      dlstring_t * value)
  {
    return E_INVALIDARG;
  }
#endif

  void SetHDRMetadata (const GstDeckLink2HDRMetadata * metadata)
  {
    hdr_metadata_ = *metadata;
  }

  /* Non-interface methods */
  HRESULT STDMETHODCALLTYPE SetTimecode (BMDTimecodeFormat format,
      GstVideoTimeCode * timecode)
  {
    GST_DECKLINK2_CLEAR_COM (timecode_);

    timecode_format_ = format == bmdTimecodeRP188Any ?
        bmdTimecodeRP188VITC1 : format;

    if (timecode)
      timecode_ = new IGstDeckLinkTimecode (timecode);

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
      SetAncillaryData (IDeckLinkVideoFrameAncillary * ancillary)
  {
    GST_DECKLINK2_CLEAR_COM (ancillary_);

    ancillary_ = ancillary;
    if (ancillary_)
      ancillary_->AddRef ();

    return S_OK;
  }

  IGstDeckLinkVideoFrame *Clone (void)
  {
    IGstDeckLinkVideoFrame *cloned;
    g_assert (frame_.buffer);
    GstVideoFrame frame;

    if (!gst_video_frame_map (&frame,
            &frame_.info, frame_.buffer, GST_MAP_READ)) {
      return NULL;
    }

    cloned = new IGstDeckLinkVideoFrame (&frame);
    cloned->hdr_metadata_ = hdr_metadata_;
    if (ancillary_)
      cloned->SetAncillaryData (ancillary_);
    if (timecode_) {
      timecode_->AddRef ();
      cloned->timecode_ = timecode_;
      cloned->timecode_format_ = timecode_format_;
    }

    return cloned;
  }

  void SetScheduledPts (GstClockTime pts)
  {
    pts_ = pts;
  }

  GstClockTime GetScheduledPts (void)
  {
    return pts_;
  }

  void SetScheduledHwTime (GstClockTime hw_time)
  {
    hw_time_ = hw_time;
  }

  GstClockTime GetScheduledHwTime (void)
  {
    return hw_time_;
  }

private:
  virtual ~ IGstDeckLinkVideoFrame () {
    gst_video_frame_unmap (&frame_);
    GST_DECKLINK2_CLEAR_COM (timecode_);
    GST_DECKLINK2_CLEAR_COM (ancillary_);
  }

private:
  std::atomic < ULONG > ref_count_;
  GstVideoFrame frame_;
  GstDeckLink2HDRMetadata hdr_metadata_ = { };
  BMDTimecodeFormat timecode_format_ = bmdTimecodeRP188Any;
  IDeckLinkTimecode *timecode_ = NULL;
  IDeckLinkVideoFrameAncillary *ancillary_ = NULL;
  GstClockTime pts_ = GST_CLOCK_TIME_NONE;
  GstClockTime hw_time_ = GST_CLOCK_TIME_NONE;
};

class GstDecklink2OutputAudioBuffer
{
public:
  void Init (const gchar * debug_name, const GstAudioInfo & info)
  {
    info_ = info;
    buffer_.clear ();
    read_offset_ = 0;
    pos_ = 0;
    init_done_ = true;
    if (debug_name)
      debug_name_ = debug_name;
    else
      debug_name_ = "GstDecklink2OutputAudioBuffer";
  }

  void Deinit ()
  {
    buffer_.clear ();
    read_offset_ = 0;
    init_done_ = false;
  }

  guint64 Append (const guint8 * data, size_t size)
  {
    if (!data || size == 0 || !init_done_)
      return 0;

    size_t num_samples = size / info_.bpf;

    GST_LOG_ID (debug_name_.c_str (), "Appending %" G_GSIZE_FORMAT " samples",
        num_samples);

    size_t valid_size = buffer_.size () - read_offset_;
    if (read_offset_ > 0 && read_offset_ >= valid_size) {
      if (valid_size > 0)
        memmove (buffer_.data (), buffer_.data () + read_offset_, valid_size);
      buffer_.resize (valid_size);
      read_offset_ = 0;
    }

    size_t cur_size = buffer_.size ();
    buffer_.resize (cur_size + size);
    memcpy (&buffer_[0] + cur_size, data, size);

    return num_samples;
  }

  guint64 PrependSilence (guint64 num_samples)
  {
    if (!init_done_)
      return 0;

    GST_WARNING_ID (debug_name_.c_str (), "Prepending silence %"
        G_GUINT64_FORMAT " samples", num_samples);

    size_t silence_length_in_bytes = num_samples * info_.bpf;
    if (silence_length_in_bytes == 0)
      return num_samples;

    if (read_offset_ >= silence_length_in_bytes) {
      read_offset_ -= silence_length_in_bytes;
    } else {
      size_t valid_size = buffer_.size () - read_offset_;
      buffer_.resize (valid_size + silence_length_in_bytes);
      if (valid_size > 0) {
        memmove (buffer_.data () + silence_length_in_bytes,
            buffer_.data () + read_offset_, valid_size);
      }
      read_offset_ = 0;
    }

    gst_audio_format_info_fill_silence (info_.finfo,
        buffer_.data () + read_offset_, silence_length_in_bytes);

    return num_samples;
  }

  guint8 *GetSamples (guint & num_samples, guint64 & sample_pos)
  {
    if (!init_done_) {
      num_samples = 0;
      return NULL;
    }

    num_samples = (buffer_.size () - read_offset_) / info_.bpf;
    sample_pos = pos_;

    if (num_samples == 0)
      return NULL;

    return buffer_.data () + read_offset_;
  }

  void Flush (guint samples)
  {
    if (samples == 0 || buffer_.empty ())
      return;

    size_t cur_size = buffer_.size () - read_offset_;
    size_t cur_samples = cur_size / info_.bpf;
    size_t flush_size;
    guint64 flush_samples;
    bool clear_all = false;

    if (cur_samples <= samples) {
      flush_size = cur_size;
      flush_samples = cur_samples;
      clear_all = true;
    } else {
      flush_size = samples * info_.bpf;
      flush_samples = samples;
    }

    pos_ += flush_samples;
    if (clear_all) {
      buffer_.clear ();
      read_offset_ = 0;
    } else {
      read_offset_ += flush_size;
    }
  }

private:
  std::vector < guint8 > buffer_;
  size_t read_offset_ = 0;
  guint64 pos_ = 0;
  GstAudioInfo info_;
  bool init_done_ = false;
  std::string debug_name_;
};

/* *INDENT-OFF* */
struct GstDeckLink2OutputPrivate
{
  std::mutex extern_lock;

  /* We schedule frames from the ScheduledFrameCompleted() callback on underrun,
   * which might re-enter ScheduledFrameCompleted(), so recursive locking is
   * safer */
  std::recursive_mutex schedule_lock;
  std::condition_variable cond;
  GstDecklink2OutputAudioBuffer audio_buf;
  std::atomic<bool> api_failed = { false };
  std::atomic<bool> completion_dropped = { false };
};
/* *INDENT-ON* */

struct GstDeckLink2VancPacket
{
  guint line_number;
  guint8 DID, SDID;
  guint8 data_count;
  guint8 data[256];
};

struct _GstDeckLink2Output
{
  GstObject parent;

  GstDeckLink2OutputPrivate *priv;

  GstDeckLink2APILevel api_level;

  IDeckLink *device;
  IDeckLinkProfileAttributes *attr;
  IDeckLinkAttributes_v10_11 *attr_10_11;
  GstDeckLink2ColorCapabilities color_caps;
  IDeckLinkConfiguration *config;
  IDeckLinkConfiguration_v10_11 *config_10_11;
  IDeckLinkKeyer *keyer;

  IDeckLinkOutput *output;
  IDeckLinkOutput_v11_4 *output_11_4;
  IDeckLinkOutput_v10_11 *output_10_11;

  IGstDeckLinkVideoOutputCallback *callback;
  IGstDeckLinkVideoFrame *last_frame;

  GstCaps *caps;
  GArray *format_table;
  guint max_audio_channels;

  GstDeckLink2DisplayMode selected_mode;
  GstAudioInfo audio_info;

  GstVideoVBIEncoder *vbi_enc;
  gint vbi_width;
  guint16 cdp_hdr_sequence_cntr;
  guint n_prerolled;
  guint64 n_frames;
  guint64 n_samples;
  guint n_preroll_frames;
  guint min_buffered;
  guint max_buffered;
  guint gap_frames;
  GstClockTime pts;
  BMDTimeValue hw_time;
  gboolean duplicating;

  gboolean configured;
  gboolean prerolled;
  guint64 late_count;
  guint64 drop_count;
  guint64 underrun_count;
  guint64 overrun_count;
  guint64 duplicate_count;
  guint64 dropped_sample_count;
  guint64 silent_sample_count;

  GArray *vanc_cache;
};

static void gst_decklink2_output_dispose (GObject * object);
static void gst_decklink2_output_finalize (GObject * object);
static void gst_decklink2_output_on_stopped (GstDeckLink2Output * self);
static void gst_decklink2_output_on_completed (GstDeckLink2Output * self,
    BMDOutputFrameCompletionResult result);
static HRESULT
gst_decklink2_output_get_completion_timestamp (GstDeckLink2Output * self,
    IDeckLinkVideoFrame * frame, BMDTimeScale scale, BMDTimeValue * timestamp);

#define gst_decklink2_output_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstDeckLink2Output, gst_decklink2_output,
    GST_TYPE_OBJECT);

class IGstDeckLinkVideoOutputCallback:public IDeckLinkVideoOutputCallback
{
public:
  IGstDeckLinkVideoOutputCallback (GstDeckLink2Output * output)
  :ref_count_ (1), output_ (output)
  {
  }

  /* IUnknown */
  HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void **object)
  {
    if (riid == IID_IUnknown) {
      *object = static_cast < IUnknown * >(this);
    } else if (riid == IID_IDeckLinkVideoOutputCallback) {
      *object = static_cast < IDeckLinkVideoOutputCallback * >(this);
    } else {
      *object = NULL;
      return E_NOINTERFACE;
    }

    AddRef ();

    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef (void)
  {
    ULONG cnt = ref_count_.fetch_add (1);

    return cnt + 1;
  }

  ULONG STDMETHODCALLTYPE Release (void)
  {
    ULONG cnt = ref_count_.fetch_sub (1);
    if (cnt == 1)
      delete this;

    return cnt - 1;
  }

  /* IDeckLinkVideoOutputCallback */
  HRESULT STDMETHODCALLTYPE
      ScheduledFrameCompleted (IDeckLinkVideoFrame * frame,
      BMDOutputFrameCompletionResult result)
  {
    IGstDeckLinkVideoFrame *gst_frame = (IGstDeckLinkVideoFrame *) frame;
    BMDTimeValue timestamp = GST_CLOCK_TIME_NONE;
    GstClockTime pts, hw_pts;

    pts = gst_frame->GetScheduledPts ();
    hw_pts = gst_frame->GetScheduledHwTime ();
    gst_decklink2_output_get_completion_timestamp (output_,
        frame, GST_SECOND, &timestamp);

    switch (result) {
      case bmdOutputFrameCompleted:
        GST_LOG_OBJECT (output_, "Frame %p completed timestamp %"
            GST_TIME_FORMAT ", scheduled %" GST_TIME_FORMAT
            " (gst pts %" GST_TIME_FORMAT ")", frame,
            GST_TIME_ARGS (timestamp), GST_TIME_ARGS (hw_pts),
            GST_TIME_ARGS (pts));
        break;
      case bmdOutputFrameDisplayedLate:
        GST_LOG_OBJECT (output_, "Frame %p late, completed timestamp %"
            GST_TIME_FORMAT ", scheduled %" GST_TIME_FORMAT
            " (gst pts %" GST_TIME_FORMAT ")", frame,
            GST_TIME_ARGS (timestamp), GST_TIME_ARGS (hw_pts),
            GST_TIME_ARGS (pts));
        break;
      case bmdOutputFrameDropped:
        GST_WARNING_OBJECT (output_, "Frame %p dropped, scheduled %"
            GST_TIME_FORMAT, frame, GST_TIME_ARGS (pts));
        break;
      case bmdOutputFrameFlushed:
        GST_LOG_OBJECT (output_, "Flushed Frame %p", frame);
        break;
      default:
        GST_WARNING_OBJECT (output_, "Unknown Frame %p: %d",
            frame, (gint) result);
        break;
    }

    if (result == bmdOutputFrameCompleted ||
        result == bmdOutputFrameDisplayedLate ||
        result == bmdOutputFrameDropped) {
      gst_decklink2_output_on_completed (output_, result);
    }

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE ScheduledPlaybackHasStopped (void)
  {
    gst_decklink2_output_on_stopped (output_);
    return S_OK;
  }

private:
  virtual ~ IGstDeckLinkVideoOutputCallback () {
  }

private:
  std::atomic < ULONG > ref_count_;
  GstDeckLink2Output *output_;
};

static void
gst_decklink2_output_class_init (GstDeckLink2OutputClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_decklink2_output_dispose;
  object_class->finalize = gst_decklink2_output_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_decklink2_output_debug, "decklink2output",
      0, "decklink2output");
}

static void
gst_decklink2_output_init (GstDeckLink2Output * self)
{
  auto priv = (GstDeckLink2OutputPrivate *)
      gst_decklink2_output_get_instance_private (self);
  self->priv = new (priv) GstDeckLink2OutputPrivate ();
  self->format_table = g_array_new (FALSE,
      FALSE, sizeof (GstDeckLink2DisplayMode));
  self->callback = new IGstDeckLinkVideoOutputCallback (self);
  self->vanc_cache =
      g_array_new (FALSE, FALSE, sizeof (GstDeckLink2VancPacket));
}

static void
gst_decklink2_output_dispose (GObject * object)
{
  GstDeckLink2Output *self = GST_DECKLINK2_OUTPUT (object);

  gst_clear_caps (&self->caps);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_decklink2_output_finalize (GObject * object)
{
  GstDeckLink2Output *self = GST_DECKLINK2_OUTPUT (object);

  GST_DECKLINK2_CLEAR_COM (self->output);
  GST_DECKLINK2_CLEAR_COM (self->output_11_4);
  GST_DECKLINK2_CLEAR_COM (self->output_10_11);
  GST_DECKLINK2_CLEAR_COM (self->keyer);
  GST_DECKLINK2_CLEAR_COM (self->attr);
  GST_DECKLINK2_CLEAR_COM (self->attr_10_11);
  GST_DECKLINK2_CLEAR_COM (self->config);
  GST_DECKLINK2_CLEAR_COM (self->config_10_11);
  GST_DECKLINK2_CLEAR_COM (self->device);
  GST_DECKLINK2_CLEAR_COM (self->callback);

  g_array_unref (self->format_table);
  g_clear_pointer (&self->vbi_enc, gst_video_vbi_encoder_free);
  g_array_unref (self->vanc_cache);

  self->priv->~GstDeckLink2OutputPrivate ();

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Returns true if format is supported without conversion */
static gboolean
gst_decklink2_output_does_support_video_mode (GstDeckLink2Output * self,
    BMDDisplayMode mode, BMDPixelFormat format)
{
  HRESULT hr;
  BMDDisplayModeSupport_v10_11 supported_v10_11 =
      bmdDisplayModeNotSupported_v10_11;
  dlbool_t supported = (dlbool_t) 0;
  BMDDisplayMode actual_mode = mode;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->DoesSupportVideoMode (mode, format,
          bmdVideoOutputFlagDefault, &supported_v10_11, NULL);
      if (hr != S_OK || supported_v10_11 != bmdDisplayModeSupported_v10_11)
        return FALSE;
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->
          output_11_4->DoesSupportVideoMode (bmdVideoConnectionUnspecified,
          mode, format, bmdSupportedVideoModeDefault, &actual_mode, &supported);
      if (hr != S_OK || !supported || actual_mode != mode)
        return FALSE;
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->DoesSupportVideoMode (bmdVideoConnectionUnspecified,
          mode, format, bmdNoVideoOutputConversion,
          bmdSupportedVideoModeDefault, &actual_mode, &supported);
      if (hr != S_OK || !supported || actual_mode != mode)
        return FALSE;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

static IDeckLinkDisplayModeIterator *
gst_decklink2_output_get_iterator (GstDeckLink2Output * self)
{
  IDeckLinkDisplayModeIterator *iter = NULL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      self->output_10_11->GetDisplayModeIterator (&iter);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      self->output_11_4->GetDisplayModeIterator (&iter);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      self->output->GetDisplayModeIterator (&iter);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return iter;
}

GstDeckLink2Output *
gst_decklink2_output_new (IDeckLink * device, GstDeckLink2APILevel api_level)
{
  GstDeckLink2Output *self;
  IDeckLinkDisplayModeIterator *iter = NULL;
  HRESULT hr;

  if (!device || api_level == GST_DECKLINK2_API_LEVEL_UNKNOWN)
    return NULL;

  self = (GstDeckLink2Output *) g_object_new (GST_TYPE_DECKLINK2_OUTPUT, NULL);
  self->api_level = api_level;
  self->device = device;
  device->AddRef ();
  device->QueryInterface (IID_IDeckLinkKeyer, (void **) &self->keyer);

  if (api_level == GST_DECKLINK2_API_LEVEL_10_11) {
    device->QueryInterface (IID_IDeckLinkAttributes_v10_11,
        (void **) &self->attr_10_11);
  } else {
    device->QueryInterface (IID_IDeckLinkProfileAttributes,
        (void **) &self->attr);
  }

  self->max_audio_channels = 2;
  gint64 max_audio_channels = 2;
  if (self->attr) {
    hr = self->attr->GetInt (BMDDeckLinkMaximumAudioChannels,
        &max_audio_channels);
    if (gst_decklink2_result (hr))
      self->max_audio_channels = (guint) max_audio_channels;
  } else if (self->attr_10_11) {
    hr = self->attr_10_11->GetInt (BMDDeckLinkMaximumAudioChannels,
        &max_audio_channels);
    if (gst_decklink2_result (hr))
      self->max_audio_channels = (guint) max_audio_channels;
  }

  if (api_level == GST_DECKLINK2_API_LEVEL_10_11) {
    device->QueryInterface (IID_IDeckLinkConfiguration_v10_11,
        (void **) &self->config_10_11);
  } else {
    device->QueryInterface (IID_IDeckLinkConfiguration,
        (void **) &self->config);
  }

  switch (api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = device->QueryInterface (IID_IDeckLinkOutput_v10_11,
          (void **) &self->output_10_11);
      if (!gst_decklink2_result (hr)) {
        gst_object_unref (self);
        return NULL;
      }
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = device->QueryInterface (IID_IDeckLinkOutput_v11_4,
          (void **) &self->output_11_4);
      if (!gst_decklink2_result (hr)) {
        gst_object_unref (self);
        return NULL;
      }

      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = device->QueryInterface (IID_IDeckLinkOutput,
          (void **) &self->output);
      if (!gst_decklink2_result (hr)) {
        gst_object_unref (self);
        return NULL;
      }
      break;
    default:
      g_assert_not_reached ();
      gst_object_unref (self);
      return NULL;
  }

  iter = gst_decklink2_output_get_iterator (self);
  if (!iter) {
    gst_object_unref (self);
    return NULL;
  }

  gst_decklink2_get_color_capabilities (device, &self->color_caps);

  self->caps = gst_decklink2_build_template_caps (GST_OBJECT (self), iter,
      (GstDeckLink2DoesSupportVideoMode)
      gst_decklink2_output_does_support_video_mode, self->format_table,
      &self->color_caps);
  iter->Release ();

  if (!self->caps)
    gst_clear_object (&self);

  return self;
}

GstCaps *
gst_decklink2_output_get_caps (GstDeckLink2Output * output, BMDDisplayMode mode,
    BMDPixelFormat format)
{
  IDeckLinkDisplayModeIterator *iter = NULL;
  GstCaps *caps;

  if (mode == bmdModeUnknown && format == bmdFormatUnspecified)
    return gst_caps_ref (output->caps);

  iter = gst_decklink2_output_get_iterator (output);
  if (!iter)
    return NULL;

  caps = gst_decklink2_build_caps (GST_OBJECT (output), iter,
      mode, format, (GstDeckLink2DoesSupportVideoMode)
      gst_decklink2_output_does_support_video_mode, &output->color_caps);
  iter->Release ();

  return caps;
}

gboolean
gst_decklink2_output_get_display_mode (GstDeckLink2Output * output,
    const GstVideoInfo * info, GstDeckLink2DisplayMode * display_mode)
{
  for (guint i = 0; i < output->format_table->len; i++) {
    const GstDeckLink2DisplayMode *m = &g_array_index (output->format_table,
        GstDeckLink2DisplayMode, i);

    if (m->width == info->width && m->height == info->height &&
        m->fps_n == info->fps_n && m->fps_d == info->fps_d &&
        m->par_n == info->par_n && m->par_d == info->par_d) {
      if ((m->interlaced && !GST_VIDEO_INFO_IS_INTERLACED (info)) ||
          (!m->interlaced && GST_VIDEO_INFO_IS_INTERLACED (info))) {
        continue;
      }

      *display_mode = *m;
      return TRUE;
    }
  }

  return FALSE;
}

guint
gst_decklink2_output_get_max_audio_channels (GstDeckLink2Output * output)
{
  return output->max_audio_channels;
}

static HRESULT
gst_decklink2_output_enable_video (GstDeckLink2Output * self,
    BMDDisplayMode mode, BMDVideoOutputFlags flags)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->EnableVideoOutput (mode, flags);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->EnableVideoOutput (mode, flags);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->EnableVideoOutput (mode, flags);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_enable_audio (GstDeckLink2Output * self,
    BMDAudioSampleRate rate, BMDAudioSampleType sample_type, guint channels,
    BMDAudioOutputStreamType stream_type)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->EnableAudioOutput (rate,
          sample_type, channels, stream_type);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->EnableAudioOutput (rate,
          sample_type, channels, stream_type);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->EnableAudioOutput (rate,
          sample_type, channels, stream_type);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_disable_video (GstDeckLink2Output * self)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->DisableVideoOutput ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->DisableVideoOutput ();
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->DisableVideoOutput ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_disable_audio (GstDeckLink2Output * self)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->DisableAudioOutput ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->DisableAudioOutput ();
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->DisableAudioOutput ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_begin_audio_preroll (GstDeckLink2Output * self)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->BeginAudioPreroll ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->BeginAudioPreroll ();
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->BeginAudioPreroll ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_end_audio_preroll (GstDeckLink2Output * self)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->EndAudioPreroll ();
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->EndAudioPreroll ();
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->EndAudioPreroll ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_schedule_audio_samples (GstDeckLink2Output * self,
    void *buffer, guint count, BMDTimeValue stream_time,
    BMDTimeScale scale, guint * written)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->ScheduleAudioSamples (buffer, count, stream_time,
          scale, written);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->ScheduleAudioSamples (buffer, count, stream_time,
          scale, written);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->ScheduleAudioSamples (buffer, count, stream_time,
          scale, written);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_set_video_callback (GstDeckLink2Output * self,
    IDeckLinkVideoOutputCallback * callback)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->SetScheduledFrameCompletionCallback (callback);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->SetScheduledFrameCompletionCallback (callback);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->SetScheduledFrameCompletionCallback (callback);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_create_ancillary_data (GstDeckLink2Output * self,
    BMDPixelFormat format, IDeckLinkVideoFrameAncillary ** frame)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->CreateAncillaryData (format, frame);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->CreateAncillaryData (format, frame);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->CreateAncillaryData (format, frame);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_is_running (GstDeckLink2Output * self, dlbool_t * active)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->IsScheduledPlaybackRunning (active);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->IsScheduledPlaybackRunning (active);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->IsScheduledPlaybackRunning (active);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_get_num_buffered (GstDeckLink2Output * self,
    guint32 * count)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->GetBufferedVideoFrameCount (count);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->GetBufferedVideoFrameCount (count);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->GetBufferedVideoFrameCount (count);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_get_num_buffered_audio (GstDeckLink2Output * self,
    guint32 * count)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->GetBufferedAudioSampleFrameCount (count);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->GetBufferedAudioSampleFrameCount (count);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->GetBufferedAudioSampleFrameCount (count);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_get_reference_clock (GstDeckLink2Output * self,
    BMDTimeScale scale, BMDTimeValue * hw_time, BMDTimeValue * time_in_frame,
    BMDTimeValue * ticks_per_frame)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->GetHardwareReferenceClock (scale,
          hw_time, time_in_frame, ticks_per_frame);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->GetHardwareReferenceClock (scale,
          hw_time, time_in_frame, ticks_per_frame);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->GetHardwareReferenceClock (scale,
          hw_time, time_in_frame, ticks_per_frame);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_get_completion_timestamp (GstDeckLink2Output * self,
    IDeckLinkVideoFrame * frame, BMDTimeScale scale, BMDTimeValue * timestamp)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->GetFrameCompletionReferenceTimestamp (frame,
          scale, timestamp);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->GetFrameCompletionReferenceTimestamp (frame,
          scale, timestamp);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->GetFrameCompletionReferenceTimestamp (frame,
          scale, timestamp);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_output_start (GstDeckLink2Output * self,
    BMDTimeValue start_time, BMDTimeScale scale, double speed)
{
  HRESULT hr = E_FAIL;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->StartScheduledPlayback (start_time,
          scale, speed);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->StartScheduledPlayback (start_time, scale, speed);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->StartScheduledPlayback (start_time, scale, speed);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return hr;
}

static HRESULT
gst_decklink2_get_current_level (GstDeckLink2Output * self,
    guint * buffered_video, GstClockTime * video_running_time,
    guint * buffered_audio, GstClockTime * audio_running_time,
    GstClockTimeDiff * av_diff, GstClockTime * hw_time,
    GstClockTime * hw_now_gst)
{
  BMDTimeValue hw_now = 0;
  BMDTimeValue dummy, dummy2;
  HRESULT hr = S_OK;

  *buffered_video = 0;
  *buffered_audio = 0;
  *video_running_time = self->pts;
  *audio_running_time = GST_CLOCK_TIME_NONE;
  *av_diff = GST_CLOCK_STIME_NONE;
  *hw_time = GST_CLOCK_TIME_NONE;

  hr = gst_decklink2_output_get_num_buffered (self, buffered_video);
  if (!gst_decklink2_result (hr))
    return hr;

  if (self->n_samples > 0 && GST_AUDIO_INFO_IS_VALID (&self->audio_info)) {
    hr = gst_decklink2_output_get_num_buffered_audio (self, buffered_audio);
    if (!gst_decklink2_result (hr))
      return hr;

    *audio_running_time = gst_util_uint64_scale (self->n_samples, GST_SECOND,
        self->audio_info.rate);
    *av_diff = GST_CLOCK_DIFF (self->pts, *audio_running_time);
  }

  if (self->prerolled) {
    hr = gst_decklink2_output_get_reference_clock (self, GST_SECOND, &hw_now,
        &dummy, &dummy2);
    if (!gst_decklink2_result (hr))
      return hr;

    if ((GstClockTime) self->hw_time == GST_CLOCK_TIME_NONE)
      self->hw_time = hw_now;
    *hw_time = hw_now - self->hw_time;
  }

  if (hw_now_gst)
    *hw_now_gst = (GstClockTime) hw_now;

  return S_OK;
}

static HRESULT
gst_decklink2_output_schedule_video_internal (GstDeckLink2Output * self,
    IGstDeckLinkVideoFrame * frame)
{
  GstDeckLink2OutputPrivate *priv = self->priv;
  guint64 audio_pos = 0;
  HRESULT hr = E_FAIL;
  guint num_samples = 0;
  guint num_written = 0;
  guint8 *audio_buf = nullptr;
  guint buffered_video = 0;
  guint buffered_audio = 0;
  GstClockTime next_pts = GST_CLOCK_TIME_NONE;
  GstClockTime dur = GST_CLOCK_TIME_NONE;
  GstClockTime video_running_time = GST_CLOCK_TIME_NONE;
  GstClockTime audio_running_time = GST_CLOCK_TIME_NONE;
  GstClockTime hw_time_gst = GST_CLOCK_TIME_NONE;
  GstClockTime hw_now_gst = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff diff = GST_CLOCK_STIME_NONE;

  hr = gst_decklink2_get_current_level (self, &buffered_video,
      &video_running_time, &buffered_audio, &audio_running_time, &diff,
      &hw_time_gst, &hw_now_gst);
  if (gst_decklink2_result (hr)) {
    GST_LOG_OBJECT (self, "Before schedule, video %" GST_TIME_FORMAT
        " (%" G_GUINT64_FORMAT ", buffered %u) audio %" GST_TIME_FORMAT
        " (%" G_GUINT64_FORMAT ", buffered %u), av-diff: %" GST_STIME_FORMAT
        ", hw-time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (self->pts), self->n_frames, buffered_video,
        GST_TIME_ARGS (audio_running_time), self->n_samples, buffered_audio,
        GST_STIME_ARGS (diff), GST_TIME_ARGS (hw_time_gst));
  }

  frame->AddRef ();
  GST_DECKLINK2_CLEAR_COM (self->last_frame);
  self->last_frame = frame;

  frame->SetScheduledPts (self->pts);
  frame->SetScheduledHwTime (hw_now_gst);

  next_pts = gst_util_uint64_scale (self->n_frames + 1,
      self->selected_mode.fps_d * GST_SECOND, self->selected_mode.fps_n);
  dur = next_pts - self->pts;

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->ScheduleVideoFrame (frame,
          self->pts, dur, GST_SECOND);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->ScheduleVideoFrame (frame,
          self->pts, dur, GST_SECOND);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->ScheduleVideoFrame (frame, self->pts, dur, GST_SECOND);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (!gst_decklink2_result (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't schedule video frame, hr: 0x%x",
        (guint) hr);
    return hr;
  }

  /* Video is already scheduled, update output state before scheduling audio */
  self->n_frames++;
  self->pts = next_pts;

  audio_buf = priv->audio_buf.GetSamples (num_samples, audio_pos);
  self->n_samples += num_samples;

  if (!self->prerolled) {
    if (self->n_prerolled == 0 && GST_AUDIO_INFO_IS_VALID (&self->audio_info)) {
      GST_DEBUG_OBJECT (self, "Begin audio preroll");

      hr = gst_decklink2_output_begin_audio_preroll (self);
      if (!gst_decklink2_result (hr)) {
        GST_ERROR_OBJECT (self, "Couldn't start audio preroll, hr: 0x%x",
            (guint) hr);
        return hr;
      }
    }

    while (audio_buf && num_samples > 0) {
      num_written = 0;
      hr = gst_decklink2_output_schedule_audio_samples (self,
          audio_buf, num_samples, audio_pos, self->audio_info.rate,
          &num_written);

      if (!gst_decklink2_result (hr)) {
        GST_ERROR_OBJECT (self, "Couldn't schedule audio sample, hr: 0x%x",
            (guint) hr);
        return hr;
      }

      priv->audio_buf.Flush (num_written);
      audio_buf = priv->audio_buf.GetSamples (num_samples, audio_pos);
    };

    self->n_prerolled++;
    if (self->n_prerolled >= self->n_preroll_frames) {
      if (GST_AUDIO_INFO_IS_VALID (&self->audio_info)) {
        hr = gst_decklink2_output_end_audio_preroll (self);

        if (!gst_decklink2_result (hr)) {
          GST_ERROR_OBJECT (self, "Audio preroll failed, hr: 0x%x", (guint) hr);
          return hr;
        }
      }

      hr = gst_decklink2_output_start (self, 0, GST_SECOND, 1.0);
      if (!gst_decklink2_result (hr)) {
        GST_ERROR_OBJECT (self, "Couldn't start playback, hr: 0x%x",
            (guint) hr);
        return hr;
      }

      GST_DEBUG_OBJECT (self, "Prerolled, start playback");

      self->prerolled = TRUE;
    }
  } else {
    while (audio_buf && num_samples > 0) {
      num_written = 0;
      hr = gst_decklink2_output_schedule_audio_samples (self,
          audio_buf, num_samples, audio_pos, self->audio_info.rate,
          &num_written);

      if (!gst_decklink2_result (hr)) {
        GST_ERROR_OBJECT (self, "Couldn't schedule audio sample, hr: 0x%x",
            (guint) hr);
        return hr;
      }

      priv->audio_buf.Flush (num_written);
      audio_buf = priv->audio_buf.GetSamples (num_samples, audio_pos);
    };
  }

  return S_OK;
}

GstFlowReturn
gst_decklink2_output_schedule_stream (GstDeckLink2Output * output,
    IDeckLinkVideoFrame * frame, guint8 * audio_buf, gsize audio_buf_size,
    GstDecklink2OutputStats * stats)
{
  GstDeckLink2OutputPrivate *priv = output->priv;
  HRESULT hr;
  std::unique_lock < std::mutex > lk (priv->extern_lock);
  dlbool_t active;

  g_assert (output->configured);

  if (priv->api_failed) {
    GST_WARNING_OBJECT (output, "Previous API call got failed");
    return GST_FLOW_ERROR;
  }

  hr = gst_decklink2_output_is_running (output, &active);
  if (!gst_decklink2_result (hr)) {
    GST_ERROR_OBJECT (output, "Couldn't query active state, hr: 0x%x",
        (guint) hr);
    return GST_FLOW_ERROR;
  }

  if (active) {
    std::lock_guard < std::recursive_mutex > slk (priv->schedule_lock);
    guint buffered_video = 0;
    guint buffered_audio = 0;
    GstClockTime video_running_time = GST_CLOCK_TIME_NONE;
    GstClockTime audio_running_time = GST_CLOCK_TIME_NONE;
    GstClockTime hw_time_gst = GST_CLOCK_TIME_NONE;
    GstClockTimeDiff diff = GST_CLOCK_STIME_NONE;

    hr = gst_decklink2_get_current_level (output, &buffered_video,
        &video_running_time, &buffered_audio, &audio_running_time, &diff,
        &hw_time_gst, NULL);

    if (!gst_decklink2_result (hr)) {
      GST_ERROR_OBJECT (output,
          "Couldn't query bufferred frame count, hr: 0x%x", (guint) hr);
      return GST_FLOW_ERROR;
    }

    stats->buffered_video = buffered_video;
    stats->buffered_audio = buffered_audio;
    stats->video_running_time = video_running_time;
    stats->audio_running_time = audio_running_time;
    stats->buffered_video_time = gst_util_uint64_scale (buffered_video,
        output->selected_mode.fps_d * GST_SECOND, output->selected_mode.fps_n);
    stats->buffered_audio_time = 0;
    if (GST_AUDIO_INFO_IS_VALID (&output->audio_info)) {
      stats->buffered_audio_time = gst_util_uint64_scale (buffered_audio,
          GST_SECOND, output->audio_info.rate);
    }
    stats->hw_time = hw_time_gst;
    stats->scheduled_video_frames = output->n_frames;
    stats->scheduled_audio_samples = output->n_samples;
    stats->late_count = output->late_count;
    stats->drop_count = output->drop_count;
    stats->overrun_count = output->overrun_count;
    stats->underrun_count = output->underrun_count;
    stats->duplicate_count = output->duplicate_count;
    stats->silent_sample_count = output->silent_sample_count;
    stats->dropped_sample_count = output->dropped_sample_count;

    if (buffered_video > output->max_buffered) {
      GST_WARNING_OBJECT (output, "Skipping frame, video %" GST_TIME_FORMAT
          " (%" G_GUINT64_FORMAT ", buffered %u) audio %" GST_TIME_FORMAT
          " (%" G_GUINT64_FORMAT ", buffered %u), av-diff: %" GST_STIME_FORMAT
          ", hw-time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (output->pts), output->n_frames, buffered_video,
          GST_TIME_ARGS (audio_running_time), output->n_samples, buffered_audio,
          GST_STIME_ARGS (diff), GST_TIME_ARGS (hw_time_gst));
      output->overrun_count++;
      if (audio_buf_size > 0 && GST_AUDIO_INFO_IS_VALID (&output->audio_info)) {
        gsize dropped_samples = audio_buf_size / output->audio_info.bpf;
        output->dropped_sample_count += dropped_samples;
      }

      return GST_BASE_SINK_FLOW_DROPPED;
    }
  }
  lk.unlock ();

  std::lock_guard < std::recursive_mutex > slk (priv->schedule_lock);
  priv->audio_buf.Append (audio_buf, audio_buf_size);

  hr = gst_decklink2_output_schedule_video_internal (output,
      (IGstDeckLinkVideoFrame *) frame);
  if (!gst_decklink2_result (hr))
    return GST_FLOW_ERROR;

  if (priv->completion_dropped.exchange (false))
    return GST_BASE_SINK_FLOW_DROPPED;

  return GST_FLOW_OK;
}

static HRESULT
gst_decklink2_output_stop_internal (GstDeckLink2Output * self)
{
  GstDeckLink2OutputPrivate *priv = self->priv;
  HRESULT hr = E_FAIL;

  GST_DEBUG_OBJECT (self, "Stopping");

  std::unique_lock < std::recursive_mutex > lk (priv->schedule_lock);
  /* Steal last frame to avoid re-rendering */
  GST_DECKLINK2_CLEAR_COM (self->last_frame);
  lk.unlock ();

  switch (self->api_level) {
    case GST_DECKLINK2_API_LEVEL_10_11:
      hr = self->output_10_11->StopScheduledPlayback (0, NULL, 0);
      break;
    case GST_DECKLINK2_API_LEVEL_11_4:
    case GST_DECKLINK2_API_LEVEL_11_5_1:
      hr = self->output_11_4->StopScheduledPlayback (0, NULL, 0);
      break;
    case GST_DECKLINK2_API_LEVEL_LATEST:
      hr = self->output->StopScheduledPlayback (0, NULL, 0);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  GST_DEBUG_OBJECT (self, "StopScheduledPlayback result 0x%x", (guint) hr);

  gst_decklink2_output_disable_audio (self);
  gst_decklink2_output_disable_video (self);
  gst_decklink2_output_set_video_callback (self, NULL);
  lk.lock ();

  priv->api_failed = false;
  priv->completion_dropped = false;
  self->configured = FALSE;
  self->prerolled = FALSE;
  self->late_count = 0;
  self->drop_count = 0;
  self->underrun_count = 0;
  self->overrun_count = 0;
  self->duplicate_count = 0;
  self->dropped_sample_count = 0;
  self->silent_sample_count = 0;

  return hr;
}

HRESULT
gst_decklink2_output_stop (GstDeckLink2Output * output)
{
  GstDeckLink2OutputPrivate *priv = output->priv;
  std::lock_guard < std::mutex > lk (priv->extern_lock);

  return gst_decklink2_output_stop_internal (output);
}

/* Copied from ext/closedcaption/gstccconverter.c */
/* Converts raw CEA708 cc_data and an optional timecode into CDP */
static guint
convert_cea708_cc_data_cea708_cdp_internal (GstDeckLink2Output * self,
    const guint8 * cc_data, guint cc_data_len, guint8 * cdp, guint cdp_len,
    const GstVideoTimeCodeMeta * tc_meta)
{
  GstByteWriter bw;
  guint8 flags, checksum;
  guint i, len;
  const GstDeckLink2DisplayMode *mode = &self->selected_mode;

  gst_byte_writer_init_with_data (&bw, cdp, cdp_len, FALSE);
  gst_byte_writer_put_uint16_be_unchecked (&bw, 0x9669);
  /* Write a length of 0 for now */
  gst_byte_writer_put_uint8_unchecked (&bw, 0);
  if (mode->fps_n == 24000 && mode->fps_d == 1001) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x1f);
  } else if (mode->fps_n == 24 && mode->fps_d == 1) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x2f);
  } else if (mode->fps_n == 25 && mode->fps_d == 1) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x3f);
  } else if (mode->fps_n == 30000 && mode->fps_d == 1001) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x4f);
  } else if (mode->fps_n == 30 && mode->fps_d == 1) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x5f);
  } else if (mode->fps_n == 50 && mode->fps_d == 1) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x6f);
  } else if (mode->fps_n == 60000 && mode->fps_d == 1001) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x7f);
  } else if (mode->fps_n == 60 && mode->fps_d == 1) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x8f);
  } else {
    g_assert_not_reached ();
  }

  /* ccdata_present | caption_service_active */
  flags = 0x42;

  /* time_code_present */
  if (tc_meta)
    flags |= 0x80;

  /* reserved */
  flags |= 0x01;

  gst_byte_writer_put_uint8_unchecked (&bw, flags);
  gst_byte_writer_put_uint16_be_unchecked (&bw, self->cdp_hdr_sequence_cntr);

  if (tc_meta) {
    const GstVideoTimeCode *tc = &tc_meta->tc;
    guint8 u8;

    gst_byte_writer_put_uint8_unchecked (&bw, 0x71);
    /* reserved 11 - 2 bits */
    u8 = 0xc0;
    /* tens of hours - 2 bits */
    u8 |= ((tc->hours / 10) & 0x3) << 4;
    /* units of hours - 4 bits */
    u8 |= (tc->hours % 10) & 0xf;
    gst_byte_writer_put_uint8_unchecked (&bw, u8);

    /* reserved 1 - 1 bit */
    u8 = 0x80;
    /* tens of minutes - 3 bits */
    u8 |= ((tc->minutes / 10) & 0x7) << 4;
    /* units of minutes - 4 bits */
    u8 |= (tc->minutes % 10) & 0xf;
    gst_byte_writer_put_uint8_unchecked (&bw, u8);

    /* field flag - 1 bit */
    u8 = tc->field_count < 2 ? 0x00 : 0x80;
    /* tens of seconds - 3 bits */
    u8 |= ((tc->seconds / 10) & 0x7) << 4;
    /* units of seconds - 4 bits */
    u8 |= (tc->seconds % 10) & 0xf;
    gst_byte_writer_put_uint8_unchecked (&bw, u8);

    /* drop frame flag - 1 bit */
    u8 = (tc->config.flags & GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME) ? 0x80 :
        0x00;
    /* reserved0 - 1 bit */
    /* tens of frames - 2 bits */
    u8 |= ((tc->frames / 10) & 0x3) << 4;
    /* units of frames 4 bits */
    u8 |= (tc->frames % 10) & 0xf;
    gst_byte_writer_put_uint8_unchecked (&bw, u8);
  }

  gst_byte_writer_put_uint8_unchecked (&bw, 0x72);
  gst_byte_writer_put_uint8_unchecked (&bw, 0xe0 | cc_data_len / 3);
  gst_byte_writer_put_data_unchecked (&bw, cc_data, cc_data_len);

  gst_byte_writer_put_uint8_unchecked (&bw, 0x74);
  gst_byte_writer_put_uint16_be_unchecked (&bw, self->cdp_hdr_sequence_cntr);
  self->cdp_hdr_sequence_cntr++;
  /* We calculate the checksum afterwards */
  gst_byte_writer_put_uint8_unchecked (&bw, 0);

  len = gst_byte_writer_get_pos (&bw);
  gst_byte_writer_set_pos (&bw, 2);
  gst_byte_writer_put_uint8_unchecked (&bw, len);

  checksum = 0;
  for (i = 0; i < len; i++) {
    checksum += cdp[i];
  }
  checksum &= 0xff;
  checksum = 256 - checksum;
  cdp[len - 1] = checksum;

  return len;
}

static void
gst_decklink2_output_write_vbi (GstDeckLink2Output * self,
    const GstVideoInfo * info, GstBuffer * buffer,
    IGstDeckLinkVideoFrame * frame, GstVideoTimeCodeMeta * tc_meta,
    gint caption_line, gint afd_bar_line, gboolean output_vanc)
{
  IDeckLinkVideoFrameAncillary *vanc_frame = NULL;

  g_array_set_size (self->vanc_cache, 0);

  if (caption_line == 0 && afd_bar_line == 0 && !output_vanc)
    return;

  // First collect all metas and transform them to generic GstDeckLink2VancPackets
  if (caption_line != 0) {
    GstVideoCaptionMeta *cc_meta;
    gpointer meta_iter = NULL;

    /* FIXME: Add captions to the correct field? Captions for the second
     * field should probably be inserted into the second field */

    while ((cc_meta =
            (GstVideoCaptionMeta *) gst_buffer_iterate_meta_filtered (buffer,
                &meta_iter, GST_VIDEO_CAPTION_META_API_TYPE))) {
      switch (cc_meta->caption_type) {
        case GST_VIDEO_CAPTION_TYPE_CEA608_RAW:{
          GstDeckLink2VancPacket packet;

          guint n = cc_meta->size / 2;
          if (cc_meta->size > 46) {
            GST_WARNING_OBJECT (self, "Too big raw CEA608 buffer");
            break;
          }

          packet.line_number = caption_line;
          packet.DID = GST_VIDEO_ANCILLARY_DID16_S334_EIA_608 >> 8;
          packet.SDID = GST_VIDEO_ANCILLARY_DID16_S334_EIA_608 & 0xff;
          packet.data_count = 3 * n;
          /* This is the offset from line 9 for 525-line fields and from line
           * 5 for 625-line fields.
           *
           * The highest bit is set for field 1 but not for field 0, but we
           * have no way of knowning the field here
           */
          for (guint i = 0; i < n; i++) {
            packet.data[3 * i] = 0x80 | (info->height ==
                525 ? caption_line - 9 : caption_line - 5);
            packet.data[3 * i + 1] = cc_meta->data[2 * i];
            packet.data[3 * i + 2] = cc_meta->data[2 * i + 1];
          }

          g_array_append_val (self->vanc_cache, packet);
          break;
        }
        case GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A:{
          GstDeckLink2VancPacket packet;

          if (cc_meta->size > 255 || cc_meta->size % 3 != 0) {
            GST_WARNING_OBJECT (self, "Invalid caption payload size");
            break;
          }

          packet.line_number = caption_line;
          packet.DID = GST_VIDEO_ANCILLARY_DID16_S334_EIA_608 >> 8;
          packet.SDID = GST_VIDEO_ANCILLARY_DID16_S334_EIA_608 & 0xff;
          packet.data_count = cc_meta->size;
          memcpy (packet.data, cc_meta->data, cc_meta->size);

          g_array_append_val (self->vanc_cache, packet);
          break;
        }
        case GST_VIDEO_CAPTION_TYPE_CEA708_RAW:{
          GstDeckLink2VancPacket packet;

          guint n = cc_meta->size / 3;
          if (cc_meta->size > 46) {
            GST_WARNING_OBJECT (self, "Too big raw CEA708 buffer");
            break;
          }

          packet.line_number = caption_line;
          packet.DID = GST_VIDEO_ANCILLARY_DID16_S334_EIA_708 >> 8;
          packet.SDID = GST_VIDEO_ANCILLARY_DID16_S334_EIA_708 & 0xff;

          n = convert_cea708_cc_data_cea708_cdp_internal (self, cc_meta->data,
              cc_meta->size, packet.data, sizeof (packet.data), tc_meta);

          packet.data_count = n;

          g_array_append_val (self->vanc_cache, packet);
          break;
        }
        case GST_VIDEO_CAPTION_TYPE_CEA708_CDP:{
          GstDeckLink2VancPacket packet;

          if (cc_meta->size > 255) {
            GST_WARNING_OBJECT (self, "Invalid caption payload size");
            break;
          }

          packet.line_number = caption_line;
          packet.DID = GST_VIDEO_ANCILLARY_DID16_S334_EIA_708 >> 8;
          packet.SDID = GST_VIDEO_ANCILLARY_DID16_S334_EIA_708 & 0xff;
          packet.data_count = cc_meta->size;
          memcpy (packet.data, cc_meta->data, cc_meta->size);

          g_array_append_val (self->vanc_cache, packet);
          break;
        }
        default:{
          GST_FIXME_OBJECT (self, "Caption type %d not supported",
              cc_meta->caption_type);
          break;
        }
      }
    }
  }

  if (afd_bar_line != 0) {
    GstDeckLink2VancPacket packet;
    GstVideoAFDMeta *afd_meta = NULL, *afd_meta2 = NULL;
    GstVideoBarMeta *bar_meta = NULL, *bar_meta2 = NULL;
    GstMeta *meta;
    gpointer meta_iter;
    guint8 afd_bar_data[8] = { 0, };
    guint8 afd_bar_data2[8] = { 0, };
    guint8 afd = 0;
    gboolean is_letterbox = FALSE;
    guint16 bar1 = 0, bar2 = 0;
    guint i;

    // Get any reasonable AFD/Bar metas for both fields
    meta_iter = NULL;
    while ((meta =
            gst_buffer_iterate_meta_filtered (buffer, &meta_iter,
                GST_VIDEO_AFD_META_API_TYPE))) {
      GstVideoAFDMeta *tmp_meta = (GstVideoAFDMeta *) meta;

      if (tmp_meta->field == 0 || !afd_meta || (afd_meta && afd_meta->field != 0
              && tmp_meta->field == 0))
        afd_meta = tmp_meta;
      if (tmp_meta->field == 1 || !afd_meta2 || (afd_meta2
              && afd_meta->field != 1 && tmp_meta->field == 1))
        afd_meta2 = tmp_meta;
    }

    meta_iter = NULL;
    while ((meta =
            gst_buffer_iterate_meta_filtered (buffer, &meta_iter,
                GST_VIDEO_BAR_META_API_TYPE))) {
      GstVideoBarMeta *tmp_meta = (GstVideoBarMeta *) meta;

      if (tmp_meta->field == 0 || !bar_meta || (bar_meta && bar_meta->field != 0
              && tmp_meta->field == 0))
        bar_meta = tmp_meta;
      if (tmp_meta->field == 1 || !bar_meta2 || (bar_meta2
              && bar_meta2->field != 1 && tmp_meta->field == 1))
        bar_meta2 = tmp_meta;
    }

    for (i = 0; i < 2; i++) {
      guint8 *afd_bar_data_ptr;

      if (i == 0) {
        afd_bar_data_ptr = afd_bar_data;
        afd = afd_meta ? afd_meta->afd : 0;
        is_letterbox = bar_meta ? bar_meta->is_letterbox : FALSE;
        bar1 = bar_meta ? bar_meta->bar_data1 : 0;
        bar2 = bar_meta ? bar_meta->bar_data2 : 0;
      } else {
        afd_bar_data_ptr = afd_bar_data2;
        afd = afd_meta2 ? afd_meta2->afd : 0;
        is_letterbox = bar_meta2 ? bar_meta2->is_letterbox : FALSE;
        bar1 = bar_meta2 ? bar_meta2->bar_data1 : 0;
        bar2 = bar_meta2 ? bar_meta2->bar_data2 : 0;
      }

      /* See SMPTE 2016-3 Section 4 */
      /* AFD and AR */
      if (self->selected_mode.mode == bmdModeNTSC ||
          self->selected_mode.mode == bmdModeNTSC2398 ||
          self->selected_mode.mode == bmdModePAL ||
          self->selected_mode.mode == bmdModeNTSCp ||
          self->selected_mode.mode == bmdModePALp) {
        afd_bar_data_ptr[0] = (afd << 3) | 0x0;
      } else {
        afd_bar_data_ptr[0] = (afd << 3) | 0x4;
      }

      /* Bar flags */
      afd_bar_data_ptr[3] = is_letterbox ? 0xc0 : 0x30;

      /* Bar value 1 and 2 */
      GST_WRITE_UINT16_BE (&afd_bar_data_ptr[4], bar1);
      GST_WRITE_UINT16_BE (&afd_bar_data_ptr[6], bar2);
    }

    packet.line_number = afd_bar_line;
    packet.DID = GST_VIDEO_ANCILLARY_DID16_S2016_3_AFD_BAR >> 8;
    packet.SDID = GST_VIDEO_ANCILLARY_DID16_S2016_3_AFD_BAR & 0xff;
    packet.data_count = sizeof (afd_bar_data);
    memcpy (packet.data, afd_bar_data, sizeof (afd_bar_data));

    g_array_append_val (self->vanc_cache, packet);

    /* For interlaced video we need to also add AFD to the second field */
    if (GST_VIDEO_INFO_IS_INTERLACED (info) && afd_bar_line != 0) {
      guint field2_offset;

      /* The VANC lines for the second field are at an offset, depending on
       * the format in use.
       */
      switch (info->height) {
        case 486:
          /* NTSC: 525 / 2 + 1 */
          field2_offset = 263;
          break;
        case 576:
          /* PAL: 625 / 2 + 1 */
          field2_offset = 313;
          break;
        case 1080:
          /* 1080i: 1125 / 2 + 1 */
          field2_offset = 563;
          break;
        default:
          g_assert_not_reached ();
      }

      packet.line_number = afd_bar_line + field2_offset;
      packet.DID = GST_VIDEO_ANCILLARY_DID16_S2016_3_AFD_BAR >> 8;
      packet.SDID = GST_VIDEO_ANCILLARY_DID16_S2016_3_AFD_BAR & 0xff;
      packet.data_count = sizeof (afd_bar_data2);
      memcpy (packet.data, afd_bar_data2, sizeof (afd_bar_data2));

      g_array_append_val (self->vanc_cache, packet);
    }
  }

  if (output_vanc) {
    GstMeta *meta;
    gpointer meta_iter = NULL;

    while ((meta =
            gst_buffer_iterate_meta_filtered (buffer, &meta_iter,
                GST_ANCILLARY_META_API_TYPE))) {
      GstDeckLink2VancPacket packet;
      GstAncillaryMeta *anc_meta = (GstAncillaryMeta *) meta;

      // Skip unpositioned anc for now
      if (anc_meta->line >= 0x7fe)
        continue;

      packet.line_number = anc_meta->line;
      packet.DID = anc_meta->DID & 0xff;
      packet.SDID = anc_meta->SDID_block_number & 0xff;
      packet.data_count = anc_meta->data_count & 0xff;
      for (guint i = 0; i < packet.data_count; i++)
        packet.data[i] = anc_meta->data[i] & 0xff;

      g_array_append_val (self->vanc_cache, packet);
    }
  }

  if (self->vanc_cache->len == 0)
    return;

  // Sort by line number
  /* *INDENT-OFF* */
  g_array_sort (self->vanc_cache,
      (GCompareFunc) +[](const GstDeckLink2VancPacket * a,
          const GstDeckLink2VancPacket * b)->gint {
        return (gint) a->line_number - (gint) b->line_number;
      }
  );
  /* *INDENT-ON* */

  HRESULT res;
  res = gst_decklink2_output_create_ancillary_data (self, bmdFormat10BitYUV,
      &vanc_frame);
  if (res != S_OK) {
    GST_WARNING_OBJECT (self, "Failed to allocate ancillary data: 0x%x",
        (guint) res);
    return;
  }

  if (self->vbi_width != info->width)
    g_clear_pointer (&self->vbi_enc, gst_video_vbi_encoder_free);

  if (!self->vbi_enc) {
    self->vbi_enc =
        gst_video_vbi_encoder_new (GST_VIDEO_FORMAT_v210, info->width);
    self->vbi_width = info->width;
  }

  if (!self->vbi_enc) {
    vanc_frame->Release ();
    return;
  }

  guint previous_line = G_MAXUINT;
  for (guint i = 0; i < self->vanc_cache->len; i++) {
    const auto packet = &g_array_index (self->vanc_cache,
        GstDeckLink2VancPacket, i);

    if (packet->line_number != previous_line && previous_line != G_MAXUINT) {
      guint8 *vancdata;

      res = vanc_frame->GetBufferForVerticalBlankingLine (previous_line,
          (void **) &vancdata);
      if (res != S_OK) {
        GST_WARNING_OBJECT (self,
            "Failed to get buffer for line %u ancillary data: 0x%x",
            previous_line, (guint) res);
        gst_video_vbi_encoder_free (self->vbi_enc);
        self->vbi_enc =
            gst_video_vbi_encoder_new (GST_VIDEO_FORMAT_v210, info->width);
      } else {
        gst_video_vbi_encoder_write_line (self->vbi_enc, vancdata);
      }
    }

    previous_line = packet->line_number;

    GST_TRACE_OBJECT (self,
        "Writing ancillary data with DID %08x, SDID %08x, DC %u into line %u",
        packet->DID, packet->SDID, packet->data_count, packet->line_number);

    if (!gst_video_vbi_encoder_add_ancillary (self->vbi_enc,
            FALSE,
            packet->DID, packet->SDID, packet->data, packet->data_count)) {
      GST_WARNING_OBJECT (self, "Couldn't add ancillary data to line %u",
          packet->line_number);
    }
  }

  // And write the last line
  if (previous_line != G_MAXUINT) {
    guint8 *vancdata;

    res = vanc_frame->GetBufferForVerticalBlankingLine (previous_line,
        (void **) &vancdata);
    if (res != S_OK) {
      GST_WARNING_OBJECT (self,
          "Failed to get buffer for line %u ancillary data: 0x%x",
          previous_line, (guint) res);
      gst_video_vbi_encoder_free (self->vbi_enc);
      self->vbi_enc =
          gst_video_vbi_encoder_new (GST_VIDEO_FORMAT_v210, info->width);
    } else {
      gst_video_vbi_encoder_write_line (self->vbi_enc, vancdata);
    }
  }

  res = frame->SetAncillaryData (vanc_frame);
  if (res != S_OK) {
    GST_WARNING_OBJECT (self, "Failed to set ancillary data: 0x%x",
        (guint) res);
  }

  vanc_frame->Release ();
}

IDeckLinkVideoFrame *
gst_decklink2_output_upload (GstDeckLink2Output * output,
    const GstVideoInfo * info, const GstDeckLink2HDRMetadata * hdr_metadata,
    GstBuffer * buffer, BMDTimecodeFormat timecode_format, gint caption_line,
    gint afd_bar_line, gboolean output_vanc)
{
  GstVideoFrame vframe;
  IGstDeckLinkVideoFrame *frame;
  GstVideoTimeCodeMeta *tc_meta;

  if (!gst_video_frame_map (&vframe, info, buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (output, "Failed to map video frame");
    return NULL;
  }

  frame = new IGstDeckLinkVideoFrame (&vframe);
  frame->SetHDRMetadata (hdr_metadata);

  tc_meta = gst_buffer_get_video_time_code_meta (buffer);
  if (tc_meta)
    frame->SetTimecode (timecode_format, &tc_meta->tc);

  gst_decklink2_output_write_vbi (output, info, buffer, frame, tc_meta,
      caption_line, afd_bar_line, output_vanc);
  g_array_set_size (output->vanc_cache, 0);

  return frame;
}

HRESULT
gst_decklink2_output_configure (GstDeckLink2Output * output,
    BMDProfileID profile_id, const GstDeckLink2OutputConfig * config)
{
  GstDeckLink2OutputPrivate *priv = output->priv;
  const auto vconfig = &config->video;
  const auto aconfig = &config->audio;
  HRESULT hr = S_OK;

  std::lock_guard < std::mutex > lk (priv->extern_lock);
  if (output->configured)
    gst_decklink2_output_stop_internal (output);

  output->selected_mode = vconfig->display_mode;

  if (profile_id != bmdProfileDefault) {
    GstDeckLink2Device *device;

    device = (GstDeckLink2Device *) gst_object_get_parent (GST_OBJECT (output));
    g_assert (device);

    hr = gst_decklink2_device_set_profile_id (device, profile_id);
    gst_object_unref (device);

    if (!gst_decklink2_result (hr)) {
      GST_ERROR_OBJECT (output, "Couldn't set profile ID");
      goto error;
    }
  }

  if (vconfig->mapping_format != GST_DECKLINK2_MAPPING_FORMAT_DEFAULT &&
      (output->attr || output->attr_10_11)) {
    dlbool_t supported = false;

    if (output->attr) {
      hr = output->attr->GetFlag (BMDDeckLinkSupportsSMPTELevelAOutput,
          &supported);
    } else {
      hr = output->attr_10_11->GetFlag (BMDDeckLinkSupportsSMPTELevelAOutput,
          &supported);
    }

    if (gst_decklink2_result (hr) && supported) {
      hr = E_FAIL;
      if (vconfig->mapping_format == GST_DECKLINK2_MAPPING_FORMAT_LEVEL_A) {
        if (output->config_10_11) {
          hr = output->
              config_10_11->SetFlag (bmdDeckLinkConfigSMPTELevelAOutput, true);
        } else if (output->config) {
          hr = output->config->SetFlag (bmdDeckLinkConfigSMPTELevelAOutput,
              true);
        }
      } else if (vconfig->mapping_format ==
          GST_DECKLINK2_MAPPING_FORMAT_LEVEL_B) {
        if (output->config_10_11) {
          hr = output->
              config_10_11->SetFlag (bmdDeckLinkConfigSMPTELevelAOutput, false);
        } else if (output->config) {
          hr = output->config->SetFlag (bmdDeckLinkConfigSMPTELevelAOutput,
              false);
        }
      }

      if (gst_decklink2_result (hr))
        GST_DEBUG_OBJECT (output, "SMPTELevelAOutput is configured");
      else
        GST_WARNING_OBJECT (output, "Couldn't configure SMPTELevelAOutput");
    } else {
      GST_WARNING_OBJECT (output, "SMPTELevelAOutput is not supported");
    }
  }

  if (output->keyer) {
    switch (vconfig->keyer_mode) {
      case GST_DECKLINK2_KEYER_MODE_INTERNAL:
        output->keyer->Enable (false);
        output->keyer->SetLevel (vconfig->keyer_level);
        break;
      case GST_DECKLINK2_KEYER_MODE_EXTERNAL:
        output->keyer->Enable (true);
        output->keyer->SetLevel (vconfig->keyer_level);
        break;
      case GST_DECKLINK2_KEYER_MODE_OFF:
      default:
        output->keyer->Disable ();
        break;
    }
  } else if (vconfig->keyer_mode != GST_DECKLINK2_KEYER_MODE_OFF) {
    GST_WARNING_OBJECT (output, "Keyer interface is unavailable");
  }

  hr = gst_decklink2_output_enable_video (output,
      gst_decklink2_get_real_display_mode (output->selected_mode.mode),
      vconfig->output_flags);
  if (!gst_decklink2_result (hr))
    goto error;

  hr = gst_decklink2_output_set_video_callback (output, output->callback);
  if (!gst_decklink2_result (hr))
    goto error;

  gst_audio_info_init (&output->audio_info);
  if (aconfig->channels > 0) {
    GST_DEBUG_OBJECT (output, "Enabling audio");
    hr = gst_decklink2_output_enable_audio (output, bmdAudioSampleRate48kHz,
        aconfig->sample_type, aconfig->channels,
        bmdAudioOutputStreamTimestamped);
    if (!gst_decklink2_result (hr))
      goto error;

    gst_audio_info_set_format (&output->audio_info,
        aconfig->sample_type == bmdAudioSampleType16bitInteger ?
        GST_AUDIO_FORMAT_S16LE : GST_AUDIO_FORMAT_S32LE, 48000,
        aconfig->channels, NULL);

    priv->audio_buf.Init (GST_OBJECT_NAME (output), output->audio_info);
  } else {
    priv->audio_buf.Deinit ();
  }

  g_clear_pointer (&output->vbi_enc, gst_video_vbi_encoder_free);

  output->n_prerolled = 0;
  output->n_preroll_frames = config->n_preroll_frames;
  output->min_buffered = config->min_buffered;
  output->max_buffered = config->max_buffered;
  output->n_frames = 0;
  output->n_samples = 0;
  output->hw_time = GST_CLOCK_TIME_NONE;
  output->cdp_hdr_sequence_cntr = 0;
  output->configured = TRUE;
  output->pts = 0;
  output->drop_count = 0;
  output->late_count = 0;
  output->underrun_count = 0;
  output->overrun_count = 0;
  output->duplicate_count = 0;
  output->dropped_sample_count = 0;
  output->silent_sample_count = 0;
  output->gap_frames = 1;
  if (config->max_buffered > config->min_buffered) {
    guint gap = (config->max_buffered - config->min_buffered) / 2;
    if (gap >= 2)
      output->gap_frames = 2;
  }
  output->duplicating = FALSE;

  return S_OK;

error:
  gst_decklink2_output_disable_audio (output);
  gst_decklink2_output_disable_video (output);
  gst_decklink2_output_set_video_callback (output, NULL);
  return hr;
}

static void
gst_decklink2_output_on_stopped (GstDeckLink2Output * self)
{
  GST_DEBUG_OBJECT (self, "Scheduled playback stopped");
}

static void
gst_decklink2_output_on_completed (GstDeckLink2Output * self,
    BMDOutputFrameCompletionResult result)
{
  GstDeckLink2OutputPrivate *priv = self->priv;
  dlbool_t active;

  std::lock_guard < std::recursive_mutex > lk (priv->schedule_lock);
  if (result == bmdOutputFrameDisplayedLate) {
    self->late_count++;
  } else if (result == bmdOutputFrameDropped) {
    self->drop_count++;
    priv->completion_dropped = true;
  }

  if (priv->api_failed || !self->last_frame)
    return;

  HRESULT hr = gst_decklink2_output_is_running (self, &active);
  if (gst_decklink2_result (hr) && active) {
    guint buffered_video = 0;
    guint buffered_audio = 0;
    GstClockTime video_running_time = GST_CLOCK_TIME_NONE;
    GstClockTime audio_running_time = GST_CLOCK_TIME_NONE;
    GstClockTime hw_time_gst = GST_CLOCK_TIME_NONE;
    GstClockTime dummy;
    GstClockTimeDiff diff = GST_CLOCK_STIME_NONE;

    hr = gst_decklink2_get_current_level (self, &buffered_video,
        &video_running_time, &buffered_audio, &audio_running_time, &diff,
        &hw_time_gst, &dummy);
    if (gst_decklink2_result (hr) && buffered_video <= self->min_buffered) {
      GST_WARNING_OBJECT (self, "Underrun, video %" GST_TIME_FORMAT
          " (%" G_GUINT64_FORMAT ", buffered %u) audio %" GST_TIME_FORMAT
          " (%" G_GUINT64_FORMAT ", buffered %u), av-diff: %" GST_STIME_FORMAT
          ", hw-time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (self->pts), self->n_frames, buffered_video,
          GST_TIME_ARGS (audio_running_time), self->n_samples, buffered_audio,
          GST_STIME_ARGS (diff), GST_TIME_ARGS (hw_time_gst));
      self->underrun_count++;
      if (self->duplicating)
        return;

      self->duplicating = TRUE;
      for (guint i = 0; i < self->gap_frames; i++) {
        IGstDeckLinkVideoFrame *copy = self->last_frame->Clone ();
        if (!copy) {
          GST_ERROR_OBJECT (self, "Couldn't clone last frame");
          copy = self->last_frame;
          self->last_frame->AddRef ();
        }

        if (GST_AUDIO_INFO_IS_VALID (&self->audio_info)) {
          guint64 sample_scale = (guint64) self->selected_mode.fps_d *
              self->audio_info.rate;
          guint64 sample_start = gst_util_uint64_scale (self->n_frames,
              sample_scale, self->selected_mode.fps_n);
          guint64 sample_end = gst_util_uint64_scale (self->n_frames + 1,
              sample_scale, self->selected_mode.fps_n);

          self->silent_sample_count +=
              priv->audio_buf.PrependSilence (sample_end - sample_start);
        }

        hr = gst_decklink2_output_schedule_video_internal (self, copy);
        copy->Release ();
        if (!gst_decklink2_result (hr)) {
          priv->api_failed = true;
          break;
        }

        self->duplicate_count++;
      }
      self->duplicating = FALSE;
    }
  }
}
