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

#include <gst/gst.h>


/* Src signals and args */
enum {
  EOS,
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_src_class_init(GstSrcClass *klass);
static void gst_src_init(GstSrc *src);


static GstElementClass *parent_class = NULL;
static guint gst_src_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_src_get_type(void) {
  static GtkType src_type = 0;

  if (!src_type) {
    static const GtkTypeInfo src_info = {
      "GstSrc",
      sizeof(GstSrc),
      sizeof(GstSrcClass),
      (GtkClassInitFunc)gst_src_class_init,
      (GtkObjectInitFunc)gst_src_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    src_type = gtk_type_unique(GST_TYPE_ELEMENT,&src_info);
  }
  return src_type;
}

static void
gst_src_class_init(GstSrcClass *klass) {
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_ELEMENT);

  gst_src_signals[EOS] =
    gtk_signal_new("eos",GTK_RUN_LAST,gtkobject_class->type,
                   GTK_SIGNAL_OFFSET(GstSrcClass,eos),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GST_TYPE_SRC);
  gtk_object_class_add_signals(gtkobject_class,gst_src_signals,LAST_SIGNAL);
}

static void gst_src_init(GstSrc *src) {
  src->flags = 0;
}

/**
 * gst_src_signal_eos:
 * @src: source to trigger the eos signal of
 *
 * singals the eos signal to indicate that the end of the stream
 * is reached.
 */
void gst_src_signal_eos(GstSrc *src) {
  g_return_if_fail(src != NULL);
  g_return_if_fail(GST_IS_SRC(src));

  gtk_signal_emit(GTK_OBJECT(src),gst_src_signals[EOS],src);
}

/**
 * gst_src_push:
 * @src: source to trigger the push of
 *
 * Push a buffer from the source.
 */
void gst_src_push(GstSrc *src) {
  GstSrcClass *oclass;

  g_return_if_fail(src != NULL);
  g_return_if_fail(GST_IS_SRC(src));

  oclass = (GstSrcClass *)(GTK_OBJECT(src)->klass);

  g_return_if_fail(oclass->push != NULL);

  (oclass->push)(src);
}

/**
 * gst_src_push_region:
 * @src: source to trigger the push of
 * @offset: offset in source
 * @size: number of bytes to push
 *
 * Push a buffer of a given size from the source.
 */
void gst_src_push_region(GstSrc *src,gulong offset,gulong size) {
  GstSrcClass *oclass;

  g_return_if_fail(src != NULL);
  g_return_if_fail(GST_IS_SRC(src));

  oclass = (GstSrcClass *)(GTK_OBJECT(src)->klass);

  g_return_if_fail(oclass->push_region != NULL);

  (oclass->push_region)(src,offset,size);
}

