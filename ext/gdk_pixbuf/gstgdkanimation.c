/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

#include "config.h"

#include "gstgdkanimation.h"
#include <unistd.h>

GST_DEBUG_CATEGORY_STATIC (gst_gdk_animation_debug);
#define GST_CAT_DEFAULT gst_gdk_animation_debug

static void		gst_gdk_animation_class_init		(gpointer		g_class,
								 gpointer		class_data);
static void		gst_gdk_animation_finalize		(GObject *	      	object);

static gboolean         gst_gdk_animation_is_static_image	(GdkPixbufAnimation *	animation);
static GdkPixbuf*       gst_gdk_animation_get_static_image	(GdkPixbufAnimation *	animation);
static void             gst_gdk_animation_get_size		(GdkPixbufAnimation *	anim,
								 gint *			width,
								 gint *			height);
static GdkPixbufAnimationIter* gst_gdk_animation_get_iter	(GdkPixbufAnimation *	anim,
								 const GTimeVal *	start_time);


static gpointer parent_class;

GType
gst_gdk_animation_get_type (void)
{
  static GType object_type = 0;

  if (!object_type) {
    static const GTypeInfo object_info = {
      sizeof (GstGdkAnimationClass),
      NULL,
      NULL,
      gst_gdk_animation_class_init,
      NULL,           /* class_finalize */
      NULL,           /* class_data */
      sizeof (GstGdkAnimation),
      0,              /* n_preallocs */
      NULL,
    };
                
    object_type = g_type_register_static (GDK_TYPE_PIXBUF_ANIMATION,
	    "GstGdkAnimation", &object_info, 0);

    GST_DEBUG_CATEGORY_INIT (gst_gdk_animation_debug, "gstloader_animation", 0, "GStreamer GdkPixbuf loader - GdkAnimation class");
  }
        
  return object_type;
}
static void
gst_gdk_animation_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (g_class);
  GdkPixbufAnimationClass *anim_class = GDK_PIXBUF_ANIMATION_CLASS (g_class);
        
  parent_class = g_type_class_peek_parent (g_class);
        
  object_class->finalize = gst_gdk_animation_finalize;

  anim_class->is_static_image = gst_gdk_animation_is_static_image;
  anim_class->get_static_image = gst_gdk_animation_get_static_image;
  anim_class->get_size = gst_gdk_animation_get_size;
  anim_class->get_iter = gst_gdk_animation_get_iter;
}
static void
gst_gdk_animation_finalize (GObject *object)
{
  GstGdkAnimation *ani = GST_GDK_ANIMATION (object);

  if (ani->temp_fd) {
    close (ani->temp_fd);
  }
  if (ani->temp_location) {
    remove (ani->temp_location);
    g_free (ani->temp_location);
  }
  if (ani->pixbuf) {
    g_object_unref (ani->pixbuf);
    ani->pixbuf = NULL;
  }
        
  G_OBJECT_CLASS (parent_class)->finalize (object);
}
GstGdkAnimation *
gst_gdk_animation_new (GError **error)
{
  GstGdkAnimation *ani = GST_GDK_ANIMATION (g_object_new (GST_TYPE_GDK_ANIMATION, NULL));

  ani->temp_fd = g_file_open_tmp (NULL, &ani->temp_location, error);
  if (ani->temp_fd == 0) {
    g_object_unref (ani);
    return NULL;
  }

  return ani;
}
gboolean
gst_gdk_animation_add_data (GstGdkAnimation *ani, const guint8 *data, guint size)
{
  return (write (ani->temp_fd, data, size) == size);
}
void
gst_gdk_animation_done_adding (GstGdkAnimation *ani)
{
  close (ani->temp_fd);
  ani->temp_fd = 0;
}
static gboolean
gst_gdk_animation_is_static_image (GdkPixbufAnimation *animation)
{
  return FALSE;
}

static GdkPixbuf*
gst_gdk_animation_get_static_image (GdkPixbufAnimation *animation)
{
  GstGdkAnimation *ani = GST_GDK_ANIMATION (animation);

  return ani->pixbuf;
}

static void
gst_gdk_animation_get_size (GdkPixbufAnimation *anim, gint *width, int *height)
{
  GstGdkAnimation *ani = GST_GDK_ANIMATION (anim);

  GST_LOG_OBJECT (ani, "get_size called (%p, %p) %d x %d", width, height, ani->width, ani->height);
  if (width)
    *width = ani->width;

  if (height)
    *height = ani->height;
}


static void		gst_gdk_animation_iter_class_init		(gpointer			g_class,
									 gpointer			class_data);
static void		gst_gdk_animation_iter_init	      		(GTypeInstance *		instance, 
									 gpointer			g_class);
static void		gst_gdk_animation_iter_finalize			(GObject *			object);

