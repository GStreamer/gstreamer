/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstafparse.h: 
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


#ifndef __GST_AFPARSE_H__
#define __GST_AFPARSE_H__


#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>
#include <audiofile.h>		/* what else are we to do */
#include <af_vfs.h>


#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


/*GstElementDetails gst_afparse_details;*/


#define GST_TYPE_AFPARSE \
  (gst_afparse_get_type())
#define GST_AFPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AFPARSE,GstAFParse))
#define GST_AFPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AFPARSE,GstAFParseClass))
#define GST_IS_AFPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AFPARSE))
#define GST_IS_AFPARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AFPARSE))

  typedef struct _GstAFParse GstAFParse;
  typedef struct _GstAFParseClass GstAFParseClass;

  typedef enum
  {
    GST_AFPARSE_OPEN = GST_ELEMENT_FLAG_LAST,

    GST_AFPARSE_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
  } GstAFParseFlags;

  struct _GstAFParse
  {
    GstElement element;
    GstPad *srcpad;
    GstPad *sinkpad;

    AFvirtualfile *vfile;
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
    gint frames_per_read;

    gulong seq;
    gint64 timestamp;
    /* FIXME : endianness is a little cryptic at this point */
    int endianness_data;	/* 4321 or 1234 */
    int endianness_wanted;	/* same thing, but what the output format wants */
    int endianness_output;	/* what the output endianness will be */
  };

  struct _GstAFParseClass
  {
    GstElementClass parent_class;

    /* signals */
    void (*handoff) (GstElement * element, GstPad * pad);
  };

  gboolean gst_afparse_plugin_init (GstPlugin * plugin);

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_AFPARSE_H__ */
