/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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
#  include "config.h"
#endif

#include "gstxine.h"

GST_BOILERPLATE (GstXine, gst_xine, GstElement, GST_TYPE_ELEMENT)

static GstElementStateReturn	gst_xine_change_state		(GstElement *	element);
  
static xine_ao_driver_t *  	_xine_create_audio_driver     	(GstXine *	xine);
static xine_vo_driver_t *  	_xine_create_video_driver     	(GstXine *	xine);


static void
gst_xine_base_init (gpointer klass)
{
}

static void
gst_xine_class_init (GstXineClass *klass)
{
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  
  klass->xine = xine_new ();
  xine_init (klass->xine);

  klass->create_audio_driver = _xine_create_audio_driver;
  klass->create_video_driver = _xine_create_video_driver;

  element->change_state = gst_xine_change_state;
}

static void
gst_xine_init (GstXine *filter)
{
}

static GstElementStateReturn
gst_xine_change_state (GstElement *element)
{
  GstXine *xine = GST_XINE (element);
  
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      if (xine->stream != NULL)
	gst_xine_free_stream (xine);
      break;
    default:
      GST_ERROR_OBJECT (element, "invalid state change");
      break;
  }
  
  return GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state, (element), GST_STATE_SUCCESS);
}

static xine_ao_driver_t *
_xine_create_audio_driver (GstXine *xine)
{
  return xine_open_audio_driver (GST_XINE_GET_CLASS (xine)->xine, "none", NULL);
}

static xine_vo_driver_t *
_xine_create_video_driver (GstXine *xine)
{
  return xine_open_video_driver (GST_XINE_GET_CLASS (xine)->xine, "none", XINE_VISUAL_TYPE_NONE, NULL);
}

xine_stream_t *
gst_xine_get_stream (GstXine *xine)
{
  if (!xine->stream) {
    GstXineClass *klass = GST_XINE_GET_CLASS (xine);
    g_assert (xine->video_driver == NULL);
    g_assert (xine->audio_driver == NULL);
    xine->audio_driver = klass->create_audio_driver (xine);
    xine->video_driver = klass->create_video_driver (xine);
    xine->stream = xine_stream_new (klass->xine, xine->audio_driver, xine->video_driver);

    /* FIXME: fail gracefully */
    g_assert (xine->stream);
  }

  return xine->stream;
}

void
gst_xine_free_stream (GstXine *xine)
{
  g_return_if_fail (xine->stream != NULL);
  g_assert (xine->video_driver != NULL);
  g_assert (xine->audio_driver != NULL);

  xine_dispose (xine->stream);
  xine->stream = NULL;
  xine_close_video_driver (GST_XINE_GET_CLASS (xine)->xine, xine->video_driver);
  xine->video_driver = NULL;
  xine_close_audio_driver (GST_XINE_GET_CLASS (xine)->xine, xine->audio_driver);
  xine->audio_driver = NULL;
}

static void
_free_xine_buf_element (buf_element_t *buffer)
{
  gst_buffer_unref (GST_BUFFER (buffer->source));
}

void
gst_buffer_to_xine_buffer (buf_element_t *ret, GstBuffer *buffer)
{
  g_return_if_fail (ret != NULL);
  g_return_if_fail (buffer != NULL);
  
  /* FIXME: what's the difference? */
  ret->mem = GST_BUFFER_DATA (buffer);
  ret->content = GST_BUFFER_DATA (buffer);
  ret->size = GST_BUFFER_SIZE (buffer);
  ret->max_size = GST_BUFFER_MAXSIZE (buffer);
  /* FIXME: add more */
  ret->free_buffer = _free_xine_buf_element;
  ret->source = buffer;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_xine_input_init_plugin (plugin) ||
      !gst_xine_audio_dec_init_plugin (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "xine",
  "wrapper for libxine (version "XINE_VERSION") plugins",
  plugin_init,
  VERSION,
  "GPL",
  GST_PACKAGE,
  GST_ORIGIN
)
