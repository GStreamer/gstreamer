/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpipefilter.c:
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
#include <string.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "../gst-i18n-lib.h"
#include "gstpipefilter.h"

GST_DEBUG_CATEGORY_STATIC (gst_pipefilter_debug);
#define GST_CAT_DEFAULT gst_pipefilter_debug

GstElementDetails gst_pipefilter_details = GST_ELEMENT_DETAILS (
  "Pipefilter",
  "Filter",
  "Interoperate with an external program using stdin and stdout",
  "Erik Walthinsen <omega@cse.ogi.edu>, "
  "Wim Taymans <wim.taymans@chello.be>"
);


/* Pipefilter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_COMMAND
};


#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_pipefilter_debug, "pipefilter", 0, "pipefilter element");

GST_BOILERPLATE_FULL (GstPipefilter, gst_pipefilter, GstElement, GST_TYPE_ELEMENT, _do_init);

static void 			gst_pipefilter_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void 			gst_pipefilter_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstData*			gst_pipefilter_get		(GstPad *pad);
static void 			gst_pipefilter_chain		(GstPad *pad, GstData *_data);
static gboolean 		gst_pipefilter_handle_event 	(GstPad *pad, GstEvent *event);

static GstElementStateReturn 	gst_pipefilter_change_state	(GstElement *element);

static void
gst_pipefilter_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_set_details (gstelement_class, &gst_pipefilter_details);
}
static void 
gst_pipefilter_class_init (GstPipefilterClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;


  gobject_class->set_property = gst_pipefilter_set_property;  
  gobject_class->get_property = gst_pipefilter_get_property;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_COMMAND,
    g_param_spec_string("command","command","command",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  gstelement_class->change_state = gst_pipefilter_change_state;
}

static void
gst_pipefilter_init (GstPipefilter *pipefilter)
{
  GST_FLAG_SET (pipefilter, GST_ELEMENT_DECOUPLED);

  pipefilter->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (pipefilter), pipefilter->sinkpad);
  gst_pad_set_chain_function (pipefilter->sinkpad, gst_pipefilter_chain);

  pipefilter->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (pipefilter), pipefilter->srcpad);
  gst_pad_set_get_function (pipefilter->srcpad, gst_pipefilter_get);

  pipefilter->command = NULL;
  pipefilter->curoffset = 0;
  pipefilter->bytes_per_read = 4096;
  pipefilter->seq = 0;
}

static gboolean
gst_pipefilter_handle_event (GstPad *pad, GstEvent *event)
{
  GstPipefilter *pipefilter;

  pipefilter = GST_PIPEFILTER (gst_pad_get_parent (pad));

  GST_DEBUG ("pipefilter: %s received event", GST_ELEMENT_NAME (pipefilter));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (close (pipefilter->fdin[1]) < 0)
        perror("close");
      if (close (pipefilter->fdout[0]) < 0)
        perror("close");
      break;
    default:
      break;
  }

  gst_pad_event_default (pad, event);

  return TRUE;
}

static GstData* 
gst_pipefilter_get (GstPad *pad)
{
  GstPipefilter *pipefilter;
  GstBuffer *newbuf;
  glong readbytes;

  pipefilter = GST_PIPEFILTER (gst_pad_get_parent (pad));

  /* create the buffer */
  /* FIXME: should eventually use a bufferpool for this */
  newbuf = gst_buffer_new();
  g_return_val_if_fail(newbuf, NULL);

  /* allocate the space for the buffer data */
  GST_BUFFER_DATA(newbuf) = g_malloc(pipefilter->bytes_per_read);
  g_return_val_if_fail(GST_BUFFER_DATA(newbuf) != NULL, NULL);

  /* read it in from the file */
  GST_DEBUG ("attemting to read %ld bytes", pipefilter->bytes_per_read);
  readbytes = read(pipefilter->fdout[0], GST_BUFFER_DATA(newbuf), pipefilter->bytes_per_read);
  GST_DEBUG ("read %ld bytes", readbytes);
  if (readbytes < 0) {
    gst_element_error (pipefilter, RESOURCE, READ, NULL, GST_ERROR_SYSTEM);
    return NULL;
  }
  /* if we didn't get as many bytes as we asked for, we're at EOF */
  if (readbytes == 0) {
    return GST_DATA (gst_event_new (GST_EVENT_EOS));

  }

  GST_BUFFER_OFFSET(newbuf) = pipefilter->curoffset;
  GST_BUFFER_SIZE(newbuf) = readbytes;
  pipefilter->curoffset += readbytes;

  return GST_DATA (newbuf);
}

