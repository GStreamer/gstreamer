/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gstasyncdisksrc.h>


GstElementDetails gst_asyncdisksrc_details = {
  "Asynchronous Disk Source",
  "Source/File",
  "Read from arbitrary point in a file",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* AsyncDiskSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_BYTESPERREAD,
  ARG_LENGTH,
  ARG_OFFSET,
};


static void gst_asyncdisksrc_class_init(GstAsyncDiskSrcClass *klass);
static void gst_asyncdisksrc_init(GstAsyncDiskSrc *asyncdisksrc);
static void gst_asyncdisksrc_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_asyncdisksrc_get_arg(GtkObject *object,GtkArg *arg,guint id);

static void gst_asyncdisksrc_push(GstSrc *src);
static void gst_asyncdisksrc_push_region(GstSrc *src,gulong offset,
                                         gulong size);
static gboolean gst_asyncdisksrc_change_state(GstElement *element,
                                              GstElementState state);


static GstSrcClass *parent_class = NULL;
//static guint gst_asyncdisksrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_asyncdisksrc_get_type(void) {
  static GtkType asyncdisksrc_type = 0;

  if (!asyncdisksrc_type) {
    static const GtkTypeInfo asyncdisksrc_info = {
      "GstAsyncDiskSrc",
      sizeof(GstAsyncDiskSrc),
      sizeof(GstAsyncDiskSrcClass),
      (GtkClassInitFunc)gst_asyncdisksrc_class_init,
      (GtkObjectInitFunc)gst_asyncdisksrc_init,
      (GtkArgSetFunc)gst_asyncdisksrc_set_arg,
      (GtkArgGetFunc)gst_asyncdisksrc_get_arg,
      (GtkClassInitFunc)NULL,
    };
    asyncdisksrc_type = gtk_type_unique(GST_TYPE_SRC,&asyncdisksrc_info);
  }
  return asyncdisksrc_type;
}

static void
gst_asyncdisksrc_class_init(GstAsyncDiskSrcClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;
  GstSrcClass *gstsrc_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;
  gstsrc_class = (GstSrcClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_SRC);

  gtk_object_add_arg_type("GstAsyncDiskSrc::location", GTK_TYPE_STRING,
                          GTK_ARG_READWRITE, ARG_LOCATION);
  gtk_object_add_arg_type("GstAsyncDiskSrc::bytesperread", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_BYTESPERREAD);
  gtk_object_add_arg_type("GstAsyncDiskSrc::length", GTK_TYPE_LONG,
                          GTK_ARG_READABLE, ARG_LENGTH);
  gtk_object_add_arg_type("GstAsyncDiskSrc::offset", GTK_TYPE_LONG,
                          GTK_ARG_READWRITE, ARG_OFFSET);

  gtkobject_class->set_arg = gst_asyncdisksrc_set_arg;
  gtkobject_class->get_arg = gst_asyncdisksrc_get_arg;

  gstelement_class->change_state = gst_asyncdisksrc_change_state;

  gstsrc_class->push = gst_asyncdisksrc_push;
  gstsrc_class->push_region = gst_asyncdisksrc_push_region;
}

static void gst_asyncdisksrc_init(GstAsyncDiskSrc *asyncdisksrc) {
  GST_SRC_SET_FLAGS(asyncdisksrc,GST_SRC_ASYNC);

  asyncdisksrc->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(asyncdisksrc),asyncdisksrc->srcpad);

  asyncdisksrc->filename = NULL;
  asyncdisksrc->fd = 0;
  asyncdisksrc->size = 0;
  asyncdisksrc->map = NULL;
  asyncdisksrc->curoffset = 0;
  asyncdisksrc->bytes_per_read = 4096;
  asyncdisksrc->seq = 0;
}


static void gst_asyncdisksrc_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstAsyncDiskSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ASYNCDISKSRC(object));
  src = GST_ASYNCDISKSRC(object);

  switch(id) {
    case ARG_LOCATION:
      /* the element must be stopped in order to do this */
      g_return_if_fail(!GST_FLAG_IS_SET(src,GST_ASYNCDISKSRC_OPEN));

      if (src->filename) g_free(src->filename);
      /* clear the filename if we get a NULL (is that possible?) */
      if (GTK_VALUE_STRING(*arg) == NULL) {
        src->filename = NULL;
        gst_element_set_state(GST_ELEMENT(object),~GST_STATE_COMPLETE);
      /* otherwise set the new filename */
      } else {
        src->filename = g_strdup(GTK_VALUE_STRING(*arg));
        gst_element_set_state(GST_ELEMENT(object),GST_STATE_COMPLETE);
      }
      break;
    case ARG_BYTESPERREAD:
      src->bytes_per_read = GTK_VALUE_INT(*arg);
      break;
    case ARG_OFFSET:
      src->curoffset = GTK_VALUE_LONG(*arg);
      break;
    default:
      break;
  }
}

