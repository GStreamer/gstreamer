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


#ifndef __GST_DVDEC_H__
#define __GST_DVDEC_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#include <libdv/dv.h>
#include <gst/bytestream/bytestream.h>


/* This is the definition of the element's object structure. */
typedef struct _GstDVDec GstDVDec;

/* The structure itself is derived from GstElement, as can be seen by the
 * fact that there's a complete instance of the GstElement structure at
 * the beginning of the object.  This allows the element to be cast to
 * an Element or even an Object.
 */
struct _GstDVDec {
  GstElement 	 element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad 	*sinkpad,
  		*videosrcpad,
		  *audiosrcpad;

  dv_decoder_t 	*decoder;
  gboolean	 clamp_luma;
  gboolean	 clamp_chroma;
  gint		 quality;

  GstByteStream *bs;
  dv_color_space_t space;
  gint 		 bpp;
  gboolean PAL;
  gdouble	 framerate;
  gint		 height;
  gint     frequency;
  gint     channels;
  
  gint 		 length;
  guint64	 next_ts;
  guint64	 audio_offset;
  guint64	 end_position;
  gboolean	 need_discont;
  gboolean	 new_media;
  gboolean	 loop;
  
  gboolean found_header;

  gint16 	*audio_buffers[4];
};

/* The other half of the object is its class.  The class also derives from
 * the same parent, though it must be the class structure this time.
 * Function pointers for polymophic methods and signals are placed in this
 * structure. */
typedef struct _GstDVDecClass GstDVDecClass;

struct _GstDVDecClass {
  GstElementClass parent_class;
};

/* Five standard preprocessing macros are used in the Gtk+ object system.
 * The first uses the object's _get_type function to return the GType
 * of the object.
 */
#define GST_TYPE_DVDEC \
  (gst_dvdec_get_type())
/* The second is a checking cast to the correct type.  If the object passed
 * is not the right type, a warning will be generated on stderr.
 */
#define GST_DVDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVDEC,GstDVDec))
/* The third is a checking cast of the class instead of the object. */
#define GST_DVDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVDEC,GstDVDec))
/* The last two simply check to see if the passed pointer is an object or
 * class of the correct type. */
#define GST_IS_DVDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVDEC))
#define GST_IS_DVDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVDEC))

/* This is the only prototype needed, because it is used in the above
 * GST_TYPE_DVDEC macro.
 */
GType gst_dvdec_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_DVDEC_H__ */
