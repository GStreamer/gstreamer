/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gst_loader.c: Load GStreamer videos as gdkpixbufs
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

#include "gstgdkanimation.h"
#include <gst/gstinfo.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


typedef struct
{
  /* stuff gdk throws at us and we're supposed to keep */
  GdkPixbufModuleSizeFunc size_func;
  GdkPixbufModulePreparedFunc prepared_func;
  GdkPixbufModuleUpdatedFunc updated_func;
  gpointer user_data;
  /* our own stuff - we're much better at keeping fields small :p */
  GstGdkAnimation *ani;
  gboolean initialized;
} GstLoaderContext;

GST_DEBUG_CATEGORY_STATIC (gst_loader_debug);
#define GST_CAT_DEFAULT gst_loader_debug

static gboolean
gst_loader_init (GError ** error)
{
  static gboolean inited = FALSE;

  if (inited)
    return TRUE;

  if (!g_thread_supported ()) {
    g_set_error (error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED,
	"The GStreamer loader requires threading support.");
    return FALSE;
  }

  if (!gst_init_check (0, NULL)) {
    g_set_error (error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED,
	"GStreamer could not be initialized.");
    return FALSE;
  }

  inited = TRUE;
  GST_DEBUG_CATEGORY_INIT (gst_loader_debug, "gstloader", 0,
      "entry point debugging for the GStreamer gdk pixbuf loader");
  return TRUE;
}

static gpointer
gst_loader_begin_load (GdkPixbufModuleSizeFunc size_func,
    GdkPixbufModulePreparedFunc prepared_func,
    GdkPixbufModuleUpdatedFunc updated_func, gpointer user_data,
    GError ** error)
{
  GstLoaderContext *context;

  if (!gst_loader_init (error))
    return NULL;

  context = g_new (GstLoaderContext, 1);
  context->size_func = size_func;
  context->prepared_func = prepared_func;
  context->updated_func = updated_func;
  context->user_data = user_data;
  context->ani = gst_gdk_animation_new (error);
  context->initialized = FALSE;

  if (!context->ani) {
    GST_WARNING ("creating animation failed");
    g_free (context);
    return NULL;
  }
  context->ani->temp_fd =
      g_file_open_tmp (NULL, &context->ani->temp_location, error);
  if (context->ani->temp_fd == 0) {
    g_object_unref (context->ani);
    g_free (context);
    return NULL;
  }

  GST_LOG_OBJECT (context->ani, "begin loading");
  return context;
}

static gboolean
gst_loader_load_increment (gpointer context_pointer, const guchar * buf,
    guint size, GError ** error)
{
  GdkPixbufAnimationIter *iter;
  GstLoaderContext *context = (GstLoaderContext *) context_pointer;

  GST_LOG_OBJECT (context->ani, "load increment: %u bytes", size);
  gst_gdk_animation_add_data (context->ani, buf, size);
  if (!context->initialized
      && (iter =
	  gdk_pixbuf_animation_get_iter (GDK_PIXBUF_ANIMATION (context->ani),
	      NULL)) != NULL) {
    int width =
	gdk_pixbuf_animation_get_width (GDK_PIXBUF_ANIMATION (context->ani));
    int height =
	gdk_pixbuf_animation_get_height (GDK_PIXBUF_ANIMATION (context->ani));
    GdkPixbuf *pixbuf =
	gdk_pixbuf_animation_get_static_image (GDK_PIXBUF_ANIMATION (context->
	    ani));

    g_object_unref (iter);
    GST_LOG_OBJECT (context->ani, "initializing loader");
    if (context->size_func) {
      GST_LOG_OBJECT (context->ani, "calling size_func %p", context->size_func);
      context->size_func (&width, &height, context->user_data);
    }

    if (context->prepared_func) {
      GST_LOG_OBJECT (context->ani, "calling prepared_func %p",
	  context->prepared_func);
      context->prepared_func (pixbuf, GDK_PIXBUF_ANIMATION (context->ani),
	  context->user_data);
    }

    context->initialized = TRUE;
  }

  return TRUE;
}