static void gst_asyncdisksrc_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstAsyncDiskSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ASYNCDISKSRC(object));
  src = GST_ASYNCDISKSRC(object);

  switch (id) {
    case ARG_LOCATION:
      GTK_VALUE_STRING(*arg) = src->filename;
      break;
    case ARG_BYTESPERREAD:
      GTK_VALUE_INT(*arg) = src->bytes_per_read;
      break;
    case ARG_LENGTH:
      GTK_VALUE_LONG(*arg) = src->size;
      break;
    case ARG_OFFSET:
      GTK_VALUE_LONG(*arg) = src->curoffset;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

/**
 * gst_asyncdisksrc_push:
 * @src: #GstSrc to push a buffer from
 *
 * Push a new buffer from the asyncdisksrc at the current offset.
 */
void gst_asyncdisksrc_push(GstSrc *src) {
  GstAsyncDiskSrc *asyncdisksrc;
  GstBuffer *buf;

  g_return_if_fail(src != NULL);
  g_return_if_fail(GST_IS_ASYNCDISKSRC(src));
  g_return_if_fail(GST_FLAG_IS_SET(src,GST_ASYNCDISKSRC_OPEN));
  asyncdisksrc = GST_ASYNCDISKSRC(src);

  /* deal with EOF state */
  if (asyncdisksrc->curoffset >= asyncdisksrc->size) {
    gst_src_signal_eos(GST_SRC(asyncdisksrc));
    return;
  }

  /* create the buffer */
  // FIXME: should eventually use a bufferpool for this
  buf = gst_buffer_new();
  g_return_if_fail(buf != NULL);

  /* simply set the buffer to point to the correct region of the file */
  GST_BUFFER_DATA(buf) = asyncdisksrc->map + asyncdisksrc->curoffset;
  GST_BUFFER_OFFSET(buf) = asyncdisksrc->curoffset;
  GST_BUFFER_FLAG_SET(buf, GST_BUFFER_DONTFREE);

  if ((asyncdisksrc->curoffset + asyncdisksrc->bytes_per_read) >
      asyncdisksrc->size) {
    GST_BUFFER_SIZE(buf) = asyncdisksrc->size - asyncdisksrc->curoffset;
    // FIXME: set the buffer's EOF bit here
  } else
    GST_BUFFER_SIZE(buf) = asyncdisksrc->bytes_per_read;
  asyncdisksrc->curoffset += GST_BUFFER_SIZE(buf);

  //gst_buffer_ref(buf);

  /* we're done, push the buffer off now */
  gst_pad_push(asyncdisksrc->srcpad,buf);
}

/**
 * gst_asyncdisksrc_push_region:
 * @src: #GstSrc to push a buffer from
 * @offset: offset in file
 * @size: number of bytes
 *
 * Push a new buffer from the asyncdisksrc of given size at given offset.
 */
void gst_asyncdisksrc_push_region(GstSrc *src,gulong offset,gulong size) {
  GstAsyncDiskSrc *asyncdisksrc;
  GstBuffer *buf;

  g_return_if_fail(src != NULL);
  g_return_if_fail(GST_IS_ASYNCDISKSRC(src));
  g_return_if_fail(GST_FLAG_IS_SET(src,GST_STATE_RUNNING));
  asyncdisksrc = GST_ASYNCDISKSRC(src);

  /* deal with EOF state */
  if (offset >= asyncdisksrc->size) {
    gst_src_signal_eos(GST_SRC(asyncdisksrc));
    return;
  }

  /* create the buffer */
  // FIXME: should eventually use a bufferpool for this
  buf = gst_buffer_new();
  g_return_if_fail(buf);

  /* simply set the buffer to point to the correct region of the file */
  GST_BUFFER_DATA(buf) = asyncdisksrc->map + offset;
  GST_BUFFER_OFFSET(buf) = asyncdisksrc->curoffset;
  GST_BUFFER_FLAG_SET(buf, GST_BUFFER_DONTFREE);

  if ((offset + size) > asyncdisksrc->size) {
    GST_BUFFER_SIZE(buf) = asyncdisksrc->size - offset;
    // FIXME: set the buffer's EOF bit here
  } else
    GST_BUFFER_SIZE(buf) = size;
  asyncdisksrc->curoffset += GST_BUFFER_SIZE(buf);

  /* we're done, push the buffer off now */
  gst_pad_push(asyncdisksrc->srcpad,buf);
}


/* open the file and mmap it, necessary to go to RUNNING state */
static gboolean gst_asyncdisksrc_open_file(GstAsyncDiskSrc *src) {
  g_return_val_if_fail(!GST_FLAG_IS_SET(src,GST_ASYNCDISKSRC_OPEN), FALSE);

  /* open the file */
  src->fd = open(src->filename,O_RDONLY);
  if (src->fd < 0) {
    gst_element_error(GST_ELEMENT(src),"opening file");
    return FALSE;
  } else {
    /* find the file length */
    src->size = lseek(src->fd,0,SEEK_END);
    lseek(src->fd,0,SEEK_SET);
    /* map the file into memory */
    src->map = mmap(NULL,src->size,PROT_READ,MAP_SHARED,src->fd,0);
    madvise(src->map,src->size,2);
    /* collapse state if that failed */
    if (src->map == NULL) {
      close(src->fd);
      gst_element_error(GST_ELEMENT(src),"mmapping file");
      return FALSE;
    }
    GST_FLAG_SET(src,GST_ASYNCDISKSRC_OPEN);
  }
  return TRUE;
}

/* unmap and close the file */
static void gst_asyncdisksrc_close_file(GstAsyncDiskSrc *src) {
  g_return_if_fail(GST_FLAG_IS_SET(src,GST_ASYNCDISKSRC_OPEN));

  /* unmap the file from memory */
  munmap(src->map,src->size);
  /* close the file */
  close(src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->size = 0;
  src->map = NULL;
  src->curoffset = 0;
  src->seq = 0;

  GST_FLAG_UNSET(src,GST_ASYNCDISKSRC_OPEN);
}


static gboolean gst_asyncdisksrc_change_state(GstElement *element,
                                              GstElementState state) {
  g_return_val_if_fail(GST_IS_ASYNCDISKSRC(element), FALSE);

  switch (state) {
    case GST_STATE_RUNNING:
      if (!gst_asyncdisksrc_open_file(GST_ASYNCDISKSRC(element)))
        return FALSE;
      break;
    case ~GST_STATE_RUNNING:
      gst_asyncdisksrc_close_file(GST_ASYNCDISKSRC(element));
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element,state);
  return TRUE;
}
