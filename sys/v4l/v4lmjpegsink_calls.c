/* G-Streamer hardware MJPEG video sink plugin
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <config.h>
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4lmjpegsink_calls.h"

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ( (caddr_t) -1 )
#endif

#define DEBUG(format, args...) \
	GST_DEBUG_OBJECT (\
		GST_ELEMENT(v4lmjpegsink), \
		"V4LMJPEGSINK: " format, ##args)


/******************************************************
 * gst_v4lmjpegsink_sync_thread()
 *   thread keeps track of played frames
 ******************************************************/

static void *
gst_v4lmjpegsink_sync_thread (void *arg)
{
  GstV4lMjpegSink *v4lmjpegsink = GST_V4LMJPEGSINK (arg);
  gint frame = 0;               /* frame that we're currently syncing on */

  DEBUG ("starting sync thread");

#if 0
  /* Allow easy shutting down by other processes... */
  pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
  pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif

  while (1) {
    g_mutex_lock (v4lmjpegsink->mutex_queued_frames);
    if (!v4lmjpegsink->isqueued_queued_frames[frame]) {
      g_cond_wait (v4lmjpegsink->cond_queued_frames[frame],
          v4lmjpegsink->mutex_queued_frames);
    }
    if (v4lmjpegsink->isqueued_queued_frames[frame] != 1) {
      g_mutex_unlock (v4lmjpegsink->mutex_queued_frames);
      goto end;
    }
    g_mutex_unlock (v4lmjpegsink->mutex_queued_frames);

    DEBUG ("thread-syncing on next frame");
    if (ioctl (GST_V4LELEMENT (v4lmjpegsink)->video_fd, MJPIOC_SYNC,
            &(v4lmjpegsink->bsync)) < 0) {
      GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, SYNC, (NULL),
          ("Failed to sync on frame %d: %s", frame, g_strerror (errno)));
      g_mutex_lock (v4lmjpegsink->mutex_queued_frames);
      v4lmjpegsink->isqueued_queued_frames[frame] = -1;
      g_cond_broadcast (v4lmjpegsink->cond_queued_frames[frame]);
      g_mutex_unlock (v4lmjpegsink->mutex_queued_frames);
      goto end;
    } else {
      /* be sure that we're not confusing */
      if (frame != v4lmjpegsink->bsync.frame) {
        GST_ELEMENT_ERROR (v4lmjpegsink, CORE, TOO_LAZY, (NULL),
            ("Internal error: frame number confusion"));
        goto end;
      }
      g_mutex_lock (v4lmjpegsink->mutex_queued_frames);
      v4lmjpegsink->isqueued_queued_frames[frame] = 0;
      g_cond_broadcast (v4lmjpegsink->cond_queued_frames[frame]);
      g_mutex_unlock (v4lmjpegsink->mutex_queued_frames);
    }

    frame = (frame + 1) % v4lmjpegsink->breq.count;
  }

end:
  DEBUG ("Sync thread got signalled to exit");
  g_thread_exit (NULL);
  return NULL;
}


