/* GStreamer
 * Copyright (C) <2009> Prajnashi S <prajnashi@gmail.com>
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
#define ENABLE_GST_PLAYER_LOG
#include <media/AudioTrack.h>
#include <utils/Log.h>
#include <AudioFlinger.h>
#include <MediaPlayerInterface.h>
#include <MediaPlayerService.h>
#include "audioflinger_wrapper.h"
#include <glib/glib.h>
//#include <GstLog.h>


#define LOG_NDEBUG 0

#undef LOG_TAG
#define LOG_TAG "audioflinger_wrapper"


using namespace android;


typedef struct _AudioFlingerDevice
{
  AudioTrack* audio_track;
  bool init;
  sp<MediaPlayerBase::AudioSink> audio_sink;
  bool audio_sink_specified;
} AudioFlingerDevice;


/* commonly used macro */
#define AUDIO_FLINGER_DEVICE(handle) ((AudioFlingerDevice*)handle)
#define AUDIO_FLINGER_DEVICE_TRACK(handle) \
    (AUDIO_FLINGER_DEVICE(handle)->audio_track)
#define AUDIO_FLINGER_DEVICE_SINK(handle) \
    (AUDIO_FLINGER_DEVICE(handle)->audio_sink)


AudioFlingerDeviceHandle audioflinger_device_create()
{
  AudioFlingerDevice* audiodev = NULL;
  AudioTrack *audiotr = NULL;

  // create a new instance of AudioFlinger 
  audiodev = new AudioFlingerDevice;
  if (audiodev == NULL) {
    LOGE("Error to create AudioFlingerDevice\n");
    return NULL;
  }

  // create AudioTrack
  audiotr = new AudioTrack ();
  if (audiotr == NULL) {
    LOGE("Error to create AudioTrack\n");
    return NULL;
  }

  audiodev->init = false;
  audiodev->audio_track = (AudioTrack *) audiotr;
  audiodev->audio_sink = 0;
  audiodev->audio_sink_specified = false;
  LOGD("Create AudioTrack successfully %p\n",audiodev);

  return (AudioFlingerDeviceHandle)audiodev;
}

AudioFlingerDeviceHandle audioflinger_device_open(void* audio_sink)
{
  AudioFlingerDevice* audiodev = NULL;

  // audio_sink shall be an MediaPlayerBase::AudioSink instance
  if(audio_sink == NULL)
    return NULL;

  // create a new instance of AudioFlinger 
  audiodev = new AudioFlingerDevice;
  if (audiodev == NULL) {
    LOGE("Error to create AudioFlingerDevice\n");
    return NULL;
  }

  // set AudioSink

  audiodev->audio_sink = (MediaPlayerBase::AudioSink*)audio_sink;
  audiodev->audio_track = NULL;
  audiodev->init = false;
  audiodev->audio_sink_specified = true;
  LOGD("Open AudioSink successfully : %p\n",audiodev);

  return (AudioFlingerDeviceHandle)audiodev;    
}

int audioflinger_device_set (AudioFlingerDeviceHandle handle, 
  int streamType, int channelCount, uint32_t sampleRate, int bufferCount)
{
  status_t status = NO_ERROR;
#ifndef STECONF_ANDROID_VERSION_DONUT
  uint32_t channels = 0;
#endif

  int format = AudioSystem::PCM_16_BIT;

  if (handle == NULL)
      return -1;

  if(AUDIO_FLINGER_DEVICE_TRACK(handle)) {
    // bufferCount is not the number of internal buffer, but the internal
    // buffer size
#ifdef STECONF_ANDROID_VERSION_DONUT
    status = AUDIO_FLINGER_DEVICE_TRACK(handle)->set(streamType, sampleRate, 
        format, channelCount);
    LOGD("SET : handle : %p : Set AudioTrack, status: %d, streamType: %d, sampleRate: %d, "
        "channelCount: %d, bufferCount: %d\n",handle, status, streamType, sampleRate, 
        channelCount, bufferCount);
#else 
	switch (channelCount) 
	{
	case 1:
		channels = AudioSystem::CHANNEL_OUT_FRONT_LEFT;
		break;
	case 2:
		channels = AudioSystem::CHANNEL_OUT_STEREO;
		break;
	case 0: 	
	default:
		channels = 0;
		break;
	}
	status = AUDIO_FLINGER_DEVICE_TRACK(handle)->set(streamType, sampleRate, 
		format, channels/*, bufferCount*/);
	LOGD("SET handle : %p : Set AudioTrack, status: %d, streamType: %d, sampleRate: %d, "
         "channelCount: %d(%d), bufferCount: %d\n",handle, status, streamType, sampleRate, 
         channelCount, channels, bufferCount);
#endif	
    AUDIO_FLINGER_DEVICE_TRACK(handle)->setPositionUpdatePeriod(bufferCount);
    
  }
  else if(AUDIO_FLINGER_DEVICE_SINK(handle).get()) {
#ifdef STECONF_ANDROID_VERSION_DONUT
    status = AUDIO_FLINGER_DEVICE_SINK(handle)->open(sampleRate, channelCount, 
        format/*, bufferCount*/); //SDA

    LOGD("OPEN : handle : %p : Set AudioSink, status: %d, streamType: %d, sampleRate: %d," 
        "channelCount: %d, bufferCount: %d\n", handle, status, streamType, sampleRate, 
        channelCount, bufferCount);	
#else 
  	channels = channelCount;
    status = AUDIO_FLINGER_DEVICE_SINK(handle)->open(sampleRate, channels, 
        format/*, bufferCount*/);
    LOGD("OPEN handle : %p : Set AudioSink, status: %d, streamType: %d, sampleRate: %d," 
        "channelCount: %d(%d), bufferCount: %d\n", handle, status, streamType, sampleRate, 
        channelCount, channels, bufferCount);
#endif	
	AUDIO_FLINGER_DEVICE_TRACK(handle) = (AudioTrack *)(AUDIO_FLINGER_DEVICE_SINK(handle)->getTrack());
	if(AUDIO_FLINGER_DEVICE_TRACK(handle)) {
		AUDIO_FLINGER_DEVICE_TRACK(handle)->setPositionUpdatePeriod(bufferCount);
	}
  }

  if (status != NO_ERROR) 
    return -1;

  AUDIO_FLINGER_DEVICE(handle)->init = true;

  return 0;
}

