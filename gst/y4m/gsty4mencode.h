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


#ifndef __GST_LAVENCODE_H__
#define __GST_LAVENCODE_H__


#include <config.h>
#include <gst/gst.h>
/* #include <gst/meta/audioraw.h> */


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_LAVENCODE \
  (gst_lavencode_get_type())
#define GST_LAVENCODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LAVENCODE,GstLavEncode))
#define GST_LAVENCODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstLavEncode))
#define GST_IS_LAVENCODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LAVENCODE))
#define GST_IS_LAVENCODE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LAVENCODE))

typedef struct _GstLavEncode GstLavEncode;
typedef struct _GstLavEncodeClass GstLavEncodeClass;

struct _GstLavEncode {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  gint width, height;

  gboolean init;

};

struct _GstLavEncodeClass {
  GstElementClass parent_class;
};

GType gst_lavencode_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_STEREO_H__ */
