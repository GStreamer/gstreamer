/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfdsrc.c: 
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

static void		gst_fdsrc_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void		gst_fdsrc_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static GstBuffer *	gst_fdsrc_get		(GstPad *pad);


static GstElementClass *parent_class = NULL;
//static guint gst_fdsrc_signals[LAST_SIGNAL] = { 0 };

GType
gst_fdsrc_get_type (void) 
{
  static GType fdsrc_type = 0;

  if (!fdsrc_type) {
    static const GTypeInfo fdsrc_info = {
      sizeof(GstFdSrcClass),      NULL,
      NULL,
      (GClassInitFunc)gst_fdsrc_class_init,
      NULL,
      NULL,
      sizeof(GstFdSrc),
      0,
      (GInstanceInitFunc)gst_fdsrc_init,
    };
    fdsrc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstFdSrc", &fdsrc_info, 0);
  }
  return fdsrc_type;
}

static void
gst_fdsrc_class_init (GstFdSrcClass *klass) 
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gst_element_install_std_props (
	  GST_ELEMENT_CLASS (klass),
	  "location",     ARG_LOCATION,     G_PARAM_WRITABLE,
	  "bytesperread", ARG_BYTESPERREAD, G_PARAM_READWRITE,
	  "offset",       ARG_OFFSET,       G_PARAM_READABLE,
	  NULL);

  gobject_class->set_property = gst_fdsrc_set_property;
  gobject_class->get_property = gst_fdsrc_get_property;
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
gst_fdsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstFdSrc *src;
  int fd;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSRC (object));
  
  src = GST_FDSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      /* the element must not be playing in order to do this */
      g_return_if_fail (GST_STATE (src) < GST_STATE_PLAYING);

      /* if we get a NULL, consider it to be a fd of 0 */
      if (g_value_get_string (value) == NULL) {
        gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);
        src->fd = 0;
      /* otherwise set the new filename */
      } else {
        if (sscanf (g_value_get_string (value), "%d", &fd))
          src->fd = fd;
      }
      break;
    case ARG_BYTESPERREAD:
      src->bytes_per_read = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void 
gst_fdsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  GstFdSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSRC (object));
  
  src = GST_FDSRC (object);

  switch (prop_id) {
    case ARG_BYTESPERREAD:
      g_value_set_int (value, src->bytes_per_read);
      break;
    case ARG_OFFSET:
      g_value_set_int (value, src->curoffset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
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
  src = GST_FDSRC(gst_pad_get_parent (pad));

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
