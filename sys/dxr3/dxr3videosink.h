/* GStreamer
 * Copyright (C) 2003 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * dxr3videosink.h: Video sink for em8300 based cards.
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

#ifndef __DXR3VIDEOSINK_H__
#define __DXR3VIDEOSINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS


#define GST_TYPE_DXR3VIDEOSINK \
  (dxr3videosink_get_type())
#define DXR3VIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DXR3VIDEOSINK,Dxr3VideoSink))
#define DXR3VIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DXR3VIDEOSINK,Dxr3VideoSinkClass))
#define GST_IS_DXR3VIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DXR3VIDEOSINK))
#define GST_IS_DXR3VIDEOSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DXR3VIDEOSINK))


typedef struct _Dxr3VideoSink Dxr3VideoSink;
typedef struct _Dxr3VideoSinkClass Dxr3VideoSinkClass;


typedef enum {
  DXR3VIDEOSINK_OPEN = GST_ELEMENT_FLAG_LAST,
  DXR3VIDEOSINK_FLAG_LAST  = GST_ELEMENT_FLAG_LAST + 2,
} Dxr3VideoSinkFlags;


struct _Dxr3VideoSink {
  GstElement element;

  int card_number;		/* The number of the card to open. */

  gchar *video_filename;	/* File name for the video device. */
  int video_fd;			/* File descriptor for the video device. */

  gchar *control_filename;	/* File name for the control device. */
  int control_fd;          	/* File descriptor for the control
                                   device. */

  GstClock *clock;		/* The clock for this element. */

  GstClockTime last_ts;		/* Last timestamp received. */

  GstBuffer *cur_buf;		/* The buffer we are currently
                                   building. */
  GstClockTime cur_ts;		/* Timestamp associated to the
                                   current buffer. */

  guchar scan_state;		/* The current state of the MPEG start
                                   code scanner. */
  guint scan_pos;		/* The current position of the MPEG
                                   start code scanner (with respect to
                                   the start of the current buffer. */

  guchar parse_state;		/* The current state of the MPEG
                                   sequence parser. */
};


struct _Dxr3VideoSinkClass {
  GstElementClass parent_class;

  /* signals */
  void (*flushed) (Dxr3VideoSink *sink);
};


extern GType	dxr3videosink_get_type		(void);
extern gboolean	dxr3videosink_factory_init	(GstPlugin *plugin);

G_END_DECLS

#endif /* __DXR3VIDEOSINK_H__ */
