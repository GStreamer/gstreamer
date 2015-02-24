/*
 * GStreamer
 * Copyright (C) 2012-2013 Fluendo S.A. <support@fluendo.com>
 *   Authors: Josep Torra Vallès <josep@fluendo.com>
 *            Andoni Morales Alastruey <amorales@fluendo.com>
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
 *
 */

#include "gstosxcoreaudio.h"
#include "gstosxcoreaudiocommon.h"

GST_DEBUG_CATEGORY_STATIC (osx_audio_debug);
#define GST_CAT_DEFAULT osx_audio_debug

G_DEFINE_TYPE (GstCoreAudio, gst_core_audio, G_TYPE_OBJECT);

#ifdef HAVE_IOS
#include "gstosxcoreaudioremoteio.c"
#else
#include "gstosxcoreaudiohal.c"
#endif


static void
gst_core_audio_class_init (GstCoreAudioClass * klass)
{
}

static void
gst_core_audio_init (GstCoreAudio * core_audio)
{
  core_audio->is_passthrough = FALSE;
  core_audio->device_id = kAudioDeviceUnknown;
  core_audio->is_src = FALSE;
  core_audio->audiounit = NULL;
#ifndef HAVE_IOS
  core_audio->hog_pid = -1;
  core_audio->disabled_mixing = FALSE;
#endif
}

/**************************
 *       Public API       *
 *************************/

GstCoreAudio *
gst_core_audio_new (GstObject * osxbuf)
{
  GstCoreAudio *core_audio;

  core_audio = g_object_new (GST_TYPE_CORE_AUDIO, NULL);
  core_audio->osxbuf = osxbuf;
  return core_audio;
}

gboolean
gst_core_audio_close (GstCoreAudio * core_audio)
{
  AudioComponentInstanceDispose (core_audio->audiounit);
  core_audio->audiounit = NULL;
  return TRUE;
}

gboolean
gst_core_audio_open (GstCoreAudio * core_audio)
{
  return gst_core_audio_open_impl (core_audio);
}

gboolean
gst_core_audio_start_processing (GstCoreAudio * core_audio)
{
  return gst_core_audio_start_processing_impl (core_audio);
}

gboolean
gst_core_audio_pause_processing (GstCoreAudio * core_audio)
{
  return gst_core_audio_pause_processing_impl (core_audio);
}

gboolean
gst_core_audio_stop_processing (GstCoreAudio * core_audio)
{
  return gst_core_audio_stop_processing_impl (core_audio);
}

gboolean
gst_core_audio_get_samples_and_latency (GstCoreAudio * core_audio,
    gdouble rate, guint * samples, gdouble * latency)
{
  return gst_core_audio_get_samples_and_latency_impl (core_audio, rate,
      samples, latency);
}

gboolean
gst_core_audio_initialize (GstCoreAudio * core_audio,
    AudioStreamBasicDescription format, GstCaps * caps, gboolean is_passthrough)
{
  guint32 frame_size;
  OSStatus status;

  GST_DEBUG_OBJECT (core_audio,
      "Initializing: passthrough:%d caps:%" GST_PTR_FORMAT, is_passthrough,
      caps);

  if (!gst_core_audio_initialize_impl (core_audio, format, caps,
          is_passthrough, &frame_size)) {
    goto error;
  }

  if (core_audio->is_src) {
    /* create AudioBufferList needed for recording */
    core_audio->recBufferSize = frame_size * format.mBytesPerFrame;
    core_audio->recBufferList =
        buffer_list_alloc (format.mChannelsPerFrame, core_audio->recBufferSize,
        /* Currently always TRUE (i.e. interleaved) */
        !(format.mFormatFlags & kAudioFormatFlagIsNonInterleaved));
  }

  /* Initialize the AudioUnit */
  status = AudioUnitInitialize (core_audio->audiounit);
  if (status) {
    GST_ERROR_OBJECT (core_audio, "Failed to initialise AudioUnit: %d",
        (int) status);
    goto error;
  }
  return TRUE;

error:
  buffer_list_free (core_audio->recBufferList);
  core_audio->recBufferList = NULL;
  return FALSE;
}

void
gst_core_audio_unitialize (GstCoreAudio * core_audio)
{
  AudioUnitUninitialize (core_audio->audiounit);

  buffer_list_free (core_audio->recBufferList);
  core_audio->recBufferList = NULL;
}

void
gst_core_audio_set_volume (GstCoreAudio * core_audio, gfloat volume)
{
  AudioUnitSetParameter (core_audio->audiounit, kHALOutputParam_Volume,
      kAudioUnitScope_Global, 0, (float) volume, 0);
}

gboolean
gst_core_audio_select_device (GstCoreAudio * core_audio)
{
  return gst_core_audio_select_device_impl (core_audio);
}

void
gst_core_audio_init_debug (void)
{
  GST_DEBUG_CATEGORY_INIT (osx_audio_debug, "osxaudio", 0,
      "OSX Audio Elements");
}

