/* G-Streamer Video4linux2 video-capture plugin - system calls
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4l2src_calls.h"
#include <sys/time.h>

#define DEBUG(format, args...) \
	GST_DEBUG_OBJECT (\
		GST_ELEMENT(v4l2src), \
		"V4L2SRC: " format, ##args)

#define MIN_BUFFERS_QUEUED 2

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ( (caddr_t) -1 )
#endif

enum {
  QUEUE_STATE_ERROR = -1,
  QUEUE_STATE_READY_FOR_QUEUE,
  QUEUE_STATE_QUEUED,
  QUEUE_STATE_SYNCED,
};

/******************************************************
 * gst_v4l2src_fill_format_list():
 *   create list of supported capture formats
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_fill_format_list (GstV4l2Src *v4l2src)
{
	gint n;

	DEBUG("getting src format enumerations");

	/* format enumeration */
	for (n=0;;n++) {
		struct v4l2_fmtdesc format, *fmtptr;
		format.index = n;
		format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
			if (errno == EINVAL)
				break; /* end of enumeration */
			else {
				gst_element_error(GST_ELEMENT(v4l2src),
					"Failed to get no. %d in pixelformat enumeration for %s: %s",
					n, GST_V4L2ELEMENT(v4l2src)->device, g_strerror(errno));
				return FALSE;
			}
		}
		fmtptr = g_malloc(sizeof(format));
		memcpy(fmtptr, &format, sizeof(format));
		v4l2src->formats = g_list_append(v4l2src->formats, fmtptr);

		v4l2src->format_list = g_list_append(v4l2src->format_list, fmtptr->description);
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2src_empty_format_list():
 *   free list of supported capture formats
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_empty_format_list (GstV4l2Src *v4l2src)
{
	while (g_list_length(v4l2src->formats) > 0) {
		gpointer data = g_list_nth_data(v4l2src->formats, 0);
		v4l2src->formats = g_list_remove(v4l2src->formats, data);
		g_free(data);
	}
	g_list_free(v4l2src->format_list);
	v4l2src->format_list = NULL;

	return TRUE;
}


/******************************************************
 * gst_v4l2src_queue_frame():
 *   queue a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4l2src_queue_frame (GstV4l2Src *v4l2src,
                         gint        num)
{
	DEBUG("queueing frame %d", num);

	if (v4l2src->frame_queue_state[num] != QUEUE_STATE_READY_FOR_QUEUE) {
		return FALSE;
	}

	v4l2src->bufsettings.index = num;
	if (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd,
		  VIDIOC_QBUF, &v4l2src->bufsettings) < 0) {
		gst_element_error(GST_ELEMENT(v4l2src),
			"Error queueing buffer %d on device %s: %s",
			num, GST_V4L2ELEMENT(v4l2src)->device, g_strerror(errno));
		return FALSE;
	}

	v4l2src->frame_queue_state[num] = QUEUE_STATE_QUEUED;
	v4l2src->num_queued++;

	return TRUE;
}


/******************************************************
 * gst_v4l2src_sync_next_frame():
 *   sync on a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4l2src_sync_next_frame (GstV4l2Src *v4l2src,
                             gint       *num)
{
	if (v4l2src->num_queued <= 0) {
		return FALSE;
	}

	while (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd,
		     VIDIOC_DQBUF, &v4l2src->bufsettings) < 0) {
		/* if the sync() got interrupted, we can retry */
		if (errno != EINTR) {
			gst_element_error(GST_ELEMENT(v4l2src),
				"Error syncing on a buffer on device %s: %s",
				GST_V4L2ELEMENT(v4l2src)->device, g_strerror(errno));
			return FALSE;
		}
		DEBUG("Sync got interrupted");
	}

	DEBUG("synced on frame %d", v4l2src->bufsettings.index);
	*num = v4l2src->bufsettings.index;

	v4l2src->frame_queue_state[*num] = QUEUE_STATE_SYNCED;
	v4l2src->num_queued--;

	return TRUE;
}


