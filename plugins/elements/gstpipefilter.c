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
#include <errno.h>

//#define DEBUG_ENABLED
#include "gstpipefilter.h"


GstElementDetails gst_pipefilter_details = {
  "Pipefilter",
  "Filter",
  "Pass data without modification",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* Pipefilter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_COMMAND
};


static void gst_pipefilter_class_init(GstPipefilterClass *klass);
static void gst_pipefilter_init(GstPipefilter *pipefilter);
static void gst_pipefilter_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_pipefilter_get_arg(GtkObject *object,GtkArg *arg,guint id);

void gst_pipefilter_chain(GstPad *pad,GstBuffer *buf);

static GstElementStateReturn gst_pipefilter_change_state(GstElement *element);

static GstFilterClass *parent_class = NULL;
//static guint gst_pipefilter_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_pipefilter_get_type(void) {
  static GtkType pipefilter_type = 0;

  if (!pipefilter_type) {
    static const GtkTypeInfo pipefilter_info = {
      "GstPipefilter",
      sizeof(GstPipefilter),
      sizeof(GstPipefilterClass),
      (GtkClassInitFunc)gst_pipefilter_class_init,
      (GtkObjectInitFunc)gst_pipefilter_init,
      (GtkArgSetFunc)gst_pipefilter_set_arg,
      (GtkArgGetFunc)gst_pipefilter_get_arg,
      (GtkClassInitFunc)NULL,
    };
    pipefilter_type = gtk_type_unique(GST_TYPE_FILTER,&pipefilter_info);
  }
  return pipefilter_type;
}

static void gst_pipefilter_class_init(GstPipefilterClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstFilterClass *gstfilter_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstfilter_class = (GstFilterClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_FILTER);

  gstelement_class->change_state = gst_pipefilter_change_state;

  gtk_object_add_arg_type("GstPipefilter::command", GTK_TYPE_STRING,
                          GTK_ARG_READWRITE, ARG_COMMAND);

  gtkobject_class->set_arg = gst_pipefilter_set_arg;  
  gtkobject_class->get_arg = gst_pipefilter_get_arg;
}

static void gst_pipefilter_init(GstPipefilter *pipefilter) {
  pipefilter->sinkpad = gst_pad_new("sink",GST_PAD_SINK);
  gst_element_add_pad(GST_ELEMENT(pipefilter),pipefilter->sinkpad);
  gst_pad_set_chain_function(pipefilter->sinkpad,gst_pipefilter_chain);
  pipefilter->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(pipefilter),pipefilter->srcpad);

  pipefilter->command = NULL;
  pipefilter->curoffset = 0;
  pipefilter->bytes_per_read = 4096;
  pipefilter->seq = 0;
}

GstElement *gst_pipefilter_new(gchar *name) {
  GstElement *pipefilter = GST_ELEMENT(gtk_type_new(GST_TYPE_PIPEFILTER));
  gst_element_set_name(GST_ELEMENT(pipefilter),name);
  return pipefilter;
}


