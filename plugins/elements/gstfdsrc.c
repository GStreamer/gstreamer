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


static void		gst_fdsrc_class_init	(GstFdSrcClass *klass);
static void		gst_fdsrc_init		(GstFdSrc *fdsrc);

static void		gst_fdsrc_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void		gst_fdsrc_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static GstBuffer *	gst_fdsrc_get		(GstPad *pad);


static GstSrcClass *parent_class = NULL;
//static guint gst_fdsrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_fdsrc_get_type (void) 
{
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
    fdsrc_type = gtk_type_unique (GST_TYPE_SRC, &fdsrc_info);
  }
  return fdsrc_type;
}

static void
gst_fdsrc_class_init (GstFdSrcClass *klass) 
{
  GtkObjectClass *gtkobject_class;
  GstSrcClass *gstsrc_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstsrc_class = (GstSrcClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_SRC);

  gtk_object_add_arg_type ("GstFdSrc::location", GST_TYPE_FILENAME,
                           GTK_ARG_WRITABLE, ARG_LOCATION);
  gtk_object_add_arg_type ("GstFdSrc::bytesperread", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_BYTESPERREAD);
  gtk_object_add_arg_type ("GstFdSrc::offset", GTK_TYPE_INT,
                           GTK_ARG_READABLE, ARG_OFFSET);

  gtkobject_class->set_arg = gst_fdsrc_set_arg;
  gtkobject_class->get_arg = gst_fdsrc_get_arg;
}

static void gst_fdsrc_init(GstFdSrc *fdsrc) {
  fdsrc->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_pad_set_get_function(fdsrc->srcpad,gst_fdsrc_get);
  gst_element_add_pad(GST_ELEMENT(fdsrc),fdsrc->srcpad);

  fdsrc->fd = 0;
  fdsrc->curoffset = 0;
  fdsrc->bytes_per_read = 4096;
  fdsrc->seq = 0;
}


static void 
gst_fdsrc_set_arg (GtkObject *object, GtkArg *arg, guint id) 
{
  GstFdSrc *src;
  int fd;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSRC (object));
  
  src = GST_FDSRC (object);

  switch(id) {
    case ARG_LOCATION:
      /* the element must not be playing in order to do this */
      g_return_if_fail (GST_STATE (src) < GST_STATE_PLAYING);

      /* if we get a NULL, consider it to be a fd of 0 */
      if (GTK_VALUE_STRING (*arg) == NULL) {
        gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);
        src->fd = 0;
      /* otherwise set the new filename */
      } else {
        if (sscanf (GTK_VALUE_STRING (*arg), "%d", &fd))
          src->fd = fd;
      }
      break;
    case ARG_BYTESPERREAD:
      src->bytes_per_read = GTK_VALUE_INT (*arg);
      break;
    default:
      break;
  }
}

static void 
gst_fdsrc_get_arg (GtkObject *object, GtkArg *arg, guint id) 
{
  GstFdSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSRC (object));
  
  src = GST_FDSRC (object);

  switch (id) {
    case ARG_BYTESPERREAD:
      GTK_VALUE_INT (*arg) = src->bytes_per_read;
      break;
    case ARG_OFFSET:
      GTK_VALUE_INT (*arg) = src->curoffset;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static GstBuffer *
gst_fdsrc_get(GstPad *pad)
{
  GstFdSrc *src;
  GstBuffer *buf;
  glong readbytes;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_FDSRC(gst_pad_get_parent(pad));

  /* create the buffer */
  // FIXME: should eventually use a bufferpool for this
  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);

  /* allocate the space for the buffer data */
  GST_BUFFER_DATA(buf) = g_malloc(src->bytes_per_read);
  g_return_val_if_fail(GST_BUFFER_DATA(buf) != NULL, NULL);

  /* read it in from the file */
  readbytes = read(src->fd,GST_BUFFER_DATA(buf),src->bytes_per_read);
  if (readbytes == 0) {
    gst_src_signal_eos(GST_SRC(src));
    return NULL;
  }

  /* if we didn't get as many bytes as we asked for, we're at EOF */
  if (readbytes < src->bytes_per_read) {
    // set the buffer's EOF bit here
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_EOS);
  }
  GST_BUFFER_OFFSET(buf) = src->curoffset;
  GST_BUFFER_SIZE(buf) = readbytes;
  src->curoffset += readbytes;

  /* we're done, return the buffer */
  return buf;
}