/******************************************************
 * gst_v4l2src_get_capture():
 *   get capture parameters
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_get_capture (GstV4l2Src *v4l2src)
{
	DEBUG("Getting capture format");

	GST_V4L2_CHECK_OPEN(GST_V4L2ELEMENT(v4l2src));

	v4l2src->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd, VIDIOC_G_FMT, &v4l2src->format) < 0) {
		gst_element_error(GST_ELEMENT(v4l2src),
			"Failed to get pixel format for device %s: %s",
			GST_V4L2ELEMENT(v4l2src)->device, g_strerror(errno));
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2src_set_capture():
 *   set capture parameters
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_set_capture (GstV4l2Src          *v4l2src,
                         struct v4l2_fmtdesc *fmt,
                         gint                 width,
                         gint                 height)
{
	DEBUG("Setting capture format to %dx%d, format %s",
		width, height, fmt->description);

	GST_V4L2_CHECK_OPEN(GST_V4L2ELEMENT(v4l2src));
	GST_V4L2_CHECK_NOT_ACTIVE(GST_V4L2ELEMENT(v4l2src));

	memset(&v4l2src->format, 0, sizeof(struct v4l2_format));
	v4l2src->format.fmt.pix.width = width;
	v4l2src->format.fmt.pix.height = height;
	v4l2src->format.fmt.pix.pixelformat = fmt->pixelformat;
	v4l2src->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd, VIDIOC_S_FMT, &v4l2src->format) < 0) {
		gst_element_error(GST_ELEMENT(v4l2src),
			"Failed to set pixel format to %s @ %dx%d for device %s: %s",
			fmt->description, width, height,
			GST_V4L2ELEMENT(v4l2src)->device, g_strerror(errno));
		return FALSE;
	}

	/* update internal info */
	return gst_v4l2src_get_capture(v4l2src);;
}


/******************************************************
 * gst_v4l2src_capture_init():
 *   initialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_capture_init (GstV4l2Src *v4l2src)
{
	gint n;
	gchar *desc = NULL;
	struct v4l2_buffer buf;

	DEBUG("initting the capture system");

	GST_V4L2_CHECK_OPEN(GST_V4L2ELEMENT(v4l2src));
	GST_V4L2_CHECK_NOT_ACTIVE(GST_V4L2ELEMENT(v4l2src));

	/* request buffer info */
	if (v4l2src->breq.count < MIN_BUFFERS_QUEUED) {
		v4l2src->breq.count = MIN_BUFFERS_QUEUED;
	}
	v4l2src->breq.type = v4l2src->format.type;
	v4l2src->breq.memory = V4L2_MEMORY_MMAP;
	if (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd,
		  VIDIOC_REQBUFS, &v4l2src->breq) < 0) {
		gst_element_error(GST_ELEMENT(v4l2src),
			"Error requesting buffers (%d) for %s: %s",
			v4l2src->breq.count, GST_V4L2ELEMENT(v4l2src)->device, g_strerror(errno));
		return FALSE;
	}

	if (v4l2src->breq.count < MIN_BUFFERS_QUEUED) {
		gst_element_error(GST_ELEMENT(v4l2src),
			"Too little buffers. We got %d, we want at least %d",
			v4l2src->breq.count, MIN_BUFFERS_QUEUED);
		return FALSE;
	}
	v4l2src->bufsettings.type = v4l2src->format.type;

	for (n=0;n<g_list_length(v4l2src->formats);n++) {
		struct v4l2_fmtdesc *fmt = (struct v4l2_fmtdesc *) g_list_nth_data(v4l2src->formats, n);
		if (v4l2src->format.fmt.pix.pixelformat == fmt->pixelformat) {
			desc = fmt->description;
			break;
		}
	}
	gst_info("Got %d buffers (%s) of size %d KB\n",
		v4l2src->breq.count, desc, v4l2src->format.fmt.pix.sizeimage/1024);

	/* keep track of queued buffers */
	v4l2src->frame_queue_state = (gint8 *)
		g_malloc(sizeof(gint8) * v4l2src->breq.count);

	/* track how often to use each frame */
	v4l2src->use_num_times = (gint *)
		g_malloc(sizeof(gint) * v4l2src->breq.count);

	/* lock for the frame_state */
	v4l2src->mutex_queue_state = g_mutex_new();
	v4l2src->cond_queue_state = g_cond_new();

	/* Map the buffers */
	GST_V4L2ELEMENT(v4l2src)->buffer = (guint8 **)
		g_malloc(sizeof(guint8 *) * v4l2src->breq.count);
	for (n=0;n<v4l2src->breq.count;n++) {
		buf.index = n;
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd,
			  VIDIOC_QUERYBUF, &buf) < 0) {
			gst_element_error(GST_ELEMENT(v4l2src),
					  "Failed to get buffer (%d) properties: %s",
					  n, g_strerror(errno));
			gst_v4l2src_capture_deinit(v4l2src);
			return FALSE;
		}
		GST_V4L2ELEMENT(v4l2src)->buffer[n] = mmap(0,
			buf.length, PROT_READ|PROT_WRITE, MAP_SHARED,
			GST_V4L2ELEMENT(v4l2src)->video_fd, buf.m.offset);
		if (GST_V4L2ELEMENT(v4l2src)->buffer[n] == MAP_FAILED) {
			gst_element_error(GST_ELEMENT(v4l2src),
				"Error mapping video buffer (%d) on device %s: %s",
				n, GST_V4L2ELEMENT(v4l2src)->device,
				g_strerror(errno));
			GST_V4L2ELEMENT(v4l2src)->buffer[n] = NULL;
			gst_v4l2src_capture_deinit(v4l2src);
			return FALSE;
		}
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2src_capture_start():
 *   start streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_capture_start (GstV4l2Src *v4l2src)
{
	gint n;

	DEBUG("starting the capturing");
	GST_V4L2_CHECK_OPEN(GST_V4L2ELEMENT(v4l2src));
	GST_V4L2_CHECK_ACTIVE(GST_V4L2ELEMENT(v4l2src));

	g_mutex_lock(v4l2src->mutex_queue_state);

	v4l2src->quit = FALSE;
	v4l2src->num_queued = 0;
	v4l2src->queue_frame = 0;

	/* set all buffers ready to queue , this starts streaming capture */
	for (n=0;n<v4l2src->breq.count;n++) {
		v4l2src->frame_queue_state[n] = QUEUE_STATE_READY_FOR_QUEUE;
		if (!gst_v4l2src_queue_frame(v4l2src, n)) {
			g_mutex_unlock(v4l2src->mutex_queue_state);
			gst_v4l2src_capture_stop(v4l2src);
			return FALSE;
		}
	}

	n = 1;
	if (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd, VIDIOC_STREAMON, &n) < 0) {
		gst_element_error(GST_ELEMENT(v4l2src),
			"Error starting streaming capture for %s: %s",
			GST_V4L2ELEMENT(v4l2src)->device, g_strerror(errno));
		return FALSE;
	}

	g_mutex_unlock(v4l2src->mutex_queue_state);

	return TRUE;
}


