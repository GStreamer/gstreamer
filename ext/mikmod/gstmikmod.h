/* GStreamer
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


#ifndef __GST_MIKMOD_H__
#define __GST_MIKMOD_H__

#include <mikmod.h>
#include <gst/gst.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_MIKMOD \
  (gst_mikmod_get_type())

#define GST_MIKMOD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MIKMOD,GstMikMod))
#define GST_MIKMOD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstMikMod))
#define GST_IS_MIKMOD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MIKMOD))
#define GST_IS_MIKMOD_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MIKMOD))

  struct _GstMikMod
  {
    GstElement element;
    GstPad *sinkpad, *srcpad;
    GstBuffer *Buffer;

    gchar *songname;
    gchar *modtype;
    gint musicvolume;
    gint pansep;
    gint reverb;
    gint sndfxvolume;
    gint volume;
    gint mixfreq;
    gint mode;
    gboolean interp;
    gboolean reverse;
    gboolean surround;
    gboolean _16bit;
    gboolean hqmixer;
    gboolean soft_music;
    gboolean soft_sndfx;
    gboolean stereo;

    gboolean initialized;
  };

  struct _GstMikModClass
  {
    GstElementClass parent_class;
  };

  typedef struct _GstMikMod GstMikMod;
  typedef struct _GstMikModClass GstMikModClass;

  extern MODULE *module;
  extern MREADER *reader;
  extern GstPad *srcpad;
  extern GstClockTime timestamp;
  extern int need_sync;

  GType gst_mikmod_get_type (void);

/* symbols for mikmod_reader.h */
  struct _GST_READER
  {
    MREADER core;
    GstMikMod *mik;
    guint64 offset;
    gshort eof;
  };


  typedef struct _GST_READER GST_READER;


  MREADER *GST_READER_new (GstMikMod * mik);

/* symbols for drv_gst.c */
  extern MDRIVER drv_gst;

#ifdef __cplusplus
}
#endif				/* __cplusplus */

#endif				/* __GST_MIKMOD_H__ */
