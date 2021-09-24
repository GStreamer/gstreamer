/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Jan Schmidt <jan@noraisin.net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_DVDLPCMDEC_H__
#define __GST_DVDLPCMDEC_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiodecoder.h>

G_BEGIN_DECLS

#define GST_TYPE_DVDLPCMDEC \
  (gst_dvdlpcmdec_get_type())
#define GST_DVDLPCMDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVDLPCMDEC,GstDvdLpcmDec))
#define GST_DVDLPCMDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVDLPCMDEC,GstDvdLpcmDecClass))
#define GST_IS_DVDLPCMDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVDLPCMDEC))
#define GST_IS_DVDLPCMDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVDLPCMDEC))

typedef struct _GstDvdLpcmDec GstDvdLpcmDec;
typedef struct _GstDvdLpcmDecClass GstDvdLpcmDecClass;

typedef enum {
  GST_LPCM_UNKNOWN,
  GST_LPCM_RAW,
  GST_LPCM_DVD,
  GST_LPCM_1394,
  GST_LPCM_BLURAY
} GstDvdLpcmMode;

struct _GstDvdLpcmDec {
  GstAudioDecoder element;

  GstPadChainFunction base_chain;

  GstDvdLpcmMode mode;
  guint32 header;

  GstAudioInfo info;
  const GstAudioChannelPosition *lpcm_layout;
  gint width;
  gint dynamic_range;
  gint emphasis;
  gint mute;

  GstClockTime timestamp;
};

struct _GstDvdLpcmDecClass {
  GstAudioDecoderClass parent_class;
};

GType gst_dvdlpcmdec_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (dvdlpcmdec);

G_END_DECLS

#endif /* __GST_DVDLPCMDEC_H__ */
