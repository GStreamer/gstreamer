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

/*
 * This file defines APIs to convert C++ AudioFlinder/AudioTrack
 * interface to C interface
 */
#ifndef __AUDIOFLINGER_WRAPPER_H__
#define __AUDIOFLINGER_WRAPPER_H__

#define LATE 0x80000002

#ifdef __cplusplus
extern "C" {
#endif
typedef void* AudioFlingerDeviceHandle;

AudioFlingerDeviceHandle audioflinger_device_create();

AudioFlingerDeviceHandle audioflinger_device_open(void* audio_sink);

int audioflinger_device_set (AudioFlingerDeviceHandle handle, 
  int streamType, int channelCount, uint32_t sampleRate, int bufferCount);

void audioflinger_device_release(AudioFlingerDeviceHandle handle);

void audioflinger_device_start(AudioFlingerDeviceHandle handle);

void audioflinger_device_stop(AudioFlingerDeviceHandle handle);

ssize_t  audioflinger_device_write(AudioFlingerDeviceHandle handle, 
    const void* buffer, size_t size);

void audioflinger_device_flush(AudioFlingerDeviceHandle handle);

void audioflinger_device_pause(AudioFlingerDeviceHandle handle);

void audioflinger_device_mute(AudioFlingerDeviceHandle handle, int mute);

int  audioflinger_device_muted(AudioFlingerDeviceHandle handle);

void audioflinger_device_set_volume(AudioFlingerDeviceHandle handle, 
    float left, float right);

int audioflinger_device_frameCount(AudioFlingerDeviceHandle handle);

int audioflinger_device_frameSize(AudioFlingerDeviceHandle handle);

int64_t audioflinger_device_latency(AudioFlingerDeviceHandle handle);

int audioflinger_device_format(AudioFlingerDeviceHandle handle);

int audioflinger_device_channelCount(AudioFlingerDeviceHandle handle);

uint32_t  audioflinger_device_sampleRate(AudioFlingerDeviceHandle handle);

int audioflinger_device_obtain_buffer (AudioFlingerDeviceHandle handle,
    void **buffer_handle, int8_t **data, size_t *samples, uint64_t offset);
void audioflinger_device_release_buffer (AudioFlingerDeviceHandle handle,
    void *buffer_handle);

uint32_t audioflinger_device_get_position (AudioFlingerDeviceHandle handle);


#ifdef __cplusplus
}
#endif

#endif /* __AUDIOFLINGER_WRAPPER_H__ */
