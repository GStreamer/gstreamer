/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosxaudioelement.c: 
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <CoreAudio/CoreAudio.h>
#include <pthread.h>

#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "gstosxaudioelement.h"

enum
{
  ARG_0,
  ARG_DEVICE
};

/* elementfactory information */
static GstElementDetails gst_osxaudioelement_details =
GST_ELEMENT_DETAILS ("Audio Mixer (OSX)",
    "Generic/Audio",
    "Mac OS X audio mixer element",
    "Zaheer Abbas Merali <zaheerabbas at merali.org>");

static void gst_osxaudioelement_base_init (GstOsxAudioElementClass * klass);
static void gst_osxaudioelement_class_init (GstOsxAudioElementClass * klass);

static void gst_osxaudioelement_init (GstOsxAudioElement * osxaudio);
static void gst_osxaudioelement_dispose (GObject * object);

static void gst_osxaudioelement_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_osxaudioelement_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_osxaudioelement_change_state (GstElement *
    element);

static GstElementClass *parent_class = NULL;


GType
gst_osxaudioelement_get_type (void)
{
  static GType osxaudioelement_type = 0;

  if (!osxaudioelement_type) {
    static const GTypeInfo osxaudioelement_info = {
      sizeof (GstOsxAudioElementClass),
      (GBaseInitFunc) gst_osxaudioelement_base_init,
      NULL,
      (GClassInitFunc) gst_osxaudioelement_class_init,
      NULL,
      NULL,
      sizeof (GstOsxAudioElement),
      0,
      (GInstanceInitFunc) gst_osxaudioelement_init
    };

    osxaudioelement_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstOsxAudioElement", &osxaudioelement_info, 0);
  }

  return osxaudioelement_type;
}

static void
gst_osxaudioelement_base_init (GstOsxAudioElementClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  klass->device_combinations = NULL;

  gst_element_class_set_details (element_class, &gst_osxaudioelement_details);
}

static void
gst_osxaudioelement_class_init (GstOsxAudioElementClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE,
      g_param_spec_int ("device", "Device index",
          "Mac OS X CoreAudio Device Index (0 usually)", 0, G_MAXINT, 0,
          G_PARAM_READWRITE));
  gobject_class->set_property = gst_osxaudioelement_set_property;
  gobject_class->get_property = gst_osxaudioelement_get_property;
  gobject_class->dispose = gst_osxaudioelement_dispose;

  gstelement_class->change_state = gst_osxaudioelement_change_state;
}

static void
gst_osxaudioelement_init (GstOsxAudioElement * osxaudio)
{
  OSStatus status;
  UInt32 propertySize;

  pthread_mutex_init (&osxaudio->buffer_mutex, NULL);
  pthread_mutex_unlock (&osxaudio->buffer_mutex);
  propertySize = sizeof (osxaudio->device_id);
  status =
      AudioHardwareGetProperty (kAudioHardwarePropertyDefaultOutputDevice,
      &propertySize, &(osxaudio->device_id));

  if (status) {
    GST_DEBUG ("AudioHardwareGetProperty returned %d\n", (int) status);
  }
  if (osxaudio->device_id == kAudioDeviceUnknown) {
    GST_DEBUG ("AudioHardwareGetProperty: device_id is kAudioDeviceUnknown\n");
  }
  /* get requested buffer length */
  propertySize = sizeof (osxaudio->buffer_len);
  status =
      AudioDeviceGetProperty (osxaudio->device_id, 0, false,
      kAudioDevicePropertyBufferSize, &propertySize, &osxaudio->buffer_len);
  if (status) {
    GST_DEBUG
        ("AudioDeviceGetProperty returned %d when getting kAudioDevicePropertyBufferSize\n",
        (int) status);
  }
  GST_DEBUG ("%5d osxaudio->buffer_len\n", (int) osxaudio->buffer_len);

}