/******************************************************
 * gst_v4lmjpegsink_queue_frame()
 *   queue a frame for playback
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lmjpegsink_queue_frame (GstV4lMjpegSink * v4lmjpegsink, gint num)
{
  DEBUG ("queueing frame %d", num);

  /* queue on this frame */
  if (ioctl (GST_V4LELEMENT (v4lmjpegsink)->video_fd, MJPIOC_QBUF_PLAY,
          &num) < 0) {
    GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, WRITE, (NULL),
        ("Failed to queue frame %d: %s", num, g_strerror (errno)));
    return FALSE;
  }

  g_mutex_lock (v4lmjpegsink->mutex_queued_frames);
  v4lmjpegsink->isqueued_queued_frames[num] = 1;
  g_cond_broadcast (v4lmjpegsink->cond_queued_frames[num]);
  g_mutex_unlock (v4lmjpegsink->mutex_queued_frames);

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsink_sync_frame()
 *   wait for a frame to be finished playing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lmjpegsink_sync_frame (GstV4lMjpegSink * v4lmjpegsink, gint * num)
{
  DEBUG ("syncing on next frame");

  /* calculate next frame */
  v4lmjpegsink->current_frame =
      (v4lmjpegsink->current_frame + 1) % v4lmjpegsink->breq.count;
  *num = v4lmjpegsink->current_frame;

  g_mutex_lock (v4lmjpegsink->mutex_queued_frames);
  if (v4lmjpegsink->isqueued_queued_frames[*num] == 1) {
    g_cond_wait (v4lmjpegsink->cond_queued_frames[*num],
        v4lmjpegsink->mutex_queued_frames);
  }
  if (v4lmjpegsink->isqueued_queued_frames[*num] != 0) {
    g_mutex_unlock (v4lmjpegsink->mutex_queued_frames);
    return FALSE;
  } else
    g_mutex_unlock (v4lmjpegsink->mutex_queued_frames);

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsink_set_buffer()
 *   set buffer options
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsink_set_buffer (GstV4lMjpegSink * v4lmjpegsink,
    gint numbufs, gint bufsize)
{
  DEBUG ("setting buffer info to numbufs = %d, bufsize = %d KB",
      numbufs, bufsize);
  GST_V4L_CHECK_OPEN (GST_V4LELEMENT (v4lmjpegsink));
  GST_V4L_CHECK_NOT_ACTIVE (GST_V4LELEMENT (v4lmjpegsink));

  v4lmjpegsink->breq.size = bufsize * 1024;
  v4lmjpegsink->breq.count = numbufs;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsink_set_playback()
 *   set playback options (video, interlacing, etc.)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsink_set_playback (GstV4lMjpegSink * v4lmjpegsink,
    gint width,
    gint height, gint x_offset, gint y_offset, gint norm, gint interlacing)
{
  gint mw, mh;
  struct mjpeg_params bparm;

  DEBUG
      ("setting size = %dx%d, X/Y-offsets = %d/%d, norm = %d, interlacing = %d\n",
      width, height, x_offset, y_offset, norm, interlacing);
  GST_V4L_CHECK_OPEN (GST_V4LELEMENT (v4lmjpegsink));
  /*GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lmjpegsink)); */

  if (ioctl (GST_V4LELEMENT (v4lmjpegsink)->video_fd, MJPIOC_G_PARAMS,
          &bparm) < 0) {
    GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, SETTINGS, (NULL),
        GST_ERROR_SYSTEM);
    return FALSE;
  }

  bparm.input = 0;
  bparm.norm = norm;
  bparm.decimation = 0;         /* we'll set proper values later on */

  /* maxwidth is broken on marvel cards */
  mw = GST_V4LELEMENT (v4lmjpegsink)->vcap.maxwidth;
  if (mw != 768 && mw != 640)
    mw = 720;
  mh = (norm == VIDEO_MODE_NTSC ? 480 : 576);

  if (width > mw || height > mh) {
    GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, TOO_LAZY, (NULL),
        ("Video dimensions (%dx%d) are larger than device max (%dx%d)",
            width, height, mw, mh));
    return FALSE;
  }

  if (width <= mw / 4)
    bparm.HorDcm = 4;
  else if (width <= mw / 2)
    bparm.HorDcm = 2;
  else
    bparm.HorDcm = 1;

  /* TODO: add proper interlacing handling */
#if 0
  if (interlacing != INTERLACING_NOT_INTERLACED) {
    bparm.field_per_buff = 2;
    bparm.TmpDcm = 1;

    if (height <= mh / 2)
      bparm.VerDcm = 2;
    else
      bparm.VerDcm = 1;
  } else
#endif
  {
    if (height > mh / 2) {
      GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, TOO_LAZY, (NULL),
          ("Video dimensions (%dx%d) too large for non-interlaced playback (%dx%d)",
              width, height, mw, mh / 2));
      return FALSE;
    }

    bparm.field_per_buff = 1;
    bparm.TmpDcm = 2;

    if (height <= mh / 4)
      bparm.VerDcm = 2;
    else
      bparm.VerDcm = 1;
  }

  /* TODO: add proper interlacing handling */
#if 0
  bparm.odd_even = (interlacing == INTERLACING_TOP_FIRST);
