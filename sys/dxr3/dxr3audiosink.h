/* GStreamer
 * Copyright (C) 2003 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * dxr3audiosink.h: Audio sink for em8300 based DVD cards.
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

#ifndef __DXR3AUDIOSINK_H__
#define __DXR3AUDIOSINK_H__

#include <gst/gst.h>

#include "ac3_padder.h"

G_BEGIN_DECLS


#define GST_TYPE_DXR3AUDIOSINK \
  (dxr3audiosink_get_type())
#define DXR3AUDIOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DXR3AUDIOSINK,Dxr3AudioSink))
#define DXR3AUDIOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DXR3AUDIOSINK,Dxr3AudioSinkClass))
#define GST_IS_DXR3AUDIOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DXR3AUDIOSINK))
#define GST_IS_DXR3AUDIOSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DXR3AUDIOSINK))


typedef struct _Dxr3AudioSink Dxr3AudioSink;
typedef struct _Dxr3AudioSinkClass Dxr3AudioSinkClass;


typedef enum {
  DXR3AUDIOSINK_OPEN = GST_ELEMENT_FLAG_LAST,
  DXR3AUDIOSINK_FLAG_LAST  = GST_ELEMENT_FLAG_LAST + 2,
} Dxr3AudioSinkFlags;


/* PCM or AC3? */
typedef enum {
  DXR3AUDIOSINK_MODE_NONE, /* No mode set. */ 
  DXR3AUDIOSINK_MODE_AC3,  /* AC3 out. */ 
  DXR3AUDIOSINK_MODE_PCM,  /* PCM out. */
} Dxr3AudioSinkMode;


/* Information for a delayed SCR set operation. */
typedef struct {
  struct _Dxr3AudioSink *sink;
  guint32 scr;
} Dxr3AudioSinkDelayedSCR;


struct _Dxr3AudioSink {
  GstElement element;

  GstPad *pcm_sinkpad;     /* The AC3 audio sink pad. */
  GstPad *ac3_sinkpad;     /* The PCM audio sink pad. */

  int card_number;         /* The number of the card to open. */

  gchar *audio_filename;   /* File name for the audio device. */
  int audio_fd;            /* File descriptor for the audio device. */

  gchar *control_filename; /* File name for the control device. */
  int control_fd;          /* File descriptor for the control
                              device. */

  guint64 scr;             /* The current System Reference Clock value
                              for the audio stream. */

  gboolean digital_pcm;    /* Should PCM use the digital or the 
                              analog output? */

  Dxr3AudioSinkMode mode;  /* The current sound output mode. */

  gint rate;               /* The sampling rate for PCM sound. */

  ac3_padder *padder;      /* AC3 to SPDIF padder object. */

  GstClock *clock;	   /* The clock for this element. */
};


struct _Dxr3AudioSinkClass {
  GstElementClass parent_class;

  /* signals */
  void (*flushed) (Dxr3AudioSink *sink);
};


extern GType	dxr3audiosink_get_type		(void);
extern gboolean	dxr3audiosink_factory_init	(GstPlugin *plugin);

G_END_DECLS

#endif /* __DXR3AUDIOINK_H__ */