static void
gst_osxaudioelement_dispose (GObject * object)
{
  /* GstOsxAudioElement *osxaudio = (GstOsxAudioElement *) object; */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/* General purpose Ring-buffering routines */
int
write_buffer (GstOsxAudioElement * osxaudio, unsigned char *data, int len)
{
  int len2 = 0;
  int x;

  while (len > 0) {
    if (osxaudio->full_buffers == NUM_BUFS) {
      GST_DEBUG ("Buffer overrun\n");
      break;
    }

    x = osxaudio->buffer_len - osxaudio->buf_write_pos;
    if (x > len)
      x = len;
    memcpy (osxaudio->buffer[osxaudio->buf_write] + osxaudio->buf_write_pos,
        data + len2, x);

    /* accessing common variables, locking mutex */
    pthread_mutex_lock (&osxaudio->buffer_mutex);
    len2 += x;
    len -= x;
    osxaudio->buffered_bytes += x;
    osxaudio->buf_write_pos += x;
    if (osxaudio->buf_write_pos >= osxaudio->buffer_len) {
      /* block is full, find next! */
      osxaudio->buf_write = (osxaudio->buf_write + 1) % NUM_BUFS;
      ++osxaudio->full_buffers;
      osxaudio->buf_write_pos = 0;
    }
    pthread_mutex_unlock (&osxaudio->buffer_mutex);
  }

  return len2;
}

int
read_buffer (GstOsxAudioElement * osxaudio, unsigned char *data)
{
  int len2 = 0;
  int len = osxaudio->buffer_len;
  int x;

  while (len > 0) {
    if (osxaudio->full_buffers == 0) {
      GST_DEBUG ("Buffer underrun\n");
      break;
    }

    x = osxaudio->buffer_len - osxaudio->buf_read_pos;
    if (x > len)
      x = len;
    memcpy (data + len2,
        osxaudio->buffer[osxaudio->buf_read] + osxaudio->buf_read_pos, x);
    len2 += x;
    len -= x;

    /* accessing common variables, locking mutex */
    pthread_mutex_lock (&osxaudio->buffer_mutex);
    osxaudio->buffered_bytes -= x;
    osxaudio->buf_read_pos += x;
    if (osxaudio->buf_read_pos >= osxaudio->buffer_len) {
      /* block is empty, find next! */
      osxaudio->buf_read = (osxaudio->buf_read + 1) % NUM_BUFS;
      --osxaudio->full_buffers;
      osxaudio->buf_read_pos = 0;
    }
    pthread_mutex_unlock (&osxaudio->buffer_mutex);
  }


  return len2;
}

/* The function that the CoreAudio thread calls when it has data */
OSStatus
inputAudioDeviceIOProc (AudioDeviceID inDevice, const AudioTimeStamp * inNow,
    const AudioBufferList * inInputData, const AudioTimeStamp * inInputTime,
    AudioBufferList * outOutputData, const AudioTimeStamp * inOutputTime,
    void *inClientData)
{
  GstOsxAudioElement *osxaudio = GST_OSXAUDIOELEMENT (inClientData);

  write_buffer (GST_OSXAUDIOELEMENT (osxaudio),
      (char *) inInputData->mBuffers[0].mData, osxaudio->buffer_len);

  return 0;
}

/* The function that the CoreAudio thread calls when it wants more data */

OSStatus
outputAudioDeviceIOProc (AudioDeviceID inDevice, const AudioTimeStamp * inNow,
    const AudioBufferList * inInputData, const AudioTimeStamp * inInputTime,
    AudioBufferList * outOutputData, const AudioTimeStamp * inOutputTime,
    void *inClientData)
{
  GstOsxAudioElement *osxaudio = GST_OSXAUDIOELEMENT (inClientData);

  outOutputData->mBuffers[0].mDataByteSize =
      read_buffer (osxaudio, (char *) outOutputData->mBuffers[0].mData);

  return 0;
}

static gboolean
gst_osxaudioelement_open_audio (GstOsxAudioElement * osxaudio, gboolean input)
{
  int i;
  OSErr status;

  GST_INFO ("osxaudioelement: attempting to open sound device");

  /* Allocate ring-buffer memory */
  for (i = 0; i < NUM_BUFS; i++)
    osxaudio->buffer[i] = (unsigned char *) malloc (osxaudio->buffer_len);
  if (input) {
    status =
        AudioDeviceAddIOProc (osxaudio->device_id, inputAudioDeviceIOProc,
        osxaudio);
  } else {
    /* Set the IO proc that CoreAudio will call when it needs data */
    status =
        AudioDeviceAddIOProc (osxaudio->device_id, outputAudioDeviceIOProc,
        osxaudio);
  }
  if (status) {
    GST_DEBUG ("AudioDeviceAddIOProc returned %d\n", (int) status);
    return FALSE;
  }

  pthread_mutex_lock (&osxaudio->buffer_mutex);

  /* reset ring-buffer state */
  osxaudio->buf_read = 0;
  osxaudio->buf_write = 0;
  osxaudio->buf_read_pos = 0;
  osxaudio->buf_write_pos = 0;

  osxaudio->full_buffers = 0;
  osxaudio->buffered_bytes = 0;

  /* zero buffer */
  for (i = 0; i < NUM_BUFS; i++)
    bzero (osxaudio->buffer[i], osxaudio->buffer_len);

  pthread_mutex_unlock (&osxaudio->buffer_mutex);

  return TRUE;
}

static void
gst_osxaudioelement_close_audio (GstOsxAudioElement * osxaudio, gboolean input)
{
  OSErr status;
  int i;

  /* stop callback */
  if (input) {
    status = AudioDeviceStop (osxaudio->device_id, inputAudioDeviceIOProc);
  } else {
    status = AudioDeviceStop (osxaudio->device_id, outputAudioDeviceIOProc);
  }
  if (status)
    GST_DEBUG ("AudioDeviceStop returned %d\n", (int) status);

  if (input) {
    status =
        AudioDeviceRemoveIOProc (osxaudio->device_id, inputAudioDeviceIOProc);
  } else {
    status =
        AudioDeviceRemoveIOProc (osxaudio->device_id, outputAudioDeviceIOProc);
  }
  if (status)
    GST_DEBUG ("AudioDeviceRemoveIOProc " "returned %d\n", (int) status);

  for (i = 0; i < NUM_BUFS; i++)
    free (osxaudio->buffer[i]);

}

static void
gst_osxaudioelement_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstOsxAudioElement *osxaudio = GST_OSXAUDIOELEMENT (object);
  OSStatus status;
  int nDevices;
  UInt32 propertySize;
  int deviceid;
  AudioDeviceID *devids;

  switch (prop_id) {
    case ARG_DEVICE:
      /* check index given is in bounds, if not use default device */
      status = AudioHardwareGetPropertyInfo (kAudioHardwarePropertyDevices,
          &propertySize, NULL);
      nDevices = propertySize / sizeof (AudioDeviceID);
      deviceid = g_value_get_int (value);
      if (deviceid < nDevices) {
        devids = malloc (propertySize);
        status =
            AudioHardwareGetProperty (kAudioHardwarePropertyDevices,
            &propertySize, devids);
        osxaudio->device_id = devids[deviceid];
        free (devids);
      } else {
        GST_DEBUG ("device index %d out of range.  Max index is currently %d\n",
            deviceid, nDevices);
      }
      break;
    default:
      break;
  }
}