/******************************************************
 * gst_v4l2src_grab_frame():
 *   capture one frame during streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_grab_frame (GstV4l2Src *v4l2src,
                        gint       *num)
{
	DEBUG("syncing on the next frame");

	GST_V4L2_CHECK_OPEN(GST_V4L2ELEMENT(v4l2src));
	GST_V4L2_CHECK_ACTIVE(GST_V4L2ELEMENT(v4l2src));

	g_mutex_lock(v4l2src->mutex_queue_state);

	/* do we have enough frames? */
	while (v4l2src->num_queued < MIN_BUFFERS_QUEUED ||
	       v4l2src->frame_queue_state[v4l2src->queue_frame] ==
			QUEUE_STATE_READY_FOR_QUEUE) {
		while (v4l2src->frame_queue_state[v4l2src->queue_frame] !=
				QUEUE_STATE_READY_FOR_QUEUE &&
		       !v4l2src->quit) {
			GST_DEBUG (
				  "Waiting for frames to become available (%d < %d)",
				  v4l2src->num_queued, MIN_BUFFERS_QUEUED);
			g_cond_wait(v4l2src->cond_queue_state,
				    v4l2src->mutex_queue_state);
		}
		if (v4l2src->quit) {
			g_mutex_unlock(v4l2src->mutex_queue_state);
			return TRUE; /* it won't get through anyway */
		}
		if (!gst_v4l2src_queue_frame(v4l2src, v4l2src->queue_frame)) {
			g_mutex_unlock(v4l2src->mutex_queue_state);
			return FALSE;
		}
		v4l2src->queue_frame = (v4l2src->queue_frame + 1) % v4l2src->breq.count;
	}

	/* syncing on the buffer grabs it */
	if (!gst_v4l2src_sync_next_frame(v4l2src, num)) {
		g_mutex_unlock(v4l2src->mutex_queue_state);
		return FALSE;
	}

	g_mutex_unlock(v4l2src->mutex_queue_state);

	return TRUE;
}


/******************************************************
 *
 ******************************************************/

guint8 *
gst_v4l2src_get_buffer (GstV4l2Src *v4l2src,
		        gint        num)
{
	if (!GST_V4L2_IS_ACTIVE(GST_V4L2ELEMENT(v4l2src)) ||
	    !GST_V4L2_IS_OPEN(GST_V4L2ELEMENT(v4l2src)))
		return NULL;

	if (num < 0 || num >= v4l2src->breq.count)
		return NULL;

	return GST_V4L2ELEMENT(v4l2src)->buffer[num];
}