#endif

  bparm.quality = 100;
  bparm.img_width = bparm.HorDcm * width;
  bparm.img_height = bparm.VerDcm * height / bparm.field_per_buff;

  /* image X/Y offset on device */
  if (x_offset < 0)
    bparm.img_x = (mw - bparm.img_width) / 2;
  else {
    if (x_offset + bparm.img_width > mw)
      bparm.img_x = mw - bparm.img_width;
    else
      bparm.img_x = x_offset;
  }

  if (y_offset < 0)
    bparm.img_y = (mh / 2 - bparm.img_height) / 2;
  else {
    if (y_offset + bparm.img_height * 2 > mh)
      bparm.img_y = mh / 2 - bparm.img_height;
    else
      bparm.img_y = y_offset / 2;
  }

  if (ioctl (GST_V4LELEMENT (v4lmjpegsink)->video_fd, MJPIOC_S_PARAMS,
          &bparm) < 0) {
    GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, SETTINGS, (NULL),
        GST_ERROR_SYSTEM);
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsink_playback_init()
 *   initialize playback system, set up buffer, etc.
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsink_playback_init (GstV4lMjpegSink * v4lmjpegsink)
{
  gint n;

  DEBUG ("initting playback subsystem");
  GST_V4L_CHECK_OPEN (GST_V4LELEMENT (v4lmjpegsink));
  GST_V4L_CHECK_NOT_ACTIVE (GST_V4LELEMENT (v4lmjpegsink));

  /* Request buffers */
  if (ioctl (GST_V4LELEMENT (v4lmjpegsink)->video_fd, MJPIOC_REQBUFS,
          &(v4lmjpegsink->breq)) < 0) {
    GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }

  GST_INFO ("Got %ld buffers of size %ld KB",
      v4lmjpegsink->breq.count, v4lmjpegsink->breq.size / 1024);

  /* Map the buffers */
  GST_V4LELEMENT (v4lmjpegsink)->buffer = mmap (0,
      v4lmjpegsink->breq.count * v4lmjpegsink->breq.size,
      PROT_READ | PROT_WRITE, MAP_SHARED,
      GST_V4LELEMENT (v4lmjpegsink)->video_fd, 0);
  if (GST_V4LELEMENT (v4lmjpegsink)->buffer == MAP_FAILED) {
    GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, TOO_LAZY, (NULL),
        ("Error mapping video buffers: %s", g_strerror (errno)));
    GST_V4LELEMENT (v4lmjpegsink)->buffer = NULL;
    return FALSE;
  }

  /* allocate/init the GThread thingies */
  v4lmjpegsink->mutex_queued_frames = g_mutex_new ();
  v4lmjpegsink->isqueued_queued_frames = (gint8 *)
      malloc (sizeof (gint8) * v4lmjpegsink->breq.count);
  if (!v4lmjpegsink->isqueued_queued_frames) {
    GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, TOO_LAZY, (NULL),
        ("Failed to create queue tracker: %s", g_strerror (errno)));
    return FALSE;
  }
  v4lmjpegsink->cond_queued_frames = (GCond **)
      malloc (sizeof (GCond *) * v4lmjpegsink->breq.count);
  if (!v4lmjpegsink->cond_queued_frames) {
    GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, TOO_LAZY, (NULL),
        ("Failed to create queue condition holders: %s", g_strerror (errno)));
    return FALSE;
  }
  for (n = 0; n < v4lmjpegsink->breq.count; n++)
    v4lmjpegsink->cond_queued_frames[n] = g_cond_new ();

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsink_playback_start()
 *   start playback system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsink_playback_start (GstV4lMjpegSink * v4lmjpegsink)
{
  GError *error;
  gint n;

  DEBUG ("starting playback");
  GST_V4L_CHECK_OPEN (GST_V4LELEMENT (v4lmjpegsink));
  GST_V4L_CHECK_ACTIVE (GST_V4LELEMENT (v4lmjpegsink));

  /* mark all buffers as unqueued */
  for (n = 0; n < v4lmjpegsink->breq.count; n++)
    v4lmjpegsink->isqueued_queued_frames[n] = 0;

  v4lmjpegsink->current_frame = -1;

  /* create sync() thread */
  v4lmjpegsink->thread_queued_frames =
      g_thread_create (gst_v4lmjpegsink_sync_thread, (void *) v4lmjpegsink,
      TRUE, &error);
  if (!v4lmjpegsink->thread_queued_frames) {
    GST_ELEMENT_ERROR (v4lmjpegsink, RESOURCE, TOO_LAZY, (NULL),
        ("Failed to create sync thread: %s", error->message));
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsink_get_buffer()
 *   get address of a buffer
 * return value: buffer's address or NULL
 ******************************************************/

guint8 *
gst_v4lmjpegsink_get_buffer (GstV4lMjpegSink * v4lmjpegsink, gint num)
{
  /*DEBUG("gst_v4lmjpegsink_get_buffer(), num = %d", num); */

  if (!GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lmjpegsink)) ||
      !GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lmjpegsink)))
    return NULL;

  if (num < 0 || num >= v4lmjpegsink->breq.count)
    return NULL;

  return GST_V4LELEMENT (v4lmjpegsink)->buffer +
      (v4lmjpegsink->breq.size * num);
}