static gboolean gst_pipefilter_read_and_push(GstPipefilter *pipefilter) {
  GstBuffer *newbuf;
  glong readbytes;

  /* create the buffer */
  // FIXME: should eventually use a bufferpool for this
  newbuf = gst_buffer_new();
  g_return_val_if_fail(newbuf, FALSE);

  /* allocate the space for the buffer data */
  GST_BUFFER_DATA(newbuf) = g_malloc(pipefilter->bytes_per_read);
  g_return_val_if_fail(GST_BUFFER_DATA(newbuf) != NULL, FALSE);

  /* read it in from the file */
  DEBUG("attemting to read %d bytes\n", pipefilter->bytes_per_read);
  readbytes = read(pipefilter->fdout[0],GST_BUFFER_DATA(newbuf),pipefilter->bytes_per_read);
  DEBUG("read %d bytes\n", readbytes);
  if (readbytes < 0) {
    if (errno == EAGAIN) {
      DEBUG("no input yet\n");
      gst_buffer_unref(newbuf);
      return FALSE;
    }
    else {
      perror("read");
      gst_element_error(GST_ELEMENT(pipefilter),"reading");
      return FALSE;
    }
  }
  if (readbytes == 0) {
    gst_buffer_unref(newbuf);
    return FALSE;
  }
  /* if we didn't get as many bytes as we asked for, we're at EOF */
  if (readbytes < pipefilter->bytes_per_read)
    GST_BUFFER_FLAG_SET(newbuf,GST_BUFFER_EOS);
  GST_BUFFER_OFFSET(newbuf) = pipefilter->curoffset;
  GST_BUFFER_SIZE(newbuf) = readbytes;
  pipefilter->curoffset += readbytes;

  /* we're done, push the buffer off now */
  gst_pad_push(pipefilter->srcpad,newbuf);
  return TRUE;
}
void gst_pipefilter_chain(GstPad *pad,GstBuffer *buf) {
  GstPipefilter *pipefilter;
  glong writebytes;
  guchar *data;
  gulong size;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  pipefilter = GST_PIPEFILTER(pad->parent);

  while (gst_pipefilter_read_and_push(pipefilter));

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  DEBUG("attemting to write %d bytes\n", size);
  writebytes = write(pipefilter->fdin[1],data,size);
  DEBUG("written %d bytes\n", writebytes);
  if (writebytes < 0) {
    perror("write");
    gst_element_error(GST_ELEMENT(pipefilter),"writing");
    return;
  }
  gst_buffer_unref(buf);

  while (gst_pipefilter_read_and_push(pipefilter));
}

static void gst_pipefilter_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstPipefilter *pipefilter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_PIPEFILTER(object));
  pipefilter = GST_PIPEFILTER(object);

  switch(id) {
    case ARG_COMMAND:
      pipefilter->orig_command = g_strdup(GTK_VALUE_STRING(*arg));
      pipefilter->command = g_strsplit(GTK_VALUE_STRING(*arg), " ", 0);
      break;
    default:
      break;
  }
}

static void gst_pipefilter_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstPipefilter *pipefilter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_PIPEFILTER(object));
  pipefilter = GST_PIPEFILTER(object);

  switch (id) {
    case ARG_COMMAND:
      GTK_VALUE_STRING(*arg) = pipefilter->orig_command;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

/* open the file, necessary to go to RUNNING state */
static gboolean gst_pipefilter_open_file(GstPipefilter *src) {
  g_return_val_if_fail(!GST_FLAG_IS_SET(src,GST_PIPEFILTER_OPEN), FALSE);

  pipe(src->fdin);
  pipe(src->fdout);

  if (fcntl(src->fdout[0], F_SETFL, O_NONBLOCK) < 0) {
    perror("fcntl");
    gst_element_error(GST_ELEMENT(src),"fcntl");
    return FALSE;
  }

  if((src->childpid = fork()) == -1)
  {
    perror("fork");
    gst_element_error(GST_ELEMENT(src),"forking");
    return FALSE;
  }

  if(src->childpid == 0)
  {
    // child
    dup2(src->fdin[0], STDIN_FILENO);  /* set the childs input stream */
    dup2(src->fdout[1], STDOUT_FILENO);  /* set the childs output stream */
    execvp(src->command[0], &src->command[0]);
    // will only reach if error
    perror("exec");
    gst_element_error(GST_ELEMENT(src),"starting child process");
    return FALSE;
    
  }
  
  GST_FLAG_SET(src,GST_PIPEFILTER_OPEN);
  return TRUE;
}

/* close the file */
static void gst_pipefilter_close_file(GstPipefilter *src) {
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

static GstElementStateReturn gst_pipefilter_change_state(GstElement *element) {
  g_return_val_if_fail(GST_IS_PIPEFILTER(element), FALSE);

  /* if going down into NULL state, close the file if it's open */ 
  if (GST_STATE_PENDING(element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET(element,GST_PIPEFILTER_OPEN))
      gst_pipefilter_close_file(GST_PIPEFILTER(element));
  /* otherwise (READY or higher) we need to open the file */
  } else {
    if (!GST_FLAG_IS_SET(element,GST_PIPEFILTER_OPEN)) {
      if (!gst_disksrc_open_file(GST_PIPEFILTER(element)))
        return GST_STATE_FAILURE;
    }
  }
      
  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);
  return TRUE;
}