static gint		gst_gdk_animation_iter_get_delay_time		(GdkPixbufAnimationIter *	iter);
static GdkPixbuf *	gst_gdk_animation_iter_get_pixbuf		(GdkPixbufAnimationIter *	iter);
static gboolean		gst_gdk_animation_iter_on_currently_loading_frame (GdkPixbufAnimationIter *	iter);
static gboolean		gst_gdk_animation_iter_advance			(GdkPixbufAnimationIter *	iter,
									 const GTimeVal *		current_time);

static gpointer iter_parent_class;

GType
gst_gdk_animation_iter_get_type (void)
{
  static GType object_type = 0;

  if (!object_type) {
    static const GTypeInfo object_info = {
      sizeof (GstGdkAnimationIterClass),
      NULL,
      NULL,
      gst_gdk_animation_iter_class_init,
      NULL,           /* class_finalize */
      NULL,           /* class_data */
      sizeof (GstGdkAnimationIter),
      0,              /* n_preallocs */
      gst_gdk_animation_iter_init,
    };
    
    object_type = g_type_register_static (GDK_TYPE_PIXBUF_ANIMATION_ITER,
	    "GdkPixbufAniAnimIter", &object_info, 0);
  }
  
  return object_type;
}

static void
gst_gdk_animation_iter_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (g_class);
  GdkPixbufAnimationIterClass *anim_iter_class = GDK_PIXBUF_ANIMATION_ITER_CLASS (g_class);
  
  iter_parent_class = g_type_class_peek_parent (g_class);
  
  object_class->finalize = gst_gdk_animation_iter_finalize;
	
  anim_iter_class->get_delay_time = gst_gdk_animation_iter_get_delay_time;
  anim_iter_class->get_pixbuf = gst_gdk_animation_iter_get_pixbuf;
  anim_iter_class->on_currently_loading_frame = gst_gdk_animation_iter_on_currently_loading_frame;
  anim_iter_class->advance = gst_gdk_animation_iter_advance;
}
static void
gst_gdk_animation_iter_init (GTypeInstance *instance, gpointer g_class)
{
  GstGdkAnimationIter *iter = GST_GDK_ANIMATION_ITER (instance);

  iter->buffers = g_queue_new ();
  iter->eos = FALSE;
}
static void
gst_gdk_animation_iter_finalize (GObject *object)
{
  GstGdkAnimationIter *iter = GST_GDK_ANIMATION_ITER (object);
  
  g_object_unref (iter->ani);
  
  if (iter->pipeline)
    g_object_unref (iter->pipeline);
  if (iter->pixbuf)
    g_object_unref (iter->pixbuf);
  while (iter->buffers) {
    GstBuffer *buffer = GST_BUFFER (g_queue_pop_head (iter->buffers));
    if (buffer) {
      GST_LOG_OBJECT (iter, "unreffing buffer %p on finalize", buffer);
      gst_data_unref (GST_DATA (buffer));
    } else {
      g_queue_free (iter->buffers);
      iter->buffers = NULL;
    }
  }
  G_OBJECT_CLASS (iter_parent_class)->finalize (object);
}
static void
got_handoff (GstElement *fakesink, GstBuffer *buffer, GstGdkAnimationIter *iter)
{
  GST_LOG_OBJECT (iter, "enqueing buffer %p", buffer);
  gst_data_ref (GST_DATA (buffer));
  g_queue_push_tail (iter->buffers, buffer);
}
static GstElement *
gst_gdk_animation_iter_create_pipeline (GstGdkAnimationIter *iter)
{
  GstElement *ret;
  GstElement *src, *autoplugger, *sink, *colorspace;
  GstCaps *caps = GST_CAPS_NEW ("pixbuf_filter",
				"video/x-raw-rgb", 
				  "endianness", GST_PROPS_INT (G_BIG_ENDIAN),
				  "bpp",	GST_PROPS_LIST (
							GST_PROPS_INT (32),
							GST_PROPS_INT (24)
						),
				  "red_mask",	GST_PROPS_INT (0x000000FF),
				  "green_mask",	GST_PROPS_INT (0x0000FF00),
				  "blue_mask",	GST_PROPS_INT (0x00FF0000)
				);

  ret = gst_element_factory_make ("pipeline", "main_pipeline");
  if (!ret) return NULL;

  if (!(src = gst_element_factory_make ("filesrc", "source")))
    goto error;
  gst_bin_add (GST_BIN (ret), src);
  g_object_set (src, "location", iter->ani->temp_location, NULL);

  if (!(autoplugger = gst_element_factory_make ("spider", "autoplugger")))
    goto error;
  gst_bin_add (GST_BIN (ret), autoplugger);
  if (!gst_element_link (src, autoplugger))
    goto error;
  
  if (!(colorspace = gst_element_factory_make ("colorspace", "colorspace")))
    goto error;
  gst_bin_add (GST_BIN (ret), colorspace);
  if (!gst_element_link (autoplugger, colorspace))
    goto error;
  
  if (!(sink = gst_element_factory_make ("fakesink", "sink")))
    goto error;
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) got_handoff, iter);
  gst_bin_add (GST_BIN (ret), sink);
  if (!gst_element_link_filtered (colorspace, sink, caps))
    goto error;
  if (gst_element_set_state (ret, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    goto error;
  
  return ret;
error:
  g_object_unref (ret);
  return NULL;
}
static gboolean
gst_gdk_animation_iter_may_advance (GstGdkAnimationIter *iter)
{
  GstFormat bytes = GST_FORMAT_BYTES;
  gint64 offset;
  gint64 data_amount;
  
  if (iter->ani->temp_fd == 0)
    return TRUE;

  data_amount = lseek (iter->ani->temp_fd, 0, SEEK_CUR);
  g_assert (data_amount >= 0);
  g_assert (gst_element_query (gst_bin_get_by_name (GST_BIN (iter->pipeline), "source"),
			 GST_QUERY_POSITION, &bytes, &offset));
  if (data_amount - offset > GST_GDK_BUFFER_SIZE)
    return TRUE;

  return FALSE;
}
static gboolean
gst_gdk_animation_get_more_buffers (GstGdkAnimationIter *iter)
{
  GstBuffer *last = g_queue_peek_tail (iter->buffers);
  
  do {
    GST_LOG_OBJECT (iter, "iterating...");
    if (!gst_gdk_animation_iter_may_advance (iter)) {
      GST_LOG_OBJECT (iter, "no more data available");
      break;
    }
    if (!gst_bin_iterate (GST_BIN (iter->pipeline))) {
      GST_LOG_OBJECT (iter, "iterating done, setting EOS");
      iter->eos = TRUE;
      break;
    }
  } while (last == g_queue_peek_tail (iter->buffers));
  return last != g_queue_peek_tail (iter->buffers);
}
static void
pixbuf_destroy_notify (guchar *pixels, gpointer data)
{
  GST_LOG ("unreffing buffer %p because pixbuf was destroyed", data);
  gst_data_unref (GST_DATA (data));
}
static void
gst_gdk_animation_iter_create_pixbuf (GstGdkAnimationIter *iter)
{
  GstBuffer *buf;
  GstGdkAnimation *ani = iter->ani;
  
  buf = g_queue_pop_head (iter->buffers);
  g_assert (buf);
  if (iter->pixbuf) {
    GST_LOG_OBJECT (iter, "unreffing pixbuf %p", iter->pixbuf);
    g_object_unref (iter->pixbuf);
  }
  if (ani->width == 0) {
    GstPad *pad;
    GstCaps *caps;
    GstElement *fakesink = gst_bin_get_by_name (GST_BIN (iter->pipeline), "sink");
    g_assert (fakesink);
    pad = gst_element_get_pad (fakesink, "sink");
    g_assert (pad);
    caps = gst_pad_get_caps (pad);
    g_assert (caps);
    g_assert (GST_CAPS_IS_FIXED (caps));
    g_assert (gst_caps_has_fixed_property (caps, "bpp") &&
	      gst_caps_has_fixed_property (caps, "width") &&
	      gst_caps_has_fixed_property (caps, "height"));
    gst_caps_get_int (caps, "width", &ani->width);
    gst_caps_get_int (caps, "height", &ani->height);
    gst_caps_get_int (caps, "bpp", &ani->bpp);
    GST_DEBUG_OBJECT (ani, "found format (width %d, height %d, bpp %d)", ani->width, ani->height, ani->bpp);
  }
  g_assert (GST_BUFFER_SIZE (buf) == ani->width * ani->height * ani->bpp / 8);
  if (ani->bpp == 32) {
    gint i;
    guint32 *data = (guint32 *) GST_BUFFER_DATA (buf);
    /* ensure opacity */
    for (i = 0; i < ani->width * ani->height; i++) {
      data[i] |= 0xFF000000;
    }
  }
  iter->pixbuf = gdk_pixbuf_new_from_data (GST_BUFFER_DATA (buf),
	  GDK_COLORSPACE_RGB, ani->bpp == 32, 8, ani->width, ani->height, ani->width * ani->bpp / 8, pixbuf_destroy_notify, buf);
  GST_LOG_OBJECT (iter, "created pixbuf %p from buffer %p (refcount %d)", iter->pixbuf, buf, GST_DATA_REFCOUNT_VALUE (buf));
}
static GdkPixbufAnimationIter*
gst_gdk_animation_get_iter (GdkPixbufAnimation *anim, const GTimeVal *start_time)
{
  GstGdkAnimation *ani = GST_GDK_ANIMATION (anim);
  GstGdkAnimationIter *iter;

  if (ani->temp_fd != 0 && lseek (ani->temp_fd, 0, SEEK_CUR) < GST_GDK_BUFFER_SIZE)
    return NULL;

  iter = g_object_new (GST_TYPE_GDK_ANIMATION_ITER, NULL);

  iter->start = *start_time;
  
  iter->ani = ani;
  g_object_ref (ani);
  iter->pipeline = gst_gdk_animation_iter_create_pipeline (iter);
  if (iter->pipeline == NULL) 
    goto error;
    
        
  if (!gst_gdk_animation_get_more_buffers (iter))
    goto error;
  
  gst_gdk_animation_iter_create_pixbuf (iter);
  if (!ani->pixbuf) {
    /* set our static image */
    g_object_ref (iter->pixbuf);
    ani->pixbuf = iter->pixbuf;
  }
        
  return GDK_PIXBUF_ANIMATION_ITER (iter);

error:
  g_object_unref (iter);
  return NULL;
}
static gboolean
gst_gdk_animation_iter_advance (GdkPixbufAnimationIter *anim_iter, const GTimeVal *current_time)
{
  GstClockTime offset;
  GstBuffer *buffer = NULL;
  GstGdkAnimationIter *iter = GST_GDK_ANIMATION_ITER (anim_iter);

  /* compute timestamp that next buffer must match */
  GST_DEBUG_OBJECT (iter, "advancing to %ld:%ld (started at %ld:%ld)", 
		    current_time->tv_sec, current_time->tv_usec, iter->start.tv_sec, iter->start.tv_usec);
  offset = ((GstClockTime) current_time->tv_sec - iter->start.tv_sec) * GST_SECOND;
  if (iter->start.tv_usec > current_time->tv_usec) {
    offset -= ((GstClockTime) iter->start.tv_usec - current_time->tv_usec) * GST_SECOND / G_USEC_PER_SEC;
  } else {
    offset += ((GstClockTime) current_time->tv_usec - iter->start.tv_usec) * GST_SECOND / G_USEC_PER_SEC;
  }
  
  while (TRUE) {
    if (g_queue_is_empty (iter->buffers)) {
      if (iter->eos)
	return FALSE;
      if (gst_gdk_animation_get_more_buffers (iter))
	continue;
      break;
    }
    if (GST_BUFFER_TIMESTAMP (g_queue_peek_head (iter->buffers)) > offset)
      break;
    if (buffer) {
      GST_LOG_OBJECT (iter, "unreffing buffer %p, because timestamp too low (%"G_GUINT64_FORMAT" vs %"G_GUINT64_FORMAT")",
		      buffer, GST_BUFFER_TIMESTAMP (buffer), offset);
      gst_data_unref (GST_DATA (buffer));
    }
    buffer = GST_BUFFER (g_queue_pop_head (iter->buffers));
  }
  if (!buffer)
    return FALSE;
  iter->last_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  g_queue_push_head (iter->buffers, buffer);
  gst_gdk_animation_iter_create_pixbuf (iter);
  return TRUE;
}
static gint
gst_gdk_animation_iter_get_delay_time (GdkPixbufAnimationIter *anim_iter)
{
  gint delay;
  GstGdkAnimationIter *iter = GST_GDK_ANIMATION_ITER (anim_iter);
  
  while (g_queue_is_empty (iter->buffers)) {
    if (iter->eos) {
      GST_LOG_OBJECT (iter, "returning delay of infinite, we're EOS");
      return -1;
    }
    if (!gst_gdk_animation_get_more_buffers (iter))
      return -1; /* FIXME? */
  }

  delay = (GST_BUFFER_TIMESTAMP (g_queue_peek_head (iter->buffers)) - iter->last_timestamp) *
          1000 / GST_SECOND;
  GST_LOG_OBJECT (iter, "returning delay of %d ms", delay);
  return delay;
}
GdkPixbuf*
gst_gdk_animation_iter_get_pixbuf (GdkPixbufAnimationIter *anim_iter)
{
  GstGdkAnimationIter *iter = GST_GDK_ANIMATION_ITER (anim_iter);
  
  GST_LOG_OBJECT (iter, "returning pixbuf %p", iter->pixbuf);
  return iter->pixbuf;
}
static gboolean
gst_gdk_animation_iter_on_currently_loading_frame (GdkPixbufAnimationIter *anim_iter)
{
  GstGdkAnimationIter *iter = GST_GDK_ANIMATION_ITER (anim_iter);
  
  /* EOS - last frame */
  if (iter->eos && g_queue_is_empty (iter->buffers))
    return TRUE;

  /* can't load more frames */
  if (!gst_gdk_animation_iter_may_advance (iter))
    return FALSE;
  
  return TRUE;
}
