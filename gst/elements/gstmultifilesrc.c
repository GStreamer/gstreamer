/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Dominic Ludlam <dom@recoil.org>
 *
 * gstmultidisksrc.c:
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

//#define GST_DEBUG_ENABLED

#include "gstmultidisksrc.h"

GstElementDetails gst_multidisksrc_details = {
  "Multi Disk Source",
  "Source/File",
  "Read from multiple files in order",
  VERSION,
  "Dominic Ludlam <dom@openfx.org>",
  "(C) 2001",
};

/* DiskSrc signals and args */
enum {
  NEW_FILE,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATIONS,
};

static void		gst_multidisksrc_class_init	(GstMultiDiskSrcClass *klass);
static void		gst_multidisksrc_init		(GstMultiDiskSrc *disksrc);

static void		gst_multidisksrc_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void		gst_multidisksrc_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static GstBuffer *	gst_multidisksrc_get		(GstPad *pad);
//static GstBuffer *	gst_multidisksrc_get_region	(GstPad *pad,GstRegionType type,guint64 offset,guint64 len);

static GstElementStateReturn	gst_multidisksrc_change_state	(GstElement *element);

static gboolean		gst_multidisksrc_open_file	(GstMultiDiskSrc *src, GstPad *srcpad);
static void		gst_multidisksrc_close_file	(GstMultiDiskSrc *src);

static GstElementClass *parent_class = NULL;
static guint gst_multidisksrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_multidisksrc_get_type(void)
{
  static GtkType multidisksrc_type = 0;

  if (!multidisksrc_type) {
    static const GtkTypeInfo multidisksrc_info = {
      "GstMultiDiskSrc",
      sizeof(GstMultiDiskSrc),
      sizeof(GstMultiDiskSrcClass),
      (GtkClassInitFunc)gst_multidisksrc_class_init,
      (GtkObjectInitFunc)gst_multidisksrc_init,
      (GtkArgSetFunc)gst_multidisksrc_set_arg,
      (GtkArgGetFunc)gst_multidisksrc_get_arg,
      (GtkClassInitFunc)NULL,
    };
    multidisksrc_type = gtk_type_unique (GST_TYPE_ELEMENT, &multidisksrc_info);
  }
  return multidisksrc_type;
}

static void
gst_multidisksrc_class_init (GstMultiDiskSrcClass *klass)
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gst_multidisksrc_signals[NEW_FILE] =
    gtk_signal_new ("new_file", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstMultiDiskSrcClass, new_file),
                    gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
                    GTK_TYPE_POINTER);
  gtk_object_class_add_signals (gtkobject_class, gst_multidisksrc_signals, LAST_SIGNAL);

  gtk_object_add_arg_type ("GstMultiDiskSrc::locations", GTK_TYPE_POINTER,
                           GTK_ARG_READWRITE, ARG_LOCATIONS);

  gtkobject_class->set_arg = gst_multidisksrc_set_arg;
  gtkobject_class->get_arg = gst_multidisksrc_get_arg;

  gstelement_class->change_state = gst_multidisksrc_change_state;
}

static void
gst_multidisksrc_init (GstMultiDiskSrc *multidisksrc)
{
//  GST_FLAG_SET (disksrc, GST_SRC_);

  multidisksrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (multidisksrc->srcpad,gst_multidisksrc_get);
//  gst_pad_set_getregion_function (multidisksrc->srcpad,gst_multidisksrc_get_region);
  gst_element_add_pad (GST_ELEMENT (multidisksrc), multidisksrc->srcpad);

  multidisksrc->listptr = NULL;
  multidisksrc->currentfilename = NULL;
  multidisksrc->fd = 0;
  multidisksrc->size = 0;
  multidisksrc->map = NULL;
  multidisksrc->new_seek = FALSE;
}

