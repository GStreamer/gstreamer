/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosxaudioelement.h: 
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

#ifndef __GST_OSXAUDIO_ELEMENT_H__
#define __GST_OSXAUDIO_ELEMENT_H__

#include <gst/gst.h>
#include <CoreAudio/CoreAudio.h>
#include <pthread.h>

/* debugging category */
GST_DEBUG_CATEGORY_EXTERN (osxaudio_debug);
#define GST_CAT_DEFAULT osxaudio_debug

G_BEGIN_DECLS

OSStatus outputAudioDeviceIOProc(AudioDeviceID inDevice, const AudioTimeStamp *inNow, const AudioBufferList *inInputData, const AudioTimeStamp *inInputTime, AudioBufferList *outOutputData, const AudioTimeStamp *inOutputTime, void *inClientData);

OSStatus inputAudioDeviceIOProc(AudioDeviceID inDevice, const AudioTimeStamp *inNow, const AudioBufferList *inInputData, const AudioTimeStamp *inInputTime, AudioBufferList *outOutputData, const AudioTimeStamp *inOutputTime, void *inClientData);

#define GST_TYPE_OSXAUDIOELEMENT \
  (gst_osxaudioelement_get_type())
#define GST_OSXAUDIOELEMENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSXAUDIOELEMENT,GstOsxAudioElement))
#define GST_OSXAUDIOELEMENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSXAUDIOELEMENT,GstOsxAudioElementClass))
#define GST_IS_OSXAUDIOELEMENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSXAUDIOELEMENT))
#define GST_IS_OSXAUDIOELEMENT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSXAUDIOELEMENT))
#define GST_OSXAUDIOELEMENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_OSXAUDIOELEMENT, GstOsxAudioElementClass))

typedef struct _GstOsxAudioElement GstOsxAudioElement;
typedef struct _GstOsxAudioElementClass GstOsxAudioElementClass;

/* This is large, but best (maybe it should be even larger).
 * CoreAudio supposedly has an internal latency in the order of 2ms */
#define NUM_BUFS 128

struct _GstOsxAudioElement
{
  /* yes, we're a gstelement too */
  GstElement     parent;
  
  pthread_mutex_t buffer_mutex; /* mutex covering buffer variables */
  AudioDeviceID  device_id;

  unsigned char *buffer[NUM_BUFS];
  unsigned int buffer_len;
   
  unsigned int buf_read;
  unsigned int buf_write;
  unsigned int buf_read_pos;
  unsigned int buf_write_pos;
  int full_buffers;
  int buffered_bytes;

};

struct _GstOsxAudioElementClass {
  GstElementClass klass;

  GList		*device_combinations;
};

GType		gst_osxaudioelement_get_type		(void);
int read_buffer(GstOsxAudioElement* osxaudio, unsigned char* data);
int write_buffer(GstOsxAudioElement* osxaudio, unsigned char* data, int len);
G_END_DECLS

#endif /* __GST_OSXAUDIO_ELEMENT_H__ */
