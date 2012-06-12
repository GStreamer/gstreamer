/*
 * GStreamer
 * Copyright (C) 2012 Fluendo S.A. <support@fluendo.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * The development of this code was made possible due to the involvement of
 * Pioneers of the Inevitable, the creators of the Songbird Music player
 *
 */

#define CORE_AUDIO_FORMAT "FormatID: %" GST_FOURCC_FORMAT " rate: %f flags: 0x%x BytesPerPacket: %u FramesPerPacket: %u BytesPerFrame: %u ChannelsPerFrame: %u BitsPerChannel: %u"
#define CORE_AUDIO_FORMAT_ARGS(f) GST_FOURCC_ARGS((f).mFormatID),(f).mSampleRate,(unsigned)(f).mFormatFlags,(unsigned)(f).mBytesPerPacket,(unsigned)(f).mFramesPerPacket,(unsigned)(f).mBytesPerFrame,(unsigned)(f).mChannelsPerFrame,(unsigned)(f).mBitsPerChannel

#define CORE_AUDIO_FORMAT_IS_SPDIF(f) ((f).mFormat.mFormatID == 'IAC3' || (f).mFormat.mFormatID == 'iac3' || (f).mFormat.mFormatID == kAudioFormat60958AC3 || (f).mFormat.mFormatID == kAudioFormatAC3)

