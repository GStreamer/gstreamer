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

#include <gstdisksrc.h>


GstElementDetails gst_disksrc_details = {
  "Disk Source",
  "Source/File",
  "Synchronous read from a file",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* DiskSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_BYTESPERREAD,
  ARG_OFFSET,
  ARG_SIZE,
};


static void 			gst_disksrc_class_init		(GstDiskSrcClass *klass);
static void 			gst_disksrc_init		(GstDiskSrc *disksrc);

static void 			gst_disksrc_set_arg		(GtkObject *object, GtkArg *arg, guint id);
static void 			gst_disksrc_get_arg		(GtkObject *object, GtkArg *arg, guint id);

static void 			gst_disksrc_close_file		(GstDiskSrc *src);

static GstBuffer *		gst_disksrc_get			(GstPad *pad);

static GstElementStateReturn 	gst_disksrc_change_state	(GstElement *element);


static GstSrcClass *parent_class = NULL;
//static guint gst_disksrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_disksrc_get_type(void) {
  static GtkType disksrc_type = 0;

  if (!disksrc_type) {
    static const GtkTypeInfo disksrc_info = {
      "GstDiskSrc",
      sizeof(GstDiskSrc),
      sizeof(GstDiskSrcClass),
      (GtkClassInitFunc)gst_disksrc_class_init,
      (GtkObjectInitFunc)gst_disksrc_init,
      (GtkArgSetFunc)gst_disksrc_set_arg,
      (GtkArgGetFunc)gst_disksrc_get_arg,
      (GtkClassInitFunc)NULL,
    };
    disksrc_type = gtk_type_unique(GST_TYPE_SRC,&disksrc_info);
  }
  return disksrc_type;
}

static void
gst_disksrc_class_init (GstDiskSrcClass *klass) 
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;
  GstSrcClass *gstsrc_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;
  gstsrc_class = (GstSrcClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_SRC);

  gtk_object_add_arg_type ("GstDiskSrc::location", GST_TYPE_FILENAME,
                           GTK_ARG_READWRITE, ARG_LOCATION);
  gtk_object_add_arg_type ("GstDiskSrc::bytesperread", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_BYTESPERREAD);
  gtk_object_add_arg_type ("GstDiskSrc::offset", GTK_TYPE_INT,
                           GTK_ARG_READABLE, ARG_OFFSET);
  gtk_object_add_arg_type ("GstDiskSrc::size", GTK_TYPE_INT,
                           GTK_ARG_READABLE, ARG_SIZE);

  gtkobject_class->set_arg = gst_disksrc_set_arg;
  gtkobject_class->get_arg = gst_disksrc_get_arg;

  gstelement_class->change_state = gst_disksrc_change_state;
}

static void 
gst_disksrc_init (GstDiskSrc *disksrc) 
{
  disksrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function(disksrc->srcpad,gst_disksrc_get);
  gst_element_add_pad (GST_ELEMENT (disksrc), disksrc->srcpad);

  disksrc->filename = NULL;
  disksrc->fd = 0;
  disksrc->curoffset = 0;
  disksrc->bytes_per_read = 4096;
  disksrc->seq = 0;
  disksrc->size = 0;
  disksrc->new_seek = FALSE;
}


static void 
gst_disksrc_set_arg (GtkObject *object, GtkArg *arg, guint id) 
{
  GstDiskSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DISKSRC (object));
  
  src = GST_DISKSRC (object);

  switch(id) {
    case ARG_LOCATION:
      /* the element must not be playing in order to do this */
      g_return_if_fail (GST_STATE(src) < GST_STATE_PLAYING);

      if (src->filename) g_free (src->filename);
      /* clear the filename if we get a NULL (is that possible?) */
      if (GTK_VALUE_STRING (*arg) == NULL) {
        gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);
        src->filename = NULL;
      /* otherwise set the new filename */
      } else {
        src->filename = g_strdup (GTK_VALUE_STRING (*arg));
      }
      break;
    case ARG_BYTESPERREAD:
      src->bytes_per_read = GTK_VALUE_INT (*arg);
      break;
      /*
    case ARG_OFFSET:
      src->curoffset = GTK_VALUE_INT(*arg);
      lseek(src->fd,src->curoffset, SEEK_SET);
      src->new_seek = TRUE;
      break;
      */
    default:
      break;
  }
}