/******************************************************
 * gst_v4lmjpegsink_play_frame()
 *   queue a new buffer
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsink_play_frame (GstV4lMjpegSink * v4lmjpegsink, gint num)
{
  DEBUG ("playing frame %d", num);
  GST_V4L_CHECK_OPEN (GST_V4LELEMENT (v4lmjpegsink));
  GST_V4L_CHECK_ACTIVE (GST_V4LELEMENT (v4lmjpegsink));

  if (!gst_v4lmjpegsink_queue_frame (v4lmjpegsink, num))
    return FALSE;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsink_wait_frame()
 *   wait for buffer to be actually played
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsink_wait_frame (GstV4lMjpegSink * v4lmjpegsink, gint * num)
{
  DEBUG ("waiting for next frame to be finished playing");
  GST_V4L_CHECK_OPEN (GST_V4LELEMENT (v4lmjpegsink));
  GST_V4L_CHECK_ACTIVE (GST_V4LELEMENT (v4lmjpegsink));

  if (!gst_v4lmjpegsink_sync_frame (v4lmjpegsink, num))
    return FALSE;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsink_playback_stop()
 *   stop playback system and sync on remaining frames
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsink_playback_stop (GstV4lMjpegSink * v4lmjpegsink)
{
  gint num;

  DEBUG ("stopping playback");
  GST_V4L_CHECK_OPEN (GST_V4LELEMENT (v4lmjpegsink));
  GST_V4L_CHECK_ACTIVE (GST_V4LELEMENT (v4lmjpegsink));

  /* mark next buffer as wrong */
  if (!gst_v4lmjpegsink_sync_frame (v4lmjpegsink, &num) ||
      !gst_v4lmjpegsink_queue_frame (v4lmjpegsink, num)) {
    return FALSE;
  }

  /* .. and wait for all buffers to be queued on */
  g_thread_join (v4lmjpegsink->thread_queued_frames);

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsink_playback_deinit()
 *   deinitialize the playback system and unmap buffer
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsink_playback_deinit (GstV4lMjpegSink * v4lmjpegsink)
{
  int n;

  DEBUG ("quitting playback subsystem");
  GST_V4L_CHECK_OPEN (GST_V4LELEMENT (v4lmjpegsink));
  GST_V4L_CHECK_ACTIVE (GST_V4LELEMENT (v4lmjpegsink));

  /* free GThread thingies */
  g_mutex_free (v4lmjpegsink->mutex_queued_frames);
  for (n = 0; n < v4lmjpegsink->breq.count; n++)
    g_cond_free (v4lmjpegsink->cond_queued_frames[n]);
  free (v4lmjpegsink->cond_queued_frames);
  free (v4lmjpegsink->isqueued_queued_frames);

  /* unmap the buffer */
  munmap (GST_V4LELEMENT (v4lmjpegsink)->buffer,
      v4lmjpegsink->breq.size * v4lmjpegsink->breq.count);
  GST_V4LELEMENT (v4lmjpegsink)->buffer = NULL;

  return TRUE;
}
