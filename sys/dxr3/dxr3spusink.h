/* GStreamer
 * Copyright (C) 2003 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * dxr3spusink.h: Subpicture sink for em8300 based cards.
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

#ifndef __DXR3SPUSINK_H__
#define __DXR3SPUSINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS


#define GST_TYPE_DXR3SPUSINK \
  (dxr3spusink_get_type())
#define DXR3SPUSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DXR3SPUSINK,Dxr3SpuSink))
#define DXR3SPUSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DXR3SPUSINK,Dxr3SpuSinkClass))
#define GST_IS_DXR3SPUSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DXR3SPUSINK))
#define GST_IS_DXR3SPUSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DXR3SPUSINK))


typedef struct _Dxr3SpuSink Dxr3SpuSink;
typedef struct _Dxr3SpuSinkClass Dxr3SpuSinkClass;


typedef enum {
  DXR3SPUSINK_OPEN = GST_ELEMENT_FLAG_LAST,
  DXR3SPUSINK_FLAG_LAST  = GST_ELEMENT_FLAG_LAST + 2,
} Dxr3SpuSinkFlags;


struct _Dxr3SpuSink {
  GstElement element;

  int card_number;     		/* The number of the card to open. */

  gchar *spu_filename; 		/* File name for the spu device. */
  int spu_fd;          		/* File descriptor for the spu device. */

  gchar *control_filename;	/* File name for the control device. */
  int control_fd;		/* File descriptor for the control
                                   device. */

  GstClock *clock;		/* The clock for this element. */
};


struct _Dxr3SpuSinkClass {
  GstElementClass parent_class;

  /* Signals */
  void (*set_clut) (Dxr3SpuSink *sink, const guint32 *clut);
  void (*highlight_on) (Dxr3SpuSink *sink, unsigned palette,
                        unsigned sx, unsigned sy,
                        unsigned ex, unsigned ey,
                        unsigned pts);
  void (*highlight_off) (Dxr3SpuSink *sink);
  void (*flushed) (Dxr3SpuSink *sink);
};


extern GType	dxr3spusink_get_type		(void);
extern gboolean	dxr3spusink_factory_init	(GstPlugin *plugin);

G_END_DECLS

#endif /* __DXR3SPUSINK_H__ */
