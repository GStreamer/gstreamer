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
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <gstfdsrc.h>


GstElementDetails gst_fdsrc_details = {
  "Disk Source",
  "Source/File",
  "Synchronous read from a file",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* FdSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_BYTESPERREAD,
  ARG_OFFSET,
};


static void gst_fdsrc_class_init(GstFdSrcClass *klass);
static void gst_fdsrc_init(GstFdSrc *fdsrc);
static void gst_fdsrc_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_fdsrc_get_arg(GtkObject *object,GtkArg *arg,guint id);

static void gst_fdsrc_push(GstSrc *src);
//static void gst_fdsrc_push_region(GstSrc *src,gulong offset,gulong size);


static GstSrcClass *parent_class = NULL;
//static guint gst_fdsrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_fdsrc_get_type(void) {
  static GtkType fdsrc_type = 0;

  if (!fdsrc_type) {
    static const GtkTypeInfo fdsrc_info = {
      "GstFdSrc",
      sizeof(GstFdSrc),
      sizeof(GstFdSrcClass),
      (GtkClassInitFunc)gst_fdsrc_class_init,
      (GtkObjectInitFunc)gst_fdsrc_init,
      (GtkArgSetFunc)gst_fdsrc_set_arg,
      (GtkArgGetFunc)gst_fdsrc_get_arg,
      (GtkClassInitFunc)NULL,
    };
    fdsrc_type = gtk_type_unique(GST_TYPE_SRC,&fdsrc_info);
  }
  return fdsrc_type;
}

static void
gst_fdsrc_class_init(GstFdSrcClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstSrcClass *gstsrc_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstsrc_class = (GstSrcClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_SRC);

  gtk_object_add_arg_type("GstFdSrc::location", GTK_TYPE_STRING,
                          GTK_ARG_WRITABLE, ARG_LOCATION);
  gtk_object_add_arg_type("GstFdSrc::bytesperread", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_BYTESPERREAD);
  gtk_object_add_arg_type("GstFdSrc::offset", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_OFFSET);

  gtkobject_class->set_arg = gst_fdsrc_set_arg;
  gtkobject_class->get_arg = gst_fdsrc_get_arg;

  gstsrc_class->push = gst_fdsrc_push;
  /* we nominally can't (won't) do async */
  gstsrc_class->push_region = NULL;
}

static void gst_fdsrc_init(GstFdSrc *fdsrc) {
  fdsrc->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(fdsrc),fdsrc->srcpad);

  fdsrc->fd = 0;
  fdsrc->curoffset = 0;
  fdsrc->bytes_per_read = 4096;
  fdsrc->seq = 0;
}


static void gst_fdsrc_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstFdSrc *src;
  int fd;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_FDSRC(object));
  src = GST_FDSRC(object);

  switch(id) {
    case ARG_LOCATION:
      /* the element must be stopped in order to do this */
      g_return_if_fail(!GST_FLAG_IS_SET(src,GST_STATE_RUNNING));

      /* if we get a NULL, consider it to be a fd of 0 */
      if (GTK_VALUE_STRING(*arg) == NULL) {
        src->fd = 0;
        gst_element_set_state(GST_ELEMENT(object),~GST_STATE_COMPLETE);
      /* otherwise set the new filename */
      } else {
        if (sscanf(GTK_VALUE_STRING(*arg),"%d",&fd))
          src->fd = fd;
        gst_element_set_state(GST_ELEMENT(object),GST_STATE_COMPLETE);
      }
      break;
    case ARG_BYTESPERREAD:
      src->bytes_per_read = GTK_VALUE_INT(*arg);
      break;
    default:
      break;
  }
}

static void gst_fdsrc_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstFdSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_FDSRC(object));
  src = GST_FDSRC(object);

  switch (id) {
    case ARG_BYTESPERREAD:
      GTK_VALUE_INT(*arg) = src->bytes_per_read;
      break;
    case ARG_OFFSET:
      GTK_VALUE_INT(*arg) = src->curoffset;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

void gst_fdsrc_push(GstSrc *src) {
  GstFdSrc *fdsrc;
  GstBuffer *buf;
  glong readbytes;

  g_return_if_fail(src != NULL);
  g_return_if_fail(GST_IS_FDSRC(src));
  fdsrc = GST_FDSRC(src);

  /* create the buffer */
  // FIXME: should eventually use a bufferpool for this
  buf = gst_buffer_new();
  g_return_if_fail(buf);

  /* allocate the space for the buffer data */
  GST_BUFFER_DATA(buf) = g_malloc(fdsrc->bytes_per_read);
  g_return_if_fail(GST_BUFFER_DATA(buf) != NULL);

  /* read it in from the file */
  readbytes = read(fdsrc->fd,GST_BUFFER_DATA(buf),fdsrc->bytes_per_read);
  if (readbytes == 0) {
    gst_src_signal_eos(GST_SRC(fdsrc));
    return;
  }

  /* if we didn't get as many bytes as we asked for, we're at EOF */
  if (readbytes < fdsrc->bytes_per_read) {
    // FIXME: set the buffer's EOF bit here
  }
  GST_BUFFER_OFFSET(buf) = fdsrc->curoffset;
  GST_BUFFER_SIZE(buf) = readbytes;
  fdsrc->curoffset += readbytes;

  /* we're done, push the buffer off now */
  gst_pad_push(fdsrc->srcpad,buf);
}
