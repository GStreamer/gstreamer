/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstafsrc.h: 
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


#ifndef __GST_AFSRC_H__
#define __GST_AFSRC_H__


#include <gst/gst.h>
#include <audiofile.h>		/* what else are we to do */


#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


/*GstElementDetails gst_afsrc_details;*/


#define GST_TYPE_AFSRC \
  (gst_afsrc_get_type())
#define GST_AFSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AFSRC,GstAFSrc))
#define GST_AFSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AFSRC,GstAFSrcClass))
#define GST_IS_AFSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AFSRC))
#define GST_IS_AFSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AFSRC))

  typedef struct _GstAFSrc GstAFSrc;
  typedef struct _GstAFSrcClass GstAFSrcClass;

  typedef enum
  {
    GST_AFSRC_OPEN = GST_ELEMENT_FLAG_LAST,

    GST_AFSRC_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
  } GstAFSrcFlags;

  struct _GstAFSrc
  {
    GstElement element;
    GstPad *srcpad;

    gchar *filename;
/*  FILE *file; */

/*  AFfilesetup outfilesetup; */
    AFfilehandle file;
    int format;
    int channels;
    int width;
    unsigned int rate;
    gboolean is_signed;
    int type;			/* type of output, compare to audiofile.h 
				 * RAW, AIFF, AIFFC, NEXTSND, WAVE
				 */
    /* blocking */
    gulong curoffset;
    gulong bytes_per_read;

    gulong seq;
    guint64 framestamp;
    /* FIXME : endianness is a little cryptic at this point */
    int endianness_data;	/* 4321 or 1234 */
    int endianness_wanted;	/* same thing, but what the output format wants */
    int endianness_output;	/* what the output endianness will be */
  };

  struct _GstAFSrcClass
  {
    GstElementClass parent_class;

    /* signals */
    void (*handoff) (GstElement * element, GstPad * pad);
  };

  GType gst_afsrc_get_type (void);
  gboolean gst_afsrc_plugin_init (GstPlugin * plugin);




#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_AFSRC_H__ */