static void
gst_osxaudioelement_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstOsxAudioElement *osxaudio = GST_OSXAUDIOELEMENT (object);
  OSStatus status;
  int nDevices;
  UInt32 propertySize;
  AudioDeviceID *devids;
  int i;

  switch (prop_id) {
    case ARG_DEVICE:
      /* figure out what index the current one is */
      status = AudioHardwareGetPropertyInfo (kAudioHardwarePropertyDevices,
          &propertySize, NULL);
      nDevices = propertySize / sizeof (AudioDeviceID);
      devids = malloc (propertySize);
      status =
          AudioHardwareGetProperty (kAudioHardwarePropertyDevices,
          &propertySize, devids);
      for (i = 0; i < nDevices; i++) {
        if (osxaudio->device_id == devids[i])
          break;
      }
      g_value_set_int (value, i);
      free (devids);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_osxaudioelement_change_state (GstElement * element)
{
  GstOsxAudioElement *osxaudio = GST_OSXAUDIOELEMENT (element);
  const GList *padlist;
  gboolean input = TRUE;

  padlist = gst_element_get_pad_list (element);
  if (padlist != NULL) {
    GstPad *firstpad = padlist->data;

    if (GST_PAD_IS_SINK (firstpad)) {
      input = FALSE;
    }
  }
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!gst_osxaudioelement_open_audio (osxaudio, input)) {
        return GST_STATE_FAILURE;
      }
      GST_INFO ("osxaudioelement: opened sound device");
      break;
    case GST_STATE_READY_TO_NULL:
      gst_osxaudioelement_close_audio (osxaudio, input);
      GST_INFO ("osxaudioelement: closed sound device");
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