static void
gst_pipefilter_chain (GstPad *pad,GstData *_data)
{
  GstBuffer *buf;
  GstPipefilter *pipefilter;
  glong writebytes;
  guchar *data;
  gulong size;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  if (GST_IS_EVENT (_data)) {
    gst_pipefilter_handle_event (pad, GST_EVENT (_data));
    return;
  }

  pipefilter = GST_PIPEFILTER (gst_pad_get_parent (pad));

  buf = GST_BUFFER (_data);
  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  GST_DEBUG ("attemting to write %ld bytes", size);
  writebytes = write(pipefilter->fdin[1],data,size);
  GST_DEBUG ("written %ld bytes", writebytes);
  if (writebytes < 0) {
    gst_element_error (pipefilter, RESOURCE, WRITE, NULL, GST_ERROR_SYSTEM);
    return;
  }
  gst_buffer_unref(buf);
}

static void
gst_pipefilter_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstPipefilter *pipefilter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_PIPEFILTER(object));
  pipefilter = GST_PIPEFILTER(object);

  switch (prop_id) {
    case ARG_COMMAND:
      pipefilter->orig_command = g_strdup(g_value_get_string (value));
      pipefilter->command = g_strsplit(g_value_get_string (value), " ", 0);
      break;
    default:
      break;
  }
}

static void
gst_pipefilter_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstPipefilter *pipefilter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_PIPEFILTER(object));
  pipefilter = GST_PIPEFILTER(object);

  switch (prop_id) {
    case ARG_COMMAND:
      g_value_set_string (value, pipefilter->orig_command);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* open the file, necessary to go to RUNNING state */
static gboolean
gst_pipefilter_open_file (GstPipefilter *src)
{
  g_return_val_if_fail(!GST_FLAG_IS_SET(src,GST_PIPEFILTER_OPEN), FALSE);

  pipe(src->fdin);
  pipe(src->fdout);

  if((src->childpid = fork()) == -1)
  {
    gst_element_error (src, RESOURCE, TOO_LAZY, NULL, GST_ERROR_SYSTEM);
    return FALSE;
  }

  if(src->childpid == 0)
  {
    close(src->fdin[1]);
    close(src->fdout[0]);
    /* child */
    dup2(src->fdin[0], STDIN_FILENO);  /* set the childs input stream */
    dup2(src->fdout[1], STDOUT_FILENO);  /* set the childs output stream */
    execvp(src->command[0], &src->command[0]);
    /* will only be reached if execvp has an error */
    gst_element_error (src, RESOURCE, TOO_LAZY, NULL, GST_ERROR_SYSTEM);
    return FALSE;
    
  }
  else {
    close(src->fdin[0]);
    close(src->fdout[1]);
  }
  
  GST_FLAG_SET(src,GST_PIPEFILTER_OPEN);
  return TRUE;
}

/* close the file */
static void
gst_pipefilter_close_file (GstPipefilter *src)
{
  g_return_if_fail(GST_FLAG_IS_SET(src,GST_PIPEFILTER_OPEN));

  /* close the file */
  close(src->fdout[0]);
  close(src->fdout[1]);
  close(src->fdin[0]);
  close(src->fdin[1]);

  /* zero out a lot of our state */
  src->curoffset = 0;
  src->seq = 0;

  GST_FLAG_UNSET(src,GST_PIPEFILTER_OPEN);
}

static GstElementStateReturn
gst_pipefilter_change_state (GstElement *element)
{
  g_return_val_if_fail(GST_IS_PIPEFILTER(element), FALSE);

  /* if going down into NULL state, close the file if it's open */ 
  if (GST_STATE_PENDING(element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET(element,GST_PIPEFILTER_OPEN))
      gst_pipefilter_close_file(GST_PIPEFILTER(element));
  /* otherwise (READY or higher) we need to open the file */
  } else {
    if (!GST_FLAG_IS_SET(element,GST_PIPEFILTER_OPEN)) {
      if (!gst_pipefilter_open_file(GST_PIPEFILTER(element)))
        return GST_STATE_FAILURE;
    }
  }
      
  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);
  return GST_STATE_SUCCESS;
}
