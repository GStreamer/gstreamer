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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstfdsrc.h"

#define DEFAULT_BLOCKSIZE	4096

GST_DEBUG_CATEGORY_STATIC (gst_fdsrc_debug);
#define GST_CAT_DEFAULT gst_fdsrc_debug

GstElementDetails gst_fdsrc_details = GST_ELEMENT_DETAILS (
  "Disk Source",
  "Source/File",
  "Synchronous read from a file",
  "Erik Walthinsen <omega@cse.ogi.edu>"
);


/* FdSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_FD,
  ARG_BLOCKSIZE
};

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_fdsrc_debug, "fdsrc", 0, "fdsrc element");

GST_BOILERPLATE_FULL (GstFdSrc, gst_fdsrc, GstElement, GST_TYPE_ELEMENT, _do_init);

static void		gst_fdsrc_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void		gst_fdsrc_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static GstData *	gst_fdsrc_get		(GstPad *pad);


static void
gst_fdsrc_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_set_details (gstelement_class, &gst_fdsrc_details);
}
static void
gst_fdsrc_class_init (GstFdSrcClass *klass) 
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FD,
    g_param_spec_int ("fd", "fd", "An open file descriptor to read from",
                      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BLOCKSIZE,
    g_param_spec_ulong ("blocksize", "Block size", "Size in bytes to read per buffer",
                        1, G_MAXULONG, DEFAULT_BLOCKSIZE, G_PARAM_READWRITE));

  gobject_class->set_property = gst_fdsrc_set_property;
  gobject_class->get_property = gst_fdsrc_get_property;
}

static void gst_fdsrc_init(GstFdSrc *fdsrc) {
  fdsrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  
  gst_pad_set_get_function (fdsrc->srcpad, gst_fdsrc_get);
  gst_element_add_pad (GST_ELEMENT (fdsrc), fdsrc->srcpad);

  fdsrc->fd = 0;
  fdsrc->curoffset = 0;
  fdsrc->blocksize = DEFAULT_BLOCKSIZE;
  fdsrc->seq = 0;
}


static void 
gst_fdsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstFdSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSRC (object));
  
  src = GST_FDSRC (object);

  switch (prop_id) {
    case ARG_FD:
      src->fd = g_value_get_int (value);
      break;
    case ARG_BLOCKSIZE:
      src->blocksize = g_value_get_ulong (value);
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
    case ARG_BLOCKSIZE:
      g_value_set_ulong (value, src->blocksize);
      break;
    case ARG_FD:
      g_value_set_int (value, src->fd);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstData *
gst_fdsrc_get(GstPad *pad)
{
  GstFdSrc *src;
  GstBuffer *buf;
  glong readbytes;

  src = GST_FDSRC (gst_pad_get_parent (pad));

  /* create the buffer */
  buf = gst_buffer_new_and_alloc (src->blocksize);

  /* read it in from the file */
  readbytes = read (src->fd, GST_BUFFER_DATA (buf), src->blocksize);

  /* if nothing was read, we're in eos */
  if (readbytes == 0) {
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }

  if (readbytes == -1) {
    g_error ("Error reading from file descriptor. Ending stream.\n");
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }
  
  GST_BUFFER_OFFSET (buf) = src->curoffset;
  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
  src->curoffset += readbytes;

  /* we're done, return the buffer */
  return GST_DATA (buf);
}
