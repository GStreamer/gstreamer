/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Dominic Ludlam <dom@recoil.org>
 *
 * gstmultifilesrc.c:
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
#include <errno.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "../gst-i18n-lib.h"

#include "gstmultifilesrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_multifilesrc_debug);
#define GST_CAT_DEFAULT gst_multifilesrc_debug

GstElementDetails gst_multifilesrc_details =
GST_ELEMENT_DETAILS ("Multi File Source",
    "Source/File",
    "Read from multiple files in order",
    "Dominic Ludlam <dom@openfx.org>");

/* FileSrc signals and args */
enum
{
  NEW_FILE,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATIONS
};

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_multifilesrc_debug, "multifilesrc", 0, "multifilesrc element");

GST_BOILERPLATE_FULL (GstMultiFileSrc, gst_multifilesrc, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void gst_multifilesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multifilesrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstData *gst_multifilesrc_get (GstPad * pad);

/*static GstBuffer *	gst_multifilesrc_get_region	(GstPad *pad,GstRegionType type,guint64 offset,guint64 len);*/

static GstElementStateReturn gst_multifilesrc_change_state (GstElement *
    element);

static gboolean gst_multifilesrc_open_file (GstMultiFileSrc * src,
    GstPad * srcpad);
static void gst_multifilesrc_close_file (GstMultiFileSrc * src);

static guint gst_multifilesrc_signals[LAST_SIGNAL] = { 0 };

static void
gst_multifilesrc_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_multifilesrc_details);
}
static void
gst_multifilesrc_class_init (GstMultiFileSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;


  gst_multifilesrc_signals[NEW_FILE] =
      g_signal_new ("new-file", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiFileSrcClass, new_file), NULL, NULL,
      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 1, G_TYPE_STRING);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOCATIONS, g_param_spec_pointer ("locations", "locations", "locations", G_PARAM_READWRITE));     /* CHECKME */

  gobject_class->set_property = gst_multifilesrc_set_property;
  gobject_class->get_property = gst_multifilesrc_get_property;

  gstelement_class->change_state = gst_multifilesrc_change_state;
}

static void
gst_multifilesrc_init (GstMultiFileSrc * multifilesrc)
{
/*  GST_FLAG_SET (filesrc, GST_SRC_); */

  multifilesrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (multifilesrc->srcpad, gst_multifilesrc_get);
/*  gst_pad_set_getregion_function (multifilesrc->srcpad,gst_multifilesrc_get_region); */
  gst_element_add_pad (GST_ELEMENT (multifilesrc), multifilesrc->srcpad);

  multifilesrc->listptr = NULL;
  multifilesrc->currentfilename = NULL;
  multifilesrc->fd = 0;
  multifilesrc->size = 0;
  multifilesrc->map = NULL;
  multifilesrc->new_seek = FALSE;
}

static void
gst_multifilesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiFileSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MULTIFILESRC (object));

  src = GST_MULTIFILESRC (object);

  switch (prop_id) {
    case ARG_LOCATIONS:
      /* the element must be stopped in order to do this */
      g_return_if_fail (GST_STATE (src) < GST_STATE_PLAYING);

      /* clear the filename if we get a NULL */
      if (g_value_get_pointer (value) == NULL) {
        gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);
        src->listptr = NULL;
        /* otherwise set the new filenames */
      } else {
        src->listptr = g_value_get_pointer (value);
      }
      break;
    default:
      break;
  }
}

static void
gst_multifilesrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMultiFileSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MULTIFILESRC (object));

  src = GST_MULTIFILESRC (object);

  switch (prop_id) {
    case ARG_LOCATIONS:
      g_value_set_pointer (value, src->listptr);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_filesrc_get:
 * @pad: #GstPad to push a buffer from
 *
 * Push a new buffer from the filesrc at the current offset.
 */
static GstData *
gst_multifilesrc_get (GstPad * pad)
{
  GstMultiFileSrc *src;
  GstBuffer *buf;
  GSList *list;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_MULTIFILESRC (gst_pad_get_parent (pad));

  if (GST_FLAG_IS_SET (src, GST_MULTIFILESRC_OPEN))
    gst_multifilesrc_close_file (src);

  if (!src->listptr) {
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }

  list = src->listptr;
  src->currentfilename = (gchar *) list->data;
  src->listptr = src->listptr->next;

  if (!gst_multifilesrc_open_file (src, pad))
    return NULL;

  /* emitted after the open, as the user may free the list and string from here */
  g_signal_emit (G_OBJECT (src), gst_multifilesrc_signals[NEW_FILE], 0, list);

  /* create the buffer */
  /* FIXME: should eventually use a bufferpool for this */
  buf = gst_buffer_new ();

  g_return_val_if_fail (buf != NULL, NULL);

  /* simply set the buffer to point to the correct region of the file */
  GST_BUFFER_DATA (buf) = src->map;
  GST_BUFFER_SIZE (buf) = src->size;
  GST_BUFFER_OFFSET (buf) = 0;
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_DONTFREE);

  if (src->new_seek) {
    /* fixme, do something here */
    src->new_seek = FALSE;
  }

  /* we're done, return the buffer */
  return GST_DATA (buf);
}

/* open the file and mmap it, necessary to go to READY state */
static gboolean
gst_multifilesrc_open_file (GstMultiFileSrc * src, GstPad * srcpad)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (src, GST_MULTIFILESRC_OPEN), FALSE);

  if (src->currentfilename == NULL || src->currentfilename[0] == '\0') {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        (_("No file name specified for reading.")), (NULL));
    return FALSE;
  }

  /* open the file */
  src->fd = open ((const char *) src->currentfilename, O_RDONLY);
  if (src->fd < 0) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open file \"%s\" for reading."), src->currentfilename),
        GST_ERROR_SYSTEM);
    return FALSE;

  } else {
    /* find the file length */
    src->size = lseek (src->fd, 0, SEEK_END);
    lseek (src->fd, 0, SEEK_SET);
    /* map the file into memory */
    src->map = mmap (NULL, src->size, PROT_READ, MAP_SHARED, src->fd, 0);
    madvise (src->map, src->size, 2);
    /* collapse state if that failed */
    if (src->map == NULL) {
      close (src->fd);
      GST_ELEMENT_ERROR (src, RESOURCE, TOO_LAZY, (NULL),
          ("mmap call failed."));
      return FALSE;
    }
    GST_FLAG_SET (src, GST_MULTIFILESRC_OPEN);
    src->new_seek = TRUE;
  }
  return TRUE;
}

/* unmap and close the file */
static void
gst_multifilesrc_close_file (GstMultiFileSrc * src)
{
  g_return_if_fail (GST_FLAG_IS_SET (src, GST_MULTIFILESRC_OPEN));

  /* unmap the file from memory and close the file */
  munmap (src->map, src->size);
  close (src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->size = 0;
  src->map = NULL;
  src->new_seek = FALSE;

  GST_FLAG_UNSET (src, GST_MULTIFILESRC_OPEN);
}

static GstElementStateReturn
gst_multifilesrc_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_MULTIFILESRC (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_MULTIFILESRC_OPEN))
      gst_multifilesrc_close_file (GST_MULTIFILESRC (element));
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
