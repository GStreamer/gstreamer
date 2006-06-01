/* GStreamer
 * Copyright (C) 2004 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * ac3iec.h: Pad AC3 frames into IEC958 frames for the SP/DIF interface.
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

#ifndef __AC3IEC_H__
#define __AC3IEC_H__

#include <gst/gst.h>

#include "ac3_padder.h"

G_BEGIN_DECLS


#define GST_TYPE_AC3IEC \
  (ac3iec_get_type())
#define AC3IEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AC3IEC,AC3IEC))
#define AC3IEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AC3IEC,AC3IECClass))
#define GST_IS_AC3IEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AC3IEC))
#define GST_IS_AC3IEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AC3IEC))


typedef struct _AC3IEC AC3IEC;
typedef struct _AC3IECClass AC3IECClass;


typedef enum {
  AC3IEC_OPEN           = (GST_ELEMENT_FLAG_LAST << 0),
  AC3IEC_FLAG_LAST      = (GST_ELEMENT_FLAG_LAST << 2)
} Ac3iecFlags;


struct _AC3IEC {
  GstElement element;

  GstPad *sink;
  GstPad *src;

  GstCaps *caps;                /* source pad caps, once known */

  GstClockTime cur_ts;          /* Time stamp for the current
                                   frame. */
  GstClockTime next_ts;         /* Time stamp for the next frame. */

  ac3_padder *padder;           /* AC3 to SPDIF padder object. */

  gboolean dvdmode;		/* TRUE if DVD mode (input is
				   demultiplexed from a DVD) is
				   active. */

  gboolean raw_audio;		/* TRUE if output pad should use raw
				   audio capabilities. */
};


struct _AC3IECClass {
  GstElementClass parent_class;

  /* Signals */
};


extern GType    ac3iec_get_type         (void);

G_END_DECLS

#endif /* __AC3IEC_H__ */