static void 
gst_disksrc_get_arg (GtkObject *object, GtkArg *arg, guint id) 
{
  GstDiskSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DISKSRC (object));
  src = GST_DISKSRC (object);

  switch (id) {
    case ARG_LOCATION:
      GTK_VALUE_STRING (*arg) = src->filename;
      break;
    case ARG_BYTESPERREAD:
      GTK_VALUE_INT (*arg) = src->bytes_per_read;
      break;
    case ARG_OFFSET:
      GTK_VALUE_INT (*arg) = src->curoffset;
      break;
    case ARG_SIZE:
      GTK_VALUE_INT (*arg) = src->size;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static GstBuffer *
gst_disksrc_get (GstPad *pad) 
{
  GstDiskSrc *src;
  GstBuffer *buf;
  glong readbytes;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_DISKSRC(gst_pad_get_parent(pad));
  g_return_val_if_fail (GST_FLAG_IS_SET (src, GST_DISKSRC_OPEN), NULL);
  g_return_val_if_fail (GST_STATE (src) >= GST_STATE_READY, NULL);

  /* create the buffer */
  // FIXME: should eventually use a bufferpool for this
  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);

  /* allocate the space for the buffer data */
  GST_BUFFER_DATA (buf) = g_malloc (src->bytes_per_read);
  g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

  /* read it in from the file */
  readbytes = read (src->fd, GST_BUFFER_DATA (buf), src->bytes_per_read);
  if (readbytes == -1) {
    perror ("read()");
    gst_buffer_unref (buf);
    return NULL;
  } else if (readbytes == 0) {
    gst_src_signal_eos (GST_SRC (src));
    gst_buffer_unref (buf);
    return NULL;
  }

  /* if we didn't get as many bytes as we asked for, we're at EOF */
  if (readbytes < src->bytes_per_read) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_EOS);
    DEBUG("setting GST_BUFFER_EOS\n");
  }

  /* if we have a new buffer from a seek, mark it */
  if (src->new_seek) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLUSH);
    src->new_seek = FALSE;
  }

  GST_BUFFER_OFFSET (buf) = src->curoffset;
  GST_BUFFER_SIZE (buf) = readbytes;
  src->curoffset += readbytes;

  DEBUG("pushing %d bytes with offset %d\n", GST_BUFFER_SIZE(buf), GST_BUFFER_OFFSET (buf));
  /* we're done, push the buffer off now */
  DEBUG("returning %d bytes with offset %d done\n", GST_BUFFER_SIZE(buf), GST_BUFFER_OFFSET (buf));
  return buf;
}


/* open the file, necessary to go to RUNNING state */
static gboolean 
gst_disksrc_open_file (GstDiskSrc *src) 
{
  struct stat f_stat;

  g_return_val_if_fail (!GST_FLAG_IS_SET (src, GST_DISKSRC_OPEN), FALSE);
  g_return_val_if_fail (src->filename != NULL, FALSE);

  /* open the file */
  src->fd = open (src->filename, O_RDONLY);
  if (src->fd < 0) {
    perror ("open()");
    gst_element_error (GST_ELEMENT (src), "opening file");
    return FALSE;
  }
  if (fstat (src->fd, &f_stat) < 0) {
    perror("fstat()");
  }
  else {
    src->size = f_stat.st_size;
    DEBUG("gstdisksrc: file size %ld\n", src->size);
  }
  GST_FLAG_SET (src, GST_DISKSRC_OPEN);
  return TRUE;
}

/* close the file */
static void 
gst_disksrc_close_file (GstDiskSrc *src) 
{
  g_return_if_fail (GST_FLAG_IS_SET (src, GST_DISKSRC_OPEN));

  /* close the file */
  close (src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->curoffset = 0;
  src->seq = 0;
  src->size = 0;

  GST_FLAG_UNSET (src, GST_DISKSRC_OPEN);
}

static GstElementStateReturn 
gst_disksrc_change_state (GstElement *element) 
{
  g_return_val_if_fail (GST_IS_DISKSRC (element), GST_STATE_FAILURE);

  DEBUG("gstdisksrc: state pending %d\n", GST_STATE_PENDING (element));

  /* if going down into NULL state, close the file if it's open */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_DISKSRC_OPEN))
      gst_disksrc_close_file (GST_DISKSRC (element));
  /* otherwise (READY or higher) we need to open the file */
  } else {
    if (!GST_FLAG_IS_SET (element, GST_DISKSRC_OPEN)) {
      if (!gst_disksrc_open_file (GST_DISKSRC (element)))
        return GST_STATE_FAILURE;
    }
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