void audioflinger_device_release (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL)
    return;

  LOGD("Enter\n");
  if(! AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified ) {
  	if (AUDIO_FLINGER_DEVICE_TRACK(handle) )  {
    LOGD("handle : %p Release AudioTrack\n", handle);
    delete AUDIO_FLINGER_DEVICE_TRACK(handle);
  }
  }
  if (AUDIO_FLINGER_DEVICE_SINK(handle).get()) {
    LOGD("handle : %p Release AudioSink\n", handle);
    AUDIO_FLINGER_DEVICE_SINK(handle).clear();	
    AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified = false;	
  }
  
  delete AUDIO_FLINGER_DEVICE(handle);
}


void audioflinger_device_start (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return;

  LOGD("handle : %p Start Device\n", handle);

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    AUDIO_FLINGER_DEVICE_SINK(handle)->start();
  }
  else {
	AUDIO_FLINGER_DEVICE_TRACK(handle)->start();	
  }
}

void audioflinger_device_stop (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return;
  
  LOGD("handle : %p Stop Device\n", handle);

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    AUDIO_FLINGER_DEVICE_SINK(handle)->stop();
  }
  else {
	AUDIO_FLINGER_DEVICE_TRACK(handle)->stop();	
  }

}

void audioflinger_device_flush (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return;
  
  LOGD("handle : %p Flush device\n", handle);

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    AUDIO_FLINGER_DEVICE_SINK(handle)->flush();
  }
  else {
	AUDIO_FLINGER_DEVICE_TRACK(handle)->flush();	
  }
}

void audioflinger_device_pause (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return;

  LOGD("handle : %p Pause Device\n", handle);


  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    AUDIO_FLINGER_DEVICE_SINK(handle)->pause();
  }
  else {
	AUDIO_FLINGER_DEVICE_TRACK(handle)->pause();	
  }

}

void audioflinger_device_mute (AudioFlingerDeviceHandle handle, int mute)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return;
  
  LOGD("handle : %p Mute Device\n", handle);

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    // do nothing here, because the volume/mute is set in media service layer
  }
  else  if (AUDIO_FLINGER_DEVICE_TRACK(handle)) {
	AUDIO_FLINGER_DEVICE_TRACK(handle)->mute((bool)mute);
  }
}

int audioflinger_device_muted (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
      return -1;

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    // do nothing here, because the volume/mute is set in media service layer
    return -1;
  }
  else  if (AUDIO_FLINGER_DEVICE_TRACK(handle)) {
	return (int) AUDIO_FLINGER_DEVICE_TRACK(handle)->muted ();
  }
    return -1;  
}


void audioflinger_device_set_volume (AudioFlingerDeviceHandle handle, float left,
    float right)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return;

  LOGD("handle : %p Set volume Device %f,%f\n", handle,left,right);

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    // do nothing here, because the volume/mute is set in media service layer
    return ;
  }
  else  if (AUDIO_FLINGER_DEVICE_TRACK(handle))  {
    AUDIO_FLINGER_DEVICE_TRACK(handle)->setVolume (left, right);
  }
}

