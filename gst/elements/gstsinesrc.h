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


#ifndef __GST_SINESRC_H__
#define __GST_SINESRC_H__


#include <config.h>
#include <gst/gst.h>
#include <gst/meta/audioraw.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_sinesrc_details;


#define GST_TYPE_SINESRC \
  (gst_sinesrc_get_type())
#define GST_SINESRC(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_SINESRC,GstSineSrc))
#define GST_SINESRC_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_SINESRC,GstSineSrcClass))
#define GST_IS_SINESRC(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_SINESRC))
#define GST_IS_SINESRC_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_SINESRC))

typedef struct _GstSineSrc GstSineSrc;
typedef struct _GstSineSrcClass GstSineSrcClass;

struct _GstSineSrc {
  GstSrc src;

  /* pads */
  GstPad *srcpad;

  /* parameters */
  gdouble volume;
  gint freq;

  /* audio parameters */
  gint format;
  gint channels;
  gint frequency;

  gulong seq;

  MetaAudioRaw meta;
  gboolean sentmeta;
};

struct _GstSineSrcClass {
  GstSrcClass parent_class;
};

GtkType gst_sinesrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_SINESRC_H__ */
