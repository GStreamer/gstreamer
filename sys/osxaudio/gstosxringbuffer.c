/*
 * GStreamer
 * Copyright (C) 2006 Zaheer Abbas Merali <zaheerabbas at merali dot org>
 * Copyright (C) 2008 Pioneers of the Inevitable <songbird@songbirdnest.com>
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
 */

#include <CoreAudio/CoreAudio.h>
#include <CoreServices/CoreServices.h>
#include <gst/gst.h>
#include <gst/audio/multichannel.h>
#include "gstosxringbuffer.h"
#include "gstosxaudiosink.h"
#include "gstosxaudiosrc.h"

#include <unistd.h>             /* for getpid() */

GST_DEBUG_CATEGORY_STATIC (osx_audio_debug);
#define GST_CAT_DEFAULT osx_audio_debug

#include "gstosxcoreaudio.h"

static void gst_osx_ring_buffer_dispose (GObject * object);
static void gst_osx_ring_buffer_finalize (GObject * object);
static gboolean gst_osx_ring_buffer_open_device (GstRingBuffer * buf);
static gboolean gst_osx_ring_buffer_close_device (GstRingBuffer * buf);

static gboolean gst_osx_ring_buffer_acquire (GstRingBuffer * buf,
    GstRingBufferSpec * spec);
static gboolean gst_osx_ring_buffer_release (GstRingBuffer * buf);

static gboolean gst_osx_ring_buffer_start (GstRingBuffer * buf);
static gboolean gst_osx_ring_buffer_pause (GstRingBuffer * buf);
static gboolean gst_osx_ring_buffer_stop (GstRingBuffer * buf);
static guint gst_osx_ring_buffer_delay (GstRingBuffer * buf);
static GstRingBufferClass *ring_parent_class = NULL;

static void gst_osx_ring_buffer_remove_render_callback (GstOsxRingBuffer *
    osxbuf);

static void
gst_osx_ring_buffer_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (osx_audio_debug, "osxaudio", 0,
      "OSX Audio Elements");
}

GST_BOILERPLATE_FULL (GstOsxRingBuffer, gst_osx_ring_buffer,
    GstRingBuffer, GST_TYPE_RING_BUFFER, gst_osx_ring_buffer_do_init);

static void
gst_osx_ring_buffer_base_init (gpointer g_class)
{
  /* Nothing to do right now */
}

static void
gst_osx_ring_buffer_class_init (GstOsxRingBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstRingBufferClass *gstringbuffer_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstringbuffer_class = (GstRingBufferClass *) klass;

  ring_parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_osx_ring_buffer_dispose;
  gobject_class->finalize = gst_osx_ring_buffer_finalize;

  gstringbuffer_class->open_device =
      GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_open_device);
  gstringbuffer_class->close_device =
      GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_close_device);
  gstringbuffer_class->acquire =
      GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_acquire);
  gstringbuffer_class->release =
      GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_release);
  gstringbuffer_class->start = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_start);
  gstringbuffer_class->pause = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_pause);
  gstringbuffer_class->resume = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_start);
  gstringbuffer_class->stop = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_stop);

  gstringbuffer_class->delay = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_delay);

  GST_DEBUG ("osx ring buffer class init");
}

static void
gst_osx_ring_buffer_init (GstOsxRingBuffer * ringbuffer,
    GstOsxRingBufferClass * g_class)
{
  /* Nothing to do right now */
  ringbuffer->is_passthrough = FALSE;
  ringbuffer->hog_pid = -1;
  ringbuffer->disabled_mixing = FALSE;
}

static void
gst_osx_ring_buffer_dispose (GObject * object)
{
  G_OBJECT_CLASS (ring_parent_class)->dispose (object);
}

static void
gst_osx_ring_buffer_finalize (GObject * object)
{
  G_OBJECT_CLASS (ring_parent_class)->finalize (object);
}