ssize_t audioflinger_device_write (AudioFlingerDeviceHandle handle, const void *buffer,
    size_t size)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return -1;

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    return AUDIO_FLINGER_DEVICE_SINK(handle)->write(buffer, size);
  }
  else  if (AUDIO_FLINGER_DEVICE_TRACK(handle))  {
    return AUDIO_FLINGER_DEVICE_TRACK(handle)->write(buffer, size);
  }
#ifndef STECONF_ANDROID_VERSION_DONUT
  return -1;
#endif  
}

int audioflinger_device_frameCount (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return -1;

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    return (int)AUDIO_FLINGER_DEVICE_SINK(handle)->frameCount();
  }
  else  if (AUDIO_FLINGER_DEVICE_TRACK(handle))  {
    return (int)AUDIO_FLINGER_DEVICE_TRACK(handle)->frameCount();
  }
    return -1;  
}

int audioflinger_device_frameSize (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return -1;

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    return (int)AUDIO_FLINGER_DEVICE_SINK(handle)->frameSize();
  }
  else  if (AUDIO_FLINGER_DEVICE_TRACK(handle))  {
    return (int)AUDIO_FLINGER_DEVICE_TRACK(handle)->frameSize();
  }
#ifndef STECONF_ANDROID_VERSION_DONUT
  return -1;
#endif  
}

int64_t audioflinger_device_latency (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return -1;

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    return (int64_t)AUDIO_FLINGER_DEVICE_SINK(handle)->latency();
  }
  else  if (AUDIO_FLINGER_DEVICE_TRACK(handle))  {
    return (int64_t)AUDIO_FLINGER_DEVICE_TRACK(handle)->latency();
  }
   return -1;
}

int audioflinger_device_format (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return -1;

  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    // do nothing here, MediaPlayerBase::AudioSink doesn't provide format()
    // interface
    return -1;
  }
  else  if (AUDIO_FLINGER_DEVICE_TRACK(handle))  {
    return (int)AUDIO_FLINGER_DEVICE_TRACK(handle)->format();
  }
   return -1;
}

int audioflinger_device_channelCount (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return -1;
  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    return (int)AUDIO_FLINGER_DEVICE_SINK(handle)->channelCount();
  }
  else  if (AUDIO_FLINGER_DEVICE_TRACK(handle))  {
    return (int)AUDIO_FLINGER_DEVICE_TRACK(handle)->channelCount();
  }
  return -1;
}

uint32_t audioflinger_device_sampleRate (AudioFlingerDeviceHandle handle)
{
  if (handle == NULL || AUDIO_FLINGER_DEVICE(handle)->init == false)
    return 0;
  if(AUDIO_FLINGER_DEVICE(handle)->audio_sink_specified) {
    // do nothing here, MediaPlayerBase::AudioSink doesn't provide sampleRate()
    // interface
    return -1;
  }
  else  if (AUDIO_FLINGER_DEVICE_TRACK(handle))  {
    	return (int)AUDIO_FLINGER_DEVICE_TRACK(handle)->getSampleRate();
}
  return(-1);
}

int audioflinger_device_obtain_buffer (AudioFlingerDeviceHandle handle,
    void **buffer_handle, int8_t **data, size_t *samples, uint64_t offset)
{
  AudioTrack *track = AUDIO_FLINGER_DEVICE_TRACK (handle);
  status_t res;
  AudioTrack::Buffer *audioBuffer;

  if(track == 0) return(-1);
  audioBuffer = new AudioTrack::Buffer();
  audioBuffer->frameCount = *samples;
  res = track->obtainBufferAtOffset (audioBuffer, offset, -1);
  if (res < 0) {
    delete audioBuffer;

    return (int) res;
  }

  *samples = audioBuffer->frameCount;
  *buffer_handle = static_cast<void *> (audioBuffer);
  *data = audioBuffer->i8;

  return res;
}

void audioflinger_device_release_buffer (AudioFlingerDeviceHandle handle,
    void *buffer_handle)
{
  AudioTrack *track = AUDIO_FLINGER_DEVICE_TRACK (handle);
  AudioTrack::Buffer *audioBuffer = static_cast<AudioTrack::Buffer *>(buffer_handle);
  
  if(track == 0) return;

  track->releaseBuffer (audioBuffer);
  delete audioBuffer;
}

uint32_t audioflinger_device_get_position (AudioFlingerDeviceHandle handle)
{
  status_t status;
  uint32_t ret = -1;
  AudioTrack *track = AUDIO_FLINGER_DEVICE_TRACK (handle);

  if(track == 0) return(-1);

  status = track->getPosition (&ret);

  return ret;
}