static gboolean
gst_loader_stop_load (gpointer context_pointer, GError ** error)
{
  GstLoaderContext *context = (GstLoaderContext *) context_pointer;

  GST_LOG_OBJECT (context->ani, "stop loading");
  gst_gdk_animation_done_adding (context->ani);
  g_object_unref (context->ani);
  g_free (context);

  return TRUE;
}

static GdkPixbufAnimation *
gst_loader_load_animation (FILE * f, GError ** error)
{
  gchar *filename;
  GstGdkAnimation *ani;
  GdkPixbufAnimationIter *iter = NULL;

  if (!gst_loader_init (error))
    return NULL;

  GST_LOG ("load_animation");
  ani = gst_gdk_animation_new (error);
  if (!ani)
    return NULL;

  filename = g_strdup_printf ("/proc/self/fd/%d", fileno (f));
  ani->temp_fd = open (filename, 0);
  if (ani->temp_fd >= 0) {
    iter = gdk_pixbuf_animation_get_iter (GDK_PIXBUF_ANIMATION (ani), NULL);
  } else {
    GST_DEBUG ("open (\"%s\", 0) failed", filename);
  }
  g_free (filename);
  if (iter == NULL) {
    g_set_error (error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
	"could not create an image");
    g_object_unref (ani);
    GST_INFO ("could not create an image");
    return NULL;
  }
  g_object_unref (iter);
  GST_LOG_OBJECT (ani, "load_animation succeeded");
  return GDK_PIXBUF_ANIMATION (ani);
}
static GdkPixbuf *
gst_loader_load (FILE * f, GError ** error)
{
  GdkPixbufAnimation *ani;
  GdkPixbuf *pixbuf;

  ani = gst_loader_load_animation (f, error);
  if (ani == NULL)
    return NULL;

  pixbuf = gdk_pixbuf_animation_get_static_image (ani);
  if (!pixbuf) {
    GST_ERROR_OBJECT (ani, "Could not get an image in _pixbuf_load");
    g_object_unref (ani);
    g_set_error (error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
	"Could not get an image from file.");
    return NULL;
  }

  g_object_ref (pixbuf);
  g_object_unref (ani);

  return pixbuf;
}

void
fill_vtable (GdkPixbufModule * module)
{
  module->load = gst_loader_load;
  module->begin_load = gst_loader_begin_load;
  module->load_increment = gst_loader_load_increment;
  module->stop_load = gst_loader_stop_load;
  module->load_animation = gst_loader_load_animation;
}

void
fill_info (GdkPixbufFormat * info)
{
  static GdkPixbufModulePattern signature[] = {
    /* AVI */
    {"RIFF    AVI ", "    xxxx    ", 100},
    /* MPEG 1 */
    {"xx\001\272", "zz  ", 100},
    /* MPEG 2 */
    {"xx\001\263", "zz  ", 100},
    /* Quicktime */
    {"    wide", "xxxx    ", 80},
    {"    moov", "xxxx    ", 80},
    {"    mdat", "xxxx    ", 80},
    {"    pnot", "xxxx    ", 80},
    {"    PICT", "xxxx    ", 80},
    {"    free", "xxxx    ", 80},
    /* ASF */
    {"\060\046\262\165\216\146\317\021\246\331 \252 \142\316\154",
	"          z z   ", 100},
    {NULL, NULL, 0}
  };

  static gchar *mime_types[] = {
    "video/avi", "video/x-avi", "video/x-msvideo",
    "video/mpeg",
    "video/quicktime",
    "video/x-ms-asf",
    NULL
  };

  static gchar *extensions[] = {
    "avi",
    "mpeg", "mpe", "mpg",
    "mov",
    "asf", "wmv",
    NULL
  };

  info->name = "GStreamer";
  info->signature = signature;
  info->description = "GStreamer supported video";
  info->mime_types = mime_types;
  info->extensions = extensions;
  info->flags = 0;
}