static inline gboolean
_audio_system_set_runloop (CFRunLoopRef runLoop)
{
  OSStatus status = noErr;

  gboolean res = FALSE;

  AudioObjectPropertyAddress runloopAddress = {
    kAudioHardwarePropertyRunLoop,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectSetPropertyData (kAudioObjectSystemObject,
      &runloopAddress, 0, NULL, sizeof (CFRunLoopRef), &runLoop);
  if (status == noErr) {
    res = TRUE;
  } else {
    GST_ERROR ("failed to set runloop to %p: %" GST_FOURCC_FORMAT,
        runLoop, GST_FOURCC_ARGS (status));
  }

  return res;
}

static inline AudioDeviceID
_audio_system_get_default_output (void)
{
  OSStatus status = noErr;
  UInt32 propertySize = sizeof (AudioDeviceID);
  AudioDeviceID device_id = kAudioDeviceUnknown;

  AudioObjectPropertyAddress defaultDeviceAddress = {
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectGetPropertyData (kAudioObjectSystemObject,
      &defaultDeviceAddress, 0, NULL, &propertySize, &device_id);
  if (status != noErr) {
    GST_ERROR ("failed getting default output device: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
  }

  return device_id;
}

static inline AudioDeviceID *
_audio_system_get_devices (gint * ndevices)
{
  OSStatus status = noErr;
  UInt32 propertySize = 0;
  AudioDeviceID *devices = NULL;

  AudioObjectPropertyAddress audioDevicesAddress = {
    kAudioHardwarePropertyDevices,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectGetPropertyDataSize (kAudioObjectSystemObject,
      &audioDevicesAddress, 0, NULL, &propertySize);
  if (status != noErr) {
    GST_WARNING ("failed getting number of devices: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
    return NULL;
  }

  *ndevices = propertySize / sizeof (AudioDeviceID);

  devices = (AudioDeviceID *) g_malloc (propertySize);
  if (devices) {
    status = AudioObjectGetPropertyData (kAudioObjectSystemObject,
        &audioDevicesAddress, 0, NULL, &propertySize, devices);
    if (status != noErr) {
      GST_WARNING ("failed getting the list of devices: %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
      g_free (devices);
      *ndevices = 0;
      return NULL;
    }
  }
  return devices;
}

static inline gboolean
_audio_device_is_alive (AudioDeviceID device_id)
{
  OSStatus status = noErr;
  int alive = FALSE;
  UInt32 propertySize = sizeof (alive);

  AudioObjectPropertyAddress audioDeviceAliveAddress = {
    kAudioDevicePropertyDeviceIsAlive,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectGetPropertyData (device_id,
      &audioDeviceAliveAddress, 0, NULL, &propertySize, &alive);
  if (status != noErr) {
    alive = FALSE;
  }

  return alive;
}

static inline guint
_audio_device_get_latency (AudioDeviceID device_id)
{
  OSStatus status = noErr;
  UInt32 latency = 0;
  UInt32 propertySize = sizeof (latency);

  AudioObjectPropertyAddress audioDeviceLatencyAddress = {
    kAudioDevicePropertyLatency,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectGetPropertyData (device_id,
      &audioDeviceLatencyAddress, 0, NULL, &propertySize, &latency);
  if (status != noErr) {
    GST_ERROR ("failed to get latency: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
    latency = -1;
  }

  return latency;
}

static inline pid_t
_audio_device_get_hog (AudioDeviceID device_id)
{
  OSStatus status = noErr;
  pid_t hog_pid;
  UInt32 propertySize = sizeof (hog_pid);

  AudioObjectPropertyAddress audioDeviceHogModeAddress = {
    kAudioDevicePropertyHogMode,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectGetPropertyData (device_id,
      &audioDeviceHogModeAddress, 0, NULL, &propertySize, &hog_pid);
  if (status != noErr) {
    GST_ERROR ("failed to get hog: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
    hog_pid = -1;
  }

  return hog_pid;
}

static inline gboolean
_audio_device_set_hog (AudioDeviceID device_id, pid_t hog_pid)
{
  OSStatus status = noErr;
  UInt32 propertySize = sizeof (hog_pid);
  gboolean res = FALSE;

  AudioObjectPropertyAddress audioDeviceHogModeAddress = {
    kAudioDevicePropertyHogMode,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectSetPropertyData (device_id,
      &audioDeviceHogModeAddress, 0, NULL, propertySize, &hog_pid);

  if (status == noErr) {
    res = TRUE;
  } else {
    GST_ERROR ("failed to set hog: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
  }

  return res;
}

static inline gboolean
_audio_device_set_mixing (AudioDeviceID device_id, gboolean enable_mix)
{
  OSStatus status = noErr;
  UInt32 propertySize = 0, can_mix = enable_mix;
  Boolean writable = FALSE;
  gboolean res = FALSE;

  AudioObjectPropertyAddress audioDeviceSupportsMixingAddress = {
    kAudioDevicePropertySupportsMixing,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };

  if (AudioObjectHasProperty (device_id, &audioDeviceSupportsMixingAddress)) {
    /* Set mixable to false if we are allowed to */
    status = AudioObjectIsPropertySettable (device_id,
        &audioDeviceSupportsMixingAddress, &writable);
    if (status) {
      GST_DEBUG ("AudioObjectIsPropertySettable: %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (status));
    }
    status = AudioObjectGetPropertyDataSize (device_id,
        &audioDeviceSupportsMixingAddress, 0, NULL, &propertySize);
    if (status) {
      GST_DEBUG ("AudioObjectGetPropertyDataSize: %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (status));
    }
    status = AudioObjectGetPropertyData (device_id,
        &audioDeviceSupportsMixingAddress, 0, NULL, &propertySize, &can_mix);
    if (status) {
      GST_DEBUG ("AudioObjectGetPropertyData: %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (status));
    }

    if (status == noErr && writable) {
      can_mix = enable_mix;
      status = AudioObjectSetPropertyData (device_id,
          &audioDeviceSupportsMixingAddress, 0, NULL, propertySize, &can_mix);
      res = TRUE;
    }

    if (status != noErr) {
      GST_ERROR ("failed to set mixmode: %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (status));
    }
  } else {
    GST_DEBUG ("property not found, mixing coudln't be changed");
  }

  return res;
}

static inline gchar *
_audio_device_get_name (AudioDeviceID device_id)
{
  OSStatus status = noErr;
  UInt32 propertySize = 0;
  gchar *device_name = NULL;

  AudioObjectPropertyAddress deviceNameAddress = {
    kAudioDevicePropertyDeviceName,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMaster
  };

  /* Get the length of the device name */
  status = AudioObjectGetPropertyDataSize (device_id,
      &deviceNameAddress, 0, NULL, &propertySize);
  if (status != noErr) {
    goto beach;
  }

  /* Get the name of the device */
  device_name = (gchar *) g_malloc (propertySize);
  status = AudioObjectGetPropertyData (device_id,
      &deviceNameAddress, 0, NULL, &propertySize, device_name);
  if (status != noErr) {
    g_free (device_name);
    device_name = NULL;
  }

beach:
  return device_name;
}

static inline gboolean
_audio_device_has_output (AudioDeviceID device_id)
{
  OSStatus status = noErr;
  UInt32 propertySize;

  AudioObjectPropertyAddress streamsAddress = {
    kAudioDevicePropertyStreams,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectGetPropertyDataSize (device_id,
      &streamsAddress, 0, NULL, &propertySize);
  if (status != noErr) {
    return FALSE;
  }
  if (propertySize == 0) {
    return FALSE;
  }

  return TRUE;
}

static inline AudioChannelLayout *
_audio_device_get_channel_layout (AudioDeviceID device_id)
{
  OSStatus status = noErr;
  UInt32 propertySize = 0;
  AudioChannelLayout *channel_layout = NULL;

  AudioObjectPropertyAddress channelLayoutAddress = {
    kAudioDevicePropertyPreferredChannelLayout,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMaster
  };

  /* Get the length of the default channel layout structure */
  status = AudioObjectGetPropertyDataSize (device_id,
      &channelLayoutAddress, 0, NULL, &propertySize);
  if (status != noErr) {
    goto beach;
  }

  /* Get the default channel layout of the device */
  channel_layout = (AudioChannelLayout *) g_malloc (propertySize);
  status = AudioObjectGetPropertyData (device_id,
      &channelLayoutAddress, 0, NULL, &propertySize, channel_layout);
  if (status != noErr) {
    g_free (channel_layout);
    channel_layout = NULL;
  }

beach:
  return channel_layout;
}

static inline AudioStreamID *
_audio_device_get_streams (AudioDeviceID device_id, gint * nstreams)
{
  OSStatus status = noErr;
  UInt32 propertySize = 0;
  AudioStreamID *streams = NULL;

  AudioObjectPropertyAddress streamsAddress = {
    kAudioDevicePropertyStreams,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectGetPropertyDataSize (device_id,
      &streamsAddress, 0, NULL, &propertySize);
  if (status != noErr) {
    GST_WARNING ("failed getting number of streams: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
    return NULL;
  }

  *nstreams = propertySize / sizeof (AudioStreamID);
  streams = (AudioStreamID *) g_malloc (propertySize);

  if (streams) {
    status = AudioObjectGetPropertyData (device_id,
        &streamsAddress, 0, NULL, &propertySize, streams);
    if (status != noErr) {
      GST_WARNING ("failed getting the list of streams: %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
      g_free (streams);
      *nstreams = 0;
      return NULL;
    }
  }

  return streams;
}

static inline guint
_audio_stream_get_latency (AudioStreamID stream_id)
{
  OSStatus status = noErr;
  UInt32 latency;
  UInt32 propertySize = sizeof (latency);

  AudioObjectPropertyAddress latencyAddress = {
    kAudioStreamPropertyLatency,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectGetPropertyData (stream_id,
      &latencyAddress, 0, NULL, &propertySize, &latency);
  if (status != noErr) {
    GST_ERROR ("failed to get latency: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
    latency = -1;
  }

  return latency;
}

static inline gboolean
_audio_stream_get_current_format (AudioStreamID stream_id,
    AudioStreamBasicDescription * format)
{
  OSStatus status = noErr;
  UInt32 propertySize = sizeof (AudioStreamBasicDescription);

  AudioObjectPropertyAddress formatAddress = {
    kAudioStreamPropertyPhysicalFormat,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectGetPropertyData (stream_id,
      &formatAddress, 0, NULL, &propertySize, format);
  if (status != noErr) {
    GST_ERROR ("failed to get current format: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
    return FALSE;
  }

  return TRUE;
}

static inline gboolean
_audio_stream_set_current_format (AudioStreamID stream_id,
    AudioStreamBasicDescription format)
{
  OSStatus status = noErr;
  UInt32 propertySize = sizeof (AudioStreamBasicDescription);

  AudioObjectPropertyAddress formatAddress = {
    kAudioStreamPropertyPhysicalFormat,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectSetPropertyData (stream_id,
      &formatAddress, 0, NULL, propertySize, &format);
  if (status != noErr) {
    GST_ERROR ("failed to set current format: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
    return FALSE;
  }

  return TRUE;
}

static inline AudioStreamRangedDescription *
_audio_stream_get_formats (AudioStreamID stream_id, gint * nformats)
{
  OSStatus status = noErr;
  UInt32 propertySize = 0;
  AudioStreamRangedDescription *formats = NULL;

  AudioObjectPropertyAddress formatsAddress = {
    kAudioStreamPropertyAvailablePhysicalFormats,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };

  status = AudioObjectGetPropertyDataSize (stream_id,
      &formatsAddress, 0, NULL, &propertySize);
  if (status != noErr) {
    GST_WARNING ("failed getting number of stream formats: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
    return NULL;
  }

  *nformats = propertySize / sizeof (AudioStreamRangedDescription);

  formats = (AudioStreamRangedDescription *) g_malloc (propertySize);
  if (formats) {
    status = AudioObjectGetPropertyData (stream_id,
        &formatsAddress, 0, NULL, &propertySize, formats);
    if (status != noErr) {
      GST_WARNING ("failed getting the list of stream formats: %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
      g_free (formats);
      *nformats = 0;
      return NULL;
    }
  }
  return formats;
}

static inline gboolean
_audio_stream_is_spdif_avail (AudioStreamID stream_id)
{
  AudioStreamRangedDescription *formats;
  gint i, nformats = 0;
  gboolean res = FALSE;

  formats = _audio_stream_get_formats (stream_id, &nformats);
  GST_DEBUG ("found %d stream formats", nformats);

  if (formats) {
    GST_DEBUG ("formats supported on stream ID: %u",
        (unsigned) stream_id);

    for (i = 0; i < nformats; i++) {
      GST_DEBUG ("  " CORE_AUDIO_FORMAT,
          CORE_AUDIO_FORMAT_ARGS (formats[i].mFormat));

      if (CORE_AUDIO_FORMAT_IS_SPDIF (formats[i])) {
        res = TRUE;
      }
    }
    g_free (formats);
  }

  return res;
}

static inline gboolean
_audio_device_is_spdif_avail (AudioDeviceID device_id)
{
  AudioStreamID *streams = NULL;
  gint i, nstreams = 0;
  gboolean res = FALSE;

  streams = _audio_device_get_streams (device_id, &nstreams);
  GST_DEBUG ("found %d streams", nstreams);
  if (streams) {
    for (i = 0; i < nstreams; i++) {
      if (_audio_stream_is_spdif_avail (streams[i])) {
        res = TRUE;
      }
    }

    g_free (streams);
  }

  return res;
}