static AudioUnit
gst_osx_ring_buffer_create_audio_unit (GstOsxRingBuffer * osxbuf,
    gboolean input, AudioDeviceID device_id)
{
  ComponentDescription desc;
  Component comp;
  OSStatus status;
  AudioUnit unit;
  UInt32 enableIO;
  AudioStreamBasicDescription asbd_in;
  UInt32 propertySize;

  /* Create a HALOutput AudioUnit.
   * This is the lowest-level output API that is actually sensibly
   * usable (the lower level ones require that you do
   * channel-remapping yourself, and the CoreAudio channel mapping
   * is sufficiently complex that doing so would be very difficult)
   *
   * Note that for input we request an output unit even though
   * we will do input with it.
   * http://developer.apple.com/technotes/tn2002/tn2091.html
   */
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_HALOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  desc.componentFlags = 0;
  desc.componentFlagsMask = 0;

  comp = FindNextComponent (NULL, &desc);
  if (comp == NULL) {
    GST_WARNING_OBJECT (osxbuf, "Couldn't find HALOutput component");
    return NULL;
  }

  status = OpenAComponent (comp, &unit);

  if (status) {
    GST_ERROR_OBJECT (osxbuf, "Couldn't open HALOutput component %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
    return NULL;
  }

  if (input) {
    /* enable input */
    enableIO = 1;
    status = AudioUnitSetProperty (unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1,   /* 1 = input element */
        &enableIO, sizeof (enableIO));

    if (status) {
      CloseComponent (unit);
      GST_WARNING_OBJECT (osxbuf, "Failed to enable input: %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
      return NULL;
    }

    /* disable output */
    enableIO = 0;
    status = AudioUnitSetProperty (unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0,  /* 0 = output element */
        &enableIO, sizeof (enableIO));

    if (status) {
      CloseComponent (unit);
      GST_WARNING_OBJECT (osxbuf, "Failed to disable output: %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
      return NULL;
    }
  }

  GST_DEBUG_OBJECT (osxbuf, "Created HALOutput AudioUnit: %p", unit);

  if (input) {
    GstOsxAudioSrc *src = GST_OSX_AUDIO_SRC (GST_OBJECT_PARENT (osxbuf));

    propertySize = sizeof (asbd_in);
    status = AudioUnitGetProperty (unit,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input, 1, &asbd_in, &propertySize);

    if (status) {
      CloseComponent (unit);
      GST_WARNING_OBJECT (osxbuf,
          "Unable to obtain device properties: %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (status));
      return NULL;
    }

    src->deviceChannels = asbd_in.mChannelsPerFrame;
  } else {
    GstOsxAudioSink *sink = GST_OSX_AUDIO_SINK (GST_OBJECT_PARENT (osxbuf));

    /* needed for the sink's volume control */
    sink->audiounit = unit;
  }

  return unit;
}

static gboolean
gst_osx_ring_buffer_open_device (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  /* The following is needed to instruct HAL to create their own
   * thread to handle the notifications. */
  _audio_system_set_runloop (NULL);

  osxbuf->audiounit = gst_osx_ring_buffer_create_audio_unit (osxbuf,
      osxbuf->is_src, osxbuf->device_id);

  if (!osxbuf->audiounit) {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_osx_ring_buffer_close_device (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;
  osxbuf = GST_OSX_RING_BUFFER (buf);

  CloseComponent (osxbuf->audiounit);
  osxbuf->audiounit = NULL;

  return TRUE;
}

static AudioChannelLabel
gst_audio_channel_position_to_coreaudio_channel_label (GstAudioChannelPosition
    position, int channel)
{
  switch (position) {
    case GST_AUDIO_CHANNEL_POSITION_NONE:
      return kAudioChannelLabel_Discrete_0 | channel;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_MONO:
      return kAudioChannelLabel_Mono;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT:
      return kAudioChannelLabel_Left;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT:
      return kAudioChannelLabel_Right;
    case GST_AUDIO_CHANNEL_POSITION_REAR_CENTER:
      return kAudioChannelLabel_CenterSurround;
    case GST_AUDIO_CHANNEL_POSITION_REAR_LEFT:
      return kAudioChannelLabel_LeftSurround;
    case GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT:
      return kAudioChannelLabel_RightSurround;
    case GST_AUDIO_CHANNEL_POSITION_LFE:
      return kAudioChannelLabel_LFEScreen;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER:
      return kAudioChannelLabel_Center;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER:
      return kAudioChannelLabel_Center; // ???
    case GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER:
      return kAudioChannelLabel_Center; // ???
    case GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT:
      return kAudioChannelLabel_LeftSurroundDirect;
    case GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT:
      return kAudioChannelLabel_RightSurroundDirect;
    default:
      return kAudioChannelLabel_Unknown;
  }
}

static AudioBufferList *
buffer_list_alloc (int channels, int size)
{
  AudioBufferList *list;
  int total_size;
  int n;

  total_size = sizeof (AudioBufferList) + 1 * sizeof (AudioBuffer);
  list = (AudioBufferList *) g_malloc (total_size);

  list->mNumberBuffers = 1;
  for (n = 0; n < (int) list->mNumberBuffers; ++n) {
    list->mBuffers[n].mNumberChannels = channels;
    list->mBuffers[n].mDataByteSize = size;
    list->mBuffers[n].mData = g_malloc (size);
  }

  return list;
}

static void
buffer_list_free (AudioBufferList * list)
{
  int n;

  for (n = 0; n < (int) list->mNumberBuffers; ++n) {
    if (list->mBuffers[n].mData)
      g_free (list->mBuffers[n].mData);
  }

  g_free (list);
}

typedef struct
{
  GMutex *lock;
  GCond *cond;
} PropertyMutex;

static OSStatus
_audio_stream_format_listener (AudioObjectID inObjectID,
    UInt32 inNumberAddresses,
    const AudioObjectPropertyAddress inAddresses[], void *inClientData)
{
  OSStatus status = noErr;
  guint i;
  PropertyMutex *prop_mutex = inClientData;

  for (i = 0; i < inNumberAddresses; i++) {
    if (inAddresses[i].mSelector == kAudioStreamPropertyPhysicalFormat) {
      g_mutex_lock (prop_mutex->lock);
      g_cond_signal (prop_mutex->cond);
      g_mutex_unlock (prop_mutex->lock);
      break;
    }
  }
  return (status);
}

static gboolean
_audio_stream_change_format (AudioStreamID stream_id,
    AudioStreamBasicDescription format)
{
  OSStatus status = noErr;
  gint i;
  gboolean ret = FALSE;
  AudioStreamBasicDescription cformat;
  PropertyMutex prop_mutex;

  AudioObjectPropertyAddress formatAddress = {
    kAudioStreamPropertyPhysicalFormat,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };

  GST_DEBUG ("setting stream format: " CORE_AUDIO_FORMAT,
      CORE_AUDIO_FORMAT_ARGS (format));

  /* Condition because SetProperty is asynchronous */
  prop_mutex.lock = g_mutex_new ();
  prop_mutex.cond = g_cond_new ();

  g_mutex_lock (prop_mutex.lock);

  /* Install the property listener to serialize the operations */
  status = AudioObjectAddPropertyListener (stream_id, &formatAddress,
      _audio_stream_format_listener, (void *) &prop_mutex);
  if (status != noErr) {
    GST_ERROR ("AudioObjectAddPropertyListener failed: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
    goto done;
  }

  /* Change the format */
  if (!_audio_stream_set_current_format (stream_id, format)) {
    goto done;
  }

  /* The AudioObjectSetProperty is not only asynchronous
   * it is also not atomic in its behaviour.
   * Therefore we check 4 times before we really give up. */
  for (i = 0; i < 4; i++) {
    GTimeVal timeout;

    g_get_current_time (&timeout);
    g_time_val_add (&timeout, 250000);

    if (!g_cond_timed_wait (prop_mutex.cond, prop_mutex.lock, &timeout)) {
      GST_LOG ("timeout...");
    }

    if (_audio_stream_get_current_format (stream_id, &cformat)) {
      GST_DEBUG ("current stream format: " CORE_AUDIO_FORMAT,
          CORE_AUDIO_FORMAT_ARGS (cformat));

      if (cformat.mSampleRate == format.mSampleRate &&
          cformat.mFormatID == format.mFormatID &&
          cformat.mFramesPerPacket == format.mFramesPerPacket) {
        /* The right format is now active */
        break;
      }
    }
  }

  if (cformat.mSampleRate != format.mSampleRate ||
      cformat.mFormatID != format.mFormatID ||
      cformat.mFramesPerPacket != format.mFramesPerPacket) {
    goto done;
  }

  ret = TRUE;

done:
  /* Removing the property listener */
  status = AudioObjectRemovePropertyListener (stream_id,
      &formatAddress, _audio_stream_format_listener, (void *) &prop_mutex);
  if (status != noErr) {
    GST_ERROR ("AudioObjectRemovePropertyListener failed: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
  }
  /* Destroy the lock and condition */
  g_mutex_unlock (prop_mutex.lock);
  g_mutex_free (prop_mutex.lock);
  g_cond_free (prop_mutex.cond);

  return ret;
}

static OSStatus
_audio_stream_hardware_changed_listener (AudioObjectID inObjectID,
    UInt32 inNumberAddresses,
    const AudioObjectPropertyAddress inAddresses[], void *inClientData)
{
  OSStatus status = noErr;
  guint i;
  GstOsxRingBuffer *osxbuf = inClientData;

  for (i = 0; i < inNumberAddresses; i++) {
    if (inAddresses[i].mSelector == kAudioDevicePropertyDeviceHasChanged) {
      if (!_audio_device_is_spdif_avail (osxbuf->device_id)) {
        GstOsxAudioSink *sink = GST_OSX_AUDIO_SINK (GST_OBJECT_PARENT (osxbuf));
        GST_ELEMENT_ERROR (sink, RESOURCE, FAILED,
            ("SPDIF output no longer available"),
            ("Audio device is reporting that SPDIF output isn't available"));
      }
      break;
    }
  }
  return (status);
}

static gboolean
gst_osx_ring_buffer_monitorize_spdif (GstOsxRingBuffer * osxbuf)
{
  OSStatus status = noErr;
  gboolean ret = TRUE;

  AudioObjectPropertyAddress propAddress = {
    kAudioDevicePropertyDeviceHasChanged,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };

  /* Install the property listener */
  status = AudioObjectAddPropertyListener (osxbuf->device_id,
      &propAddress, _audio_stream_hardware_changed_listener, (void *) osxbuf);
  if (status != noErr) {
    GST_ERROR ("AudioObjectAddPropertyListener failed: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
    ret = FALSE;
  }

  return ret;
}

static gboolean
gst_osx_ring_buffer_unmonitorize_spdif (GstOsxRingBuffer * osxbuf)
{
  OSStatus status = noErr;
  gboolean ret = TRUE;

  AudioObjectPropertyAddress propAddress = {
    kAudioDevicePropertyDeviceHasChanged,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };

  /* Remove the property listener */
  status = AudioObjectRemovePropertyListener (osxbuf->device_id,
      &propAddress, _audio_stream_hardware_changed_listener, (void *) osxbuf);
  if (status != noErr) {
    GST_ERROR ("AudioObjectRemovePropertyListener failed: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
    ret = FALSE;
  }

  return ret;
}

static gboolean
gst_osx_ring_buffer_open_spdif (GstOsxRingBuffer * osxbuf)
{
  gboolean res = FALSE;
  pid_t hog_pid, own_pid = getpid ();

  /* We need the device in exclusive and disable the mixing */
  hog_pid = _audio_device_get_hog (osxbuf->device_id);

  if (hog_pid != -1 && hog_pid != own_pid) {
    GST_DEBUG_OBJECT (osxbuf,
        "device is currently in use by another application");
    goto done;
  }

  if (_audio_device_set_hog (osxbuf->device_id, own_pid)) {
    osxbuf->hog_pid = own_pid;
  }

  if (_audio_device_set_mixing (osxbuf->device_id, FALSE)) {
    GST_DEBUG_OBJECT (osxbuf, "disabled mixing on the device");
    osxbuf->disabled_mixing = TRUE;
  }

  res = TRUE;
done:
  return res;
}

static gboolean
gst_osx_ring_buffer_close_spdif (GstOsxRingBuffer * osxbuf)
{
  pid_t hog_pid;

  gst_osx_ring_buffer_unmonitorize_spdif (osxbuf);

  if (osxbuf->revert_format) {
    if (!_audio_stream_change_format (osxbuf->stream_id,
            osxbuf->original_format)) {
      GST_WARNING ("Format revert failed");
    }
    osxbuf->revert_format = FALSE;
  }

  if (osxbuf->disabled_mixing) {
    _audio_device_set_mixing (osxbuf->device_id, TRUE);
    osxbuf->disabled_mixing = FALSE;
  }

  if (osxbuf->hog_pid != -1) {
    hog_pid = _audio_device_get_hog (osxbuf->device_id);
    if (hog_pid == getpid ()) {
      if (_audio_device_set_hog (osxbuf->device_id, -1)) {
        osxbuf->hog_pid = -1;
      }
    }
  }

  return TRUE;
}

static OSStatus
gst_osx_ring_buffer_io_proc_spdif (AudioDeviceID inDevice,
    const AudioTimeStamp * inNow,
    const void *inInputData,
    const AudioTimeStamp * inTimestamp,
    AudioBufferList * bufferList,
    const AudioTimeStamp * inOutputTime, GstOsxRingBuffer * osxbuf)
{
  OSStatus status;

  status = osxbuf->element->io_proc (osxbuf, NULL, inTimestamp, 0, 0,
      bufferList);

  return status;
}

static gboolean
gst_osx_ring_buffer_acquire_spdif (GstOsxRingBuffer * osxbuf,
    AudioStreamBasicDescription format)
{
  AudioStreamID *streams = NULL;
  gint i, j, nstreams = 0;
  gboolean ret = FALSE;

  if (!gst_osx_ring_buffer_open_spdif (osxbuf))
    goto done;

  streams = _audio_device_get_streams (osxbuf->device_id, &nstreams);

  for (i = 0; i < nstreams; i++) {
    AudioStreamRangedDescription *formats = NULL;
    gint nformats = 0;

    formats = _audio_stream_get_formats (streams[i], &nformats);

    if (formats) {
      gboolean is_spdif = FALSE;

      /* Check if one of the supported formats is a digital format */
      for (j = 0; j < nformats; j++) {
        if (CORE_AUDIO_FORMAT_IS_SPDIF (formats[j])) {
          is_spdif = TRUE;
          break;
        }
      }

      if (is_spdif) {
        /* if this stream supports a digital (cac3) format,
         * then go set it. */
        gint requested_rate_format = -1;
        gint current_rate_format = -1;
        gint backup_rate_format = -1;

        osxbuf->stream_id = streams[i];
        osxbuf->stream_idx = i;

        if (!osxbuf->revert_format) {
          if (!_audio_stream_get_current_format (osxbuf->stream_id,
                  &osxbuf->original_format)) {
            GST_WARNING ("format could not be saved");
            g_free (formats);
            continue;
          }
          osxbuf->revert_format = TRUE;
        }

        for (j = 0; j < nformats; j++) {
          if (CORE_AUDIO_FORMAT_IS_SPDIF (formats[j])) {
            GST_LOG ("found stream format: " CORE_AUDIO_FORMAT,
                CORE_AUDIO_FORMAT_ARGS (formats[j].mFormat));

            if (formats[j].mFormat.mSampleRate == format.mSampleRate) {
              requested_rate_format = j;
              break;
            } else if (formats[j].mFormat.mSampleRate ==
                osxbuf->original_format.mSampleRate) {
              current_rate_format = j;
            } else {
              if (backup_rate_format < 0 ||
                  formats[j].mFormat.mSampleRate >
                  formats[backup_rate_format].mFormat.mSampleRate) {
                backup_rate_format = j;
              }
            }
          }
        }

        if (requested_rate_format >= 0) {
          /* We prefer to output at the rate of the original audio */
          osxbuf->stream_format = formats[requested_rate_format].mFormat;
        } else if (current_rate_format >= 0) {
          /* If not possible, we will try to use the current rate */
          osxbuf->stream_format = formats[current_rate_format].mFormat;
        } else {
          /* And if we have to, any digital format will be just
           * fine (highest rate possible) */
          osxbuf->stream_format = formats[backup_rate_format].mFormat;
        }
      }
      g_free (formats);
    }
  }
  g_free (streams);

  GST_DEBUG ("original stream format: " CORE_AUDIO_FORMAT,
      CORE_AUDIO_FORMAT_ARGS (osxbuf->original_format));

  if (!_audio_stream_change_format (osxbuf->stream_id, osxbuf->stream_format))
    goto done;

  GST_DEBUG_OBJECT (osxbuf, "osx ring buffer acquired");

  ret = TRUE;

done:
  return ret;
}

static gboolean
gst_osx_ring_buffer_acquire_analog (GstOsxRingBuffer * osxbuf,
    AudioStreamBasicDescription format, GstCaps * caps)
{
  /* Configure the output stream and allocate ringbuffer memory */
  AudioChannelLayout *layout = NULL;
  OSStatus status;
  UInt32 propertySize;
  int channels = format.mChannelsPerFrame;
  int layoutSize;
  int element;
  int i;
  AudioUnitScope scope;
  gboolean ret = FALSE;
  GstStructure *structure;
  GstAudioChannelPosition *positions;
  UInt32 frameSize;

  /* Describe channels */
  layoutSize = sizeof (AudioChannelLayout) +
      channels * sizeof (AudioChannelDescription);
  layout = g_malloc (layoutSize);

  structure = gst_caps_get_structure (caps, 0);
  positions = gst_audio_get_channel_positions (structure);

  layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
  layout->mChannelBitmap = 0;   /* Not used */
  layout->mNumberChannelDescriptions = channels;
  for (i = 0; i < channels; i++) {
    if (positions) {
      layout->mChannelDescriptions[i].mChannelLabel =
          gst_audio_channel_position_to_coreaudio_channel_label (positions[i],
          i);
    } else {
      /* Discrete channel numbers are ORed into this */
      layout->mChannelDescriptions[i].mChannelLabel =
          kAudioChannelLabel_Discrete_0 | i;
    }

    /* Others unused */
    layout->mChannelDescriptions[i].mChannelFlags = 0;
    layout->mChannelDescriptions[i].mCoordinates[0] = 0.f;
    layout->mChannelDescriptions[i].mCoordinates[1] = 0.f;
    layout->mChannelDescriptions[i].mCoordinates[2] = 0.f;
  }

  if (positions) {
    g_free (positions);
    positions = NULL;
  }

  GST_DEBUG_OBJECT (osxbuf, "Setting format for AudioUnit");

  scope = osxbuf->is_src ? kAudioUnitScope_Output : kAudioUnitScope_Input;
  element = osxbuf->is_src ? 1 : 0;

  propertySize = sizeof (AudioStreamBasicDescription);
  status = AudioUnitSetProperty (osxbuf->audiounit,
      kAudioUnitProperty_StreamFormat, scope, element, &format, propertySize);

  if (status) {
    GST_WARNING_OBJECT (osxbuf,
        "Failed to set audio description: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
    goto done;
  }

  if (layoutSize) {
    status = AudioUnitSetProperty (osxbuf->audiounit,
        kAudioUnitProperty_AudioChannelLayout,
        scope, element, layout, layoutSize);
    if (status) {
      GST_WARNING_OBJECT (osxbuf,
          "Failed to set output channel layout: %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (status));
      goto done;
    }
  }

  /* create AudioBufferList needed for recording */
  if (osxbuf->is_src) {
    propertySize = sizeof (frameSize);
    status = AudioUnitGetProperty (osxbuf->audiounit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0,   /* N/A for global */
        &frameSize, &propertySize);

    if (status) {
      GST_WARNING_OBJECT (osxbuf, "Failed to get frame size: %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
      goto done;
    }

    osxbuf->recBufferList = buffer_list_alloc (channels,
        frameSize * format.mBytesPerFrame);
  }

  /* Specify which device we're using. */
  GST_DEBUG_OBJECT (osxbuf, "Bind AudioUnit to device %d",
      (int) osxbuf->device_id);
  status = AudioUnitSetProperty (osxbuf->audiounit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,  /* N/A for global */
      &osxbuf->device_id, sizeof (AudioDeviceID));
  if (status) {
    GST_ERROR_OBJECT (osxbuf, "Failed binding to device: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
    goto audiounit_error;
  }

  /* Initialize the AudioUnit */
  status = AudioUnitInitialize (osxbuf->audiounit);
  if (status) {
    GST_ERROR_OBJECT (osxbuf, "Failed to initialise AudioUnit: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
    goto audiounit_error;
  }

  GST_DEBUG_OBJECT (osxbuf, "osx ring buffer acquired");

  ret = TRUE;

done:
  g_free (layout);
  return ret;

audiounit_error:
  if (osxbuf->recBufferList) {
    buffer_list_free (osxbuf->recBufferList);
    osxbuf->recBufferList = NULL;
  }
  return ret;
}

static gboolean
gst_osx_ring_buffer_acquire (GstRingBuffer * buf, GstRingBufferSpec * spec)
{
  gboolean ret = FALSE;
  GstOsxRingBuffer *osxbuf;
  AudioStreamBasicDescription format;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  if (RINGBUFFER_IS_SPDIF (spec->type)) {
    format.mFormatID = kAudioFormat60958AC3;
    format.mSampleRate = (double) spec->rate;
    format.mChannelsPerFrame = 2;
    format.mFormatFlags = kAudioFormatFlagIsSignedInteger |
        kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonMixable;
    format.mBytesPerFrame = 0;
    format.mBitsPerChannel = 16;
    format.mBytesPerPacket = 6144;
    format.mFramesPerPacket = 1536;
    format.mReserved = 0;
    spec->segsize = 6144;
    spec->segtotal = 10;
    osxbuf->is_passthrough = TRUE;
  } else {
    int width, depth;
    /* Fill out the audio description we're going to be using */
    format.mFormatID = kAudioFormatLinearPCM;
    format.mSampleRate = (double) spec->rate;
    format.mChannelsPerFrame = spec->channels;
    if (spec->type == GST_BUFTYPE_FLOAT) {
      format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
      width = depth = spec->width;
    } else {
      format.mFormatFlags = kAudioFormatFlagIsSignedInteger;
      width = spec->width;
      depth = spec->depth;
      if (width == depth) {
        format.mFormatFlags |= kAudioFormatFlagIsPacked;
      } else {
        format.mFormatFlags |= kAudioFormatFlagIsAlignedHigh;
      }
      if (spec->bigend) {
        format.mFormatFlags |= kAudioFormatFlagIsBigEndian;
      }
    }
    format.mBytesPerFrame = spec->channels * (width >> 3);
    format.mBitsPerChannel = depth;
    format.mBytesPerPacket = spec->channels * (width >> 3);
    format.mFramesPerPacket = 1;
    format.mReserved = 0;
    spec->segsize =
        (spec->latency_time * spec->rate / G_USEC_PER_SEC) *
        spec->bytes_per_sample;
    spec->segtotal = spec->buffer_time / spec->latency_time;
    osxbuf->stream_idx = 0;
    osxbuf->is_passthrough = FALSE;
  }

  GST_DEBUG_OBJECT (osxbuf, "Format: " CORE_AUDIO_FORMAT,
      CORE_AUDIO_FORMAT_ARGS (format));

  buf->data = gst_buffer_new_and_alloc (spec->segtotal * spec->segsize);
  memset (GST_BUFFER_DATA (buf->data), 0, GST_BUFFER_SIZE (buf->data));

  if (osxbuf->is_passthrough) {
    ret = gst_osx_ring_buffer_acquire_spdif (osxbuf, format);
    if (ret) {
      gst_osx_ring_buffer_monitorize_spdif (osxbuf);
    }
  } else {
    ret = gst_osx_ring_buffer_acquire_analog (osxbuf, format, spec->caps);
  }

  if (!ret) {
    gst_buffer_unref (buf->data);
    buf->data = NULL;
  }

  osxbuf->segoffset = 0;

  return ret;
}

static gboolean
gst_osx_ring_buffer_release (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  AudioUnitUninitialize (osxbuf->audiounit);

  gst_buffer_unref (buf->data);
  buf->data = NULL;

  if (osxbuf->recBufferList) {
    buffer_list_free (osxbuf->recBufferList);
    osxbuf->recBufferList = NULL;
  }

  return TRUE;
}

static OSStatus
gst_osx_ring_buffer_render_notify (GstOsxRingBuffer * osxbuf,
    AudioUnitRenderActionFlags * ioActionFlags,
    const AudioTimeStamp * inTimeStamp,
    unsigned int inBusNumber,
    unsigned int inNumberFrames, AudioBufferList * ioData)
{
  /* Before rendering a frame, we get the PreRender notification.
   * Here, we detach the RenderCallback if we've been paused.
   *
   * This is necessary (rather than just directly detaching it) to
   * work around some thread-safety issues in CoreAudio
   */
  if ((*ioActionFlags) & kAudioUnitRenderAction_PreRender) {
    if (osxbuf->io_proc_needs_deactivation) {
      gst_osx_ring_buffer_remove_render_callback (osxbuf);
    }
  }

  return noErr;
}

static void
gst_osx_ring_buffer_remove_render_callback (GstOsxRingBuffer * osxbuf)
{
  AURenderCallbackStruct input;
  OSStatus status;

  /* Deactivate the render callback by calling SetRenderCallback
   * with a NULL inputProc.
   */
  input.inputProc = NULL;
  input.inputProcRefCon = NULL;

  status = AudioUnitSetProperty (osxbuf->audiounit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Global, 0,    /* N/A for global */
      &input, sizeof (input));

  if (status) {
    GST_WARNING_OBJECT (osxbuf, "Failed to remove render callback %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
  }

  /* Remove the RenderNotify too */
  status = AudioUnitRemoveRenderNotify (osxbuf->audiounit,
      (AURenderCallback) gst_osx_ring_buffer_render_notify, osxbuf);

  if (status) {
    GST_WARNING_OBJECT (osxbuf,
        "Failed to remove render notify callback %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
  }

  /* We're deactivated.. */
  osxbuf->io_proc_needs_deactivation = FALSE;
  osxbuf->io_proc_active = FALSE;
}

static gboolean
gst_osx_ring_buffer_io_proc_start (GstOsxRingBuffer * osxbuf)
{
  OSStatus status;
  AURenderCallbackStruct input;
  AudioUnitPropertyID callback_type;

  GST_DEBUG ("osx ring buffer start ioproc: %p device_id %lu",
      osxbuf->element->io_proc, (gulong) osxbuf->device_id);
  if (!osxbuf->io_proc_active) {
    callback_type = osxbuf->is_src ?
        kAudioOutputUnitProperty_SetInputCallback :
        kAudioUnitProperty_SetRenderCallback;

    input.inputProc = (AURenderCallback) osxbuf->element->io_proc;
    input.inputProcRefCon = osxbuf;

    status = AudioUnitSetProperty (osxbuf->audiounit, callback_type, kAudioUnitScope_Global, 0, /* N/A for global */
        &input, sizeof (input));

    if (status) {
      GST_ERROR ("AudioUnitSetProperty failed: %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (status));
      return FALSE;
    }
    // ### does it make sense to do this notify stuff for input mode?
    status = AudioUnitAddRenderNotify (osxbuf->audiounit,
        (AURenderCallback) gst_osx_ring_buffer_render_notify, osxbuf);

    if (status) {
      GST_ERROR ("AudioUnitAddRenderNotify failed %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
      return FALSE;
    }

    osxbuf->io_proc_active = TRUE;
  }

  osxbuf->io_proc_needs_deactivation = FALSE;

  status = AudioOutputUnitStart (osxbuf->audiounit);
  if (status) {
    GST_ERROR ("AudioOutputUnitStart failed: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_osx_ring_buffer_io_proc_stop (GstOsxRingBuffer * osxbuf)
{
  OSErr status;

  GST_DEBUG ("osx ring buffer stop ioproc: %p device_id %lu",
      osxbuf->element->io_proc, (gulong) osxbuf->device_id);

  status = AudioOutputUnitStop (osxbuf->audiounit);
  if (status) {
    GST_WARNING ("AudioOutputUnitStop failed: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
  }
  // ###: why is it okay to directly remove from here but not from pause() ?
  if (osxbuf->io_proc_active) {
    gst_osx_ring_buffer_remove_render_callback (osxbuf);
  }
  return TRUE;
}

static void
gst_osx_ring_buffer_remove_render_spdif_callback (GstOsxRingBuffer * osxbuf)
{
  OSStatus status;

  /* Deactivate the render callback by calling
   * AudioDeviceDestroyIOProcID */
  status = AudioDeviceDestroyIOProcID (osxbuf->device_id, osxbuf->procID);
  if (status != noErr) {
    GST_ERROR ("AudioDeviceDestroyIOProcID failed: %"
        GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
  }

  GST_DEBUG ("osx ring buffer removed ioproc ID: %p device_id %lu",
      osxbuf->procID, (gulong) osxbuf->device_id);

  /* We're deactivated.. */
  osxbuf->procID = 0;
  osxbuf->io_proc_needs_deactivation = FALSE;
  osxbuf->io_proc_active = FALSE;
}

static gboolean
gst_osx_ring_buffer_io_proc_spdif_start (GstOsxRingBuffer * osxbuf)
{
  OSErr status;

  GST_DEBUG ("osx ring buffer start ioproc ID: %p device_id %lu",
      osxbuf->procID, (gulong) osxbuf->device_id);

  if (!osxbuf->io_proc_active) {
    /* Add IOProc callback */
    status = AudioDeviceCreateIOProcID (osxbuf->device_id,
        (AudioDeviceIOProc) gst_osx_ring_buffer_io_proc_spdif,
        (void *) osxbuf, &osxbuf->procID);
    if (status != noErr) {
      GST_ERROR ("AudioDeviceCreateIOProcID failed: %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
      return FALSE;
    }
    osxbuf->io_proc_active = TRUE;
  }

  osxbuf->io_proc_needs_deactivation = FALSE;

  /* Start device */
  status = AudioDeviceStart (osxbuf->device_id, osxbuf->procID);
  if (status != noErr) {
    GST_ERROR ("AudioDeviceStart failed: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_osx_ring_buffer_io_proc_spdif_stop (GstOsxRingBuffer * osxbuf)
{
  OSErr status;

  /* Stop device */
  status = AudioDeviceStop (osxbuf->device_id, osxbuf->procID);
  if (status != noErr) {
    GST_ERROR ("AudioDeviceStop failed: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (status));
  }

  GST_DEBUG ("osx ring buffer stop ioproc ID: %p device_id %lu",
      osxbuf->procID, (gulong) osxbuf->device_id);

  if (osxbuf->io_proc_active) {
    gst_osx_ring_buffer_remove_render_spdif_callback (osxbuf);
  }

  gst_osx_ring_buffer_close_spdif (osxbuf);

  return TRUE;
}

static gboolean
gst_osx_ring_buffer_start (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  if (osxbuf->is_passthrough) {
    return gst_osx_ring_buffer_io_proc_spdif_start (osxbuf);
  } else {
    return gst_osx_ring_buffer_io_proc_start (osxbuf);
  }
}

static gboolean
gst_osx_ring_buffer_pause (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf = GST_OSX_RING_BUFFER (buf);

  if (osxbuf->is_passthrough) {
    GST_DEBUG ("osx ring buffer pause ioproc ID: %p device_id %lu",
        osxbuf->procID, (gulong) osxbuf->device_id);

    if (osxbuf->io_proc_active) {
      gst_osx_ring_buffer_remove_render_spdif_callback (osxbuf);
    }
  } else {
    GST_DEBUG ("osx ring buffer pause ioproc: %p device_id %lu",
        osxbuf->element->io_proc, (gulong) osxbuf->device_id);
    if (osxbuf->io_proc_active) {
      /* CoreAudio isn't threadsafe enough to do this here;
       * we must deactivate the render callback elsewhere. See:
       * http://lists.apple.com/archives/Coreaudio-api/2006/Mar/msg00010.html
       */
      osxbuf->io_proc_needs_deactivation = TRUE;
    }
  }
  return TRUE;
}


static gboolean
gst_osx_ring_buffer_stop (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  if (osxbuf->is_passthrough) {
    gst_osx_ring_buffer_io_proc_spdif_stop (osxbuf);
  } else {
    gst_osx_ring_buffer_io_proc_stop (osxbuf);
  }

  return TRUE;
}

static guint
gst_osx_ring_buffer_delay (GstRingBuffer * buf)
{
  double latency;
  UInt32 size = sizeof (double);
  GstOsxRingBuffer *osxbuf;
  OSStatus status;
  guint samples;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  if (osxbuf->is_passthrough) {
    samples = _audio_device_get_latency (osxbuf->device_id);
    samples += _audio_stream_get_latency (osxbuf->stream_id);
    latency = (double) samples / GST_RING_BUFFER (buf)->spec.rate;
  } else {
    status = AudioUnitGetProperty (osxbuf->audiounit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0,    /* N/A for global */
        &latency, &size);

    if (status) {
      GST_WARNING_OBJECT (buf, "Failed to get latency: %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (status));
      return 0;
    }

    samples = latency * GST_RING_BUFFER (buf)->spec.rate;
  }
  GST_DEBUG_OBJECT (buf, "Got latency: %f seconds -> %d samples",
      latency, samples);
  return samples;
}
