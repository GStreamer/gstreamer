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

#include <gsttee.h>

#include "config.h"

GstElementDetails gst_tee_details = {
  "Tee pipe fitting",
  "Tee",
  "1-to-N pipe fitting",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};

/* Tee signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_tee_class_init(GstTeeClass *klass);
static void gst_tee_init(GstTee *tee);

//static xmlNodePtr gst_tee_save_thyself(GstElement *element,xmlNodePtr parent);

static GstFilterClass *parent_class = NULL;
//static guint gst_tee_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_tee_get_type(void) {
  static GtkType tee_type = 0;

  if (!tee_type) {
    static const GtkTypeInfo tee_info = {
      "GstTee",
      sizeof(GstTee),
      sizeof(GstTeeClass),
      (GtkClassInitFunc)gst_tee_class_init,
      (GtkObjectInitFunc)gst_tee_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    tee_type = gtk_type_unique(GST_TYPE_FILTER,&tee_info);
  }
  return tee_type;
}

static void
gst_tee_class_init(GstTeeClass *klass) {
  GstFilterClass *gstfilter_class;

  gstfilter_class = (GstFilterClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_FILTER);
}

static void gst_tee_init(GstTee *tee) {
  tee->sinkpad = gst_pad_new("sink",GST_PAD_SINK);
  gst_element_add_pad(GST_ELEMENT(tee),tee->sinkpad);
  gst_pad_set_chain_function(tee->sinkpad,gst_tee_chain);

  tee->numsrcpads = 0;
  tee->srcpads = NULL;
}

/**
 * gst_tee_new:
 * @name: the name of the new tee
 *
 * create a new tee element
 *
 * Returns: the new tee element
 */
GstElement *gst_tee_new(gchar *name) {
  GstElement *tee = GST_ELEMENT(gtk_type_new(GST_TYPE_TEE));
  gst_element_set_name(GST_ELEMENT(tee),name);
  return tee;
}

/**
 * gst_tee_new_pad:
 * @tee: the tee to create the new pad on
 *
 * create a new pad on a given tee
 *
 * Returns: the name of the new pad
 */
gchar *gst_tee_new_pad(GstTee *tee) {
  gchar *name;
  GstPad *srcpad;

  g_return_val_if_fail(tee != NULL, NULL);
  g_return_val_if_fail(GST_IS_TEE(tee), NULL);

  name = g_strdup_printf("src%d",tee->numsrcpads);
  srcpad = gst_pad_new(name,GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(tee),srcpad);
  tee->srcpads = g_slist_prepend(tee->srcpads,srcpad);
  tee->numsrcpads++;
  return name;
}

/**
 * gst_tee_chain:
 * @pad: the pad to follow
 * @buf: the buffer to pass
 *
 * chain a buffer on a pad
 */
void gst_tee_chain(GstPad *pad,GstBuffer *buf) {
  GstTee *tee;
  GSList *srcpads;
  int i;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  tee = GST_TEE(pad->parent);
  gst_trace_add_entry(NULL,0,buf,"tee buffer");
  for (i=0;i<tee->numsrcpads-1;i++)
    gst_buffer_ref(buf);
  srcpads = tee->srcpads;
  while (srcpads) {
    gst_pad_push(GST_PAD(srcpads->data),buf);
    srcpads = g_slist_next(srcpads);
  }
}