/******************************************************
 * gst_v4l2src_requeue_frame():
 *   re-queue a frame after we're done with the buffer
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_requeue_frame (GstV4l2Src *v4l2src,
                           gint        num)
{
	DEBUG("requeueing frame %d", num);
	GST_V4L2_CHECK_OPEN(GST_V4L2ELEMENT(v4l2src));
	GST_V4L2_CHECK_ACTIVE(GST_V4L2ELEMENT(v4l2src));

	/* mark frame as 'ready to requeue' */
	g_mutex_lock(v4l2src->mutex_queue_state);

	if (v4l2src->frame_queue_state[num] != QUEUE_STATE_SYNCED) {
		gst_element_error(GST_ELEMENT(v4l2src),
				  "Invalid state %d (expected %d), can't requeue",
				  v4l2src->frame_queue_state[num],
				  QUEUE_STATE_SYNCED);
		return FALSE;
	}

	v4l2src->frame_queue_state[num] = QUEUE_STATE_READY_FOR_QUEUE;

	/* let an optional wait know */
	g_cond_broadcast(v4l2src->cond_queue_state);

	g_mutex_unlock(v4l2src->mutex_queue_state);

	return TRUE;
}


/******************************************************
 * gst_v4l2src_capture_stop():
 *   stop streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_capture_stop (GstV4l2Src *v4l2src)
{
	gint n = 0;

	DEBUG("stopping capturing");
	GST_V4L2_CHECK_OPEN(GST_V4L2ELEMENT(v4l2src));
	GST_V4L2_CHECK_ACTIVE(GST_V4L2ELEMENT(v4l2src));

	g_mutex_lock(v4l2src->mutex_queue_state);

	/* we actually need to sync on all queued buffers but not
	 * on the non-queued ones */
	if (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd, VIDIOC_STREAMOFF, &n) < 0) {
		gst_element_error(GST_ELEMENT(v4l2src),
			"Error stopping streaming capture for %s: %s",
			GST_V4L2ELEMENT(v4l2src)->device, g_strerror(errno));
		return FALSE;
	}

	/* make an optional pending wait stop */
	v4l2src->quit = TRUE;
	g_cond_broadcast(v4l2src->cond_queue_state);
                                                                                
	/* sync on remaining frames */
	while (v4l2src->num_queued > 0) {
		gst_v4l2src_sync_next_frame(v4l2src, &n);
	}

	g_mutex_unlock(v4l2src->mutex_queue_state);

	return TRUE;
}


/******************************************************
 * gst_v4l2src_capture_deinit():
 *   deinitialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_capture_deinit (GstV4l2Src *v4l2src)
{
	int n;
	
	DEBUG("deinitting capture system");
	GST_V4L2_CHECK_OPEN(GST_V4L2ELEMENT(v4l2src));
	GST_V4L2_CHECK_ACTIVE(GST_V4L2ELEMENT(v4l2src));

	/* unmap the buffer */
	for (n=0;n<v4l2src->breq.count;n++) {
		if (!GST_V4L2ELEMENT(v4l2src)->buffer[n]) {
			break;
		}
		munmap(GST_V4L2ELEMENT(v4l2src)->buffer[n],
		       v4l2src->format.fmt.pix.sizeimage);
		GST_V4L2ELEMENT(v4l2src)->buffer[n] = NULL;
	}

	/* free buffer tracker */
	g_free(GST_V4L2ELEMENT(v4l2src)->buffer);
	GST_V4L2ELEMENT(v4l2src)->buffer = NULL;
	g_mutex_free(v4l2src->mutex_queue_state);
	g_cond_free(v4l2src->cond_queue_state);
	g_free(v4l2src->frame_queue_state);
	g_free(v4l2src->use_num_times);

	return TRUE;
}


/*

 */

gboolean
gst_v4l2src_get_size_limits (GstV4l2Src          *v4l2src,
			     struct v4l2_fmtdesc *format,
			     gint *min_w, gint *max_w,
			     gint *min_h, gint *max_h)
{
	struct v4l2_format fmt;

	/* get size delimiters */
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = 0;
	fmt.fmt.pix.height = 0;
	fmt.fmt.pix.pixelformat = format->pixelformat;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;
	if (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd,
		  VIDIOC_TRY_FMT, &fmt) < 0) {
		return FALSE;
	}

	if (min_w)
		*min_w = fmt.fmt.pix.width;
	if (min_h)
		*min_h = fmt.fmt.pix.height;

	fmt.fmt.pix.width = G_MAXINT;
	fmt.fmt.pix.height = G_MAXINT;
	if (ioctl(GST_V4L2ELEMENT(v4l2src)->video_fd,
		  VIDIOC_TRY_FMT, &fmt) < 0) {
		return FALSE;
	}

	if (max_w)
		*max_w = fmt.fmt.pix.width;
	if (max_h)
		*max_h = fmt.fmt.pix.height;

	return TRUE;
}