static void
gst_multidisksrc_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstMultiDiskSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MULTIDISKSRC (object));

  src = GST_MULTIDISKSRC (object);

  switch(id) {
    case ARG_LOCATIONS:
      /* the element must be stopped in order to do this */
      g_return_if_fail (GST_STATE (src) < GST_STATE_PLAYING);

      /* clear the filename if we get a NULL */
      if (GTK_VALUE_POINTER (*arg) == NULL) {
        gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);
        src->listptr = NULL;
      /* otherwise set the new filenames */
      } else {
        src->listptr = GTK_VALUE_POINTER(*arg);
      }
      break;
    default:
      break;
  }
}

static void
gst_multidisksrc_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstMultiDiskSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MULTIDISKSRC (object));

  src = GST_MULTIDISKSRC (object);

  switch (id) {
    case ARG_LOCATIONS:
      GTK_VALUE_POINTER (*arg) = src->listptr;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

/**
 * gst_disksrc_get:
 * @pad: #GstPad to push a buffer from
 *
 * Push a new buffer from the disksrc at the current offset.
 */
static GstBuffer *
gst_multidisksrc_get (GstPad *pad)
{
  GstMultiDiskSrc *src;
  GstBuffer *buf;
  GSList *list;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_MULTIDISKSRC (gst_pad_get_parent (pad));

  if (GST_FLAG_IS_SET (src, GST_MULTIDISKSRC_OPEN))
    gst_multidisksrc_close_file(src);

  if (!src->listptr) {
      gst_pad_set_eos(pad);
      return FALSE;
  }

  list = src->listptr;
  src->currentfilename = (gchar *) list->data;
  src->listptr = src->listptr->next;

  if (!gst_multidisksrc_open_file(src, pad))
      return NULL;

  // emitted after the open, as the user may free the list and string from here
  gtk_signal_emit(GTK_OBJECT(src), gst_multidisksrc_signals[NEW_FILE], list);

  /* create the buffer */
  // FIXME: should eventually use a bufferpool for this
  buf = gst_buffer_new ();

  g_return_val_if_fail (buf != NULL, NULL);

  /* simply set the buffer to point to the correct region of the file */
  GST_BUFFER_DATA (buf) = src->map;
  GST_BUFFER_OFFSET (buf) = 0;
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_DONTFREE);

  if (src->new_seek) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLUSH);
    src->new_seek = FALSE;
  }

  /* we're done, return the buffer */
  return buf;
}

/* open the file and mmap it, necessary to go to READY state */
static
gboolean gst_multidisksrc_open_file (GstMultiDiskSrc *src, GstPad *srcpad)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (src, GST_MULTIDISKSRC_OPEN), FALSE);

  /* open the file */
  src->fd = open ((const char *) src->currentfilename, O_RDONLY);

  if (src->fd < 0) {
    perror ("open");
    gst_element_error (GST_ELEMENT (src), g_strconcat("opening file \"", src->currentfilename, "\"", NULL));
    return FALSE;
  } else {
    /* find the file length */
    src->size = lseek (src->fd, 0, SEEK_END);
    lseek (src->fd, 0, SEEK_SET);
    /* map the file into memory */
    src->map = mmap (NULL, src->size, PROT_READ, MAP_SHARED, src->fd, 0);
    madvise (src->map,src->size, 2);
    /* collapse state if that failed */
    if (src->map == NULL) {
      close (src->fd);
      gst_element_error (GST_ELEMENT (src),"mmapping file");
      return FALSE;
    }
    GST_FLAG_SET (src, GST_MULTIDISKSRC_OPEN);
    src->new_seek = TRUE;
  }
  return TRUE;
}

/* unmap and close the file */
static void
gst_multidisksrc_close_file (GstMultiDiskSrc *src)
{
  g_return_if_fail (GST_FLAG_IS_SET (src, GST_MULTIDISKSRC_OPEN));

  /* unmap the file from memory and close the file */
  munmap (src->map, src->size);
  close (src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->size = 0;
  src->map = NULL;
  src->new_seek = FALSE;

  GST_FLAG_UNSET (src, GST_MULTIDISKSRC_OPEN);
}

static GstElementStateReturn
gst_multidisksrc_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_MULTIDISKSRC (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_MULTIDISKSRC_OPEN))
      gst_multidisksrc_close_file (GST_MULTIDISKSRC (element));
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