gboolean
gst_core_audio_audio_device_is_spdif_avail (AudioDeviceID device_id)
{
  return gst_core_audio_audio_device_is_spdif_avail_impl (device_id);
}

gboolean
gst_core_audio_parse_channel_layout (AudioChannelLayout * layout,
    gint channels, guint64 * channel_mask, GstAudioChannelPosition * pos)
{
  gint i;
  gboolean ret = TRUE;

  g_return_val_if_fail (channels <= GST_OSX_AUDIO_MAX_CHANNEL, FALSE);

  switch (channels) {
    case 0:
      pos[0] = GST_AUDIO_CHANNEL_POSITION_NONE;
      break;
    case 1:
      pos[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
      break;
    case 2:
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      *channel_mask |= GST_AUDIO_CHANNEL_POSITION_MASK (FRONT_LEFT);
      *channel_mask |= GST_AUDIO_CHANNEL_POSITION_MASK (FRONT_RIGHT);
      break;
    default:
      for (i = 0; i < channels; i++) {
        switch (layout->mChannelDescriptions[i].mChannelLabel) {
          case kAudioChannelLabel_Left:
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
            break;
          case kAudioChannelLabel_Right:
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
            break;
          case kAudioChannelLabel_Center:
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
            break;
          case kAudioChannelLabel_LFEScreen:
            pos[i] = GST_AUDIO_CHANNEL_POSITION_LFE1;
            break;
          case kAudioChannelLabel_LeftSurround:
            pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
            break;
          case kAudioChannelLabel_RightSurround:
            pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
            break;
          case kAudioChannelLabel_RearSurroundLeft:
            pos[i] = GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
            break;
          case kAudioChannelLabel_RearSurroundRight:
            pos[i] = GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
            break;
          case kAudioChannelLabel_CenterSurround:
            pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
            break;
          default:
            GST_WARNING ("unrecognized channel: %d",
                (int) layout->mChannelDescriptions[i].mChannelLabel);
            *channel_mask = 0;
            ret = FALSE;
            break;
        }
      }
  }

  return ret;
}

GstCaps *
gst_core_audio_asbd_to_caps (AudioStreamBasicDescription * asbd,
    AudioChannelLayout * layout)
{
  GstAudioInfo info;
  GstAudioFormat format = GST_AUDIO_FORMAT_UNKNOWN;
  GstAudioChannelPosition pos[64] = { GST_AUDIO_CHANNEL_POSITION_INVALID, };
  gint rate, channels, bps, endianness;
  guint64 channel_mask;
  gboolean sign, interleaved;

  if (asbd->mFormatID != kAudioFormatLinearPCM) {
    GST_WARNING ("Only linear PCM is supported");
    goto error;
  }

  if (!(asbd->mFormatFlags & kAudioFormatFlagIsPacked)) {
    GST_WARNING ("Only packed formats supported");
    goto error;
  }

  if (asbd->mFormatFlags & kLinearPCMFormatFlagsSampleFractionMask) {
    GST_WARNING ("Fixed point audio is unsupported");
    goto error;
  }

  rate = asbd->mSampleRate;
  if (rate == kAudioStreamAnyRate)
    rate = GST_AUDIO_DEF_RATE;

  channels = asbd->mChannelsPerFrame;
  if (channels == 0) {
    /* The documentation says this should not happen! */
    channels = 1;
  }

  bps = asbd->mBitsPerChannel;
  endianness = asbd->mFormatFlags & kAudioFormatFlagIsBigEndian ?
      G_BIG_ENDIAN : G_LITTLE_ENDIAN;
  sign = asbd->mFormatID & kAudioFormatFlagIsSignedInteger ? TRUE : FALSE;
  interleaved = asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved ?
      TRUE : FALSE;

  if (asbd->mFormatFlags & kAudioFormatFlagIsFloat) {
    if (bps == 32) {
      if (endianness == G_LITTLE_ENDIAN)
        format = GST_AUDIO_FORMAT_F32LE;
      else
        format = GST_AUDIO_FORMAT_F32BE;

    } else if (bps == 64) {
      if (endianness == G_LITTLE_ENDIAN)
        format = GST_AUDIO_FORMAT_F64LE;
      else
        format = GST_AUDIO_FORMAT_F64BE;
    }
  } else {
    format = gst_audio_format_build_integer (sign, endianness, bps, bps);
  }

  if (format == GST_AUDIO_FORMAT_UNKNOWN) {
    GST_WARNING ("Unsupported sample format");
    goto error;
  }

  if (!gst_core_audio_parse_channel_layout (layout, channels, &channel_mask,
          pos)) {
    GST_WARNING ("Failed to parse channel layout");
    goto error;
  }

  gst_audio_info_set_format (&info, format, rate, channels, pos);

  return gst_audio_info_to_caps (&info);

error:
  return NULL;
}
