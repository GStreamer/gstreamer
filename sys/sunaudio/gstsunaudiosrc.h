/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstsunaudiosrc.h:
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

#ifndef __GST_SUNAUDIO_SRC_H__
#define __GST_SUNAUDIO_SRC_H__
                                                                                
                                                                                
#include <gst/gst.h>
#include "gstsunelement.h"
                                                                                
G_BEGIN_DECLS
                                                                                
#define GST_TYPE_SUNAUDIOSRC \
  (gst_sunaudiosrc_get_type())
#define GST_SUNAUDIOSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SUNAUDIOSRC,GstSunAudioSrc))
#define GST_SUNAUDIOSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SUNAUDIOSRC,GstSunAudioSrcClass))
#define GST_IS_SUNAUDIOSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SUNAUDIOSRC))
#define GST_IS_SUNAUDIOSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SUNAUDIOSRC))


struct _GstSunAudioSrc {
  GstSunAudioElement   element;

  GstPad               *srcpad;

  char *device;
  int fd;

  audio_device_t dev;
  audio_info_t info;

  int rate;
  int width;
  int channels;
  int buffer_size;

  gulong curoffset;
};

struct _GstSunAudioSrcClass {
  GstSunAudioElementClass parent_class;
};

typedef struct _GstSunAudioSrc GstSunAudioSrc;
typedef struct _GstSunAudioSrcClass GstSunAudioSrcClass;

GType gst_sunaudiosrc_get_type (void);

G_END_DECLS

#endif /* __GST_SUNAUDIO_SRC_H__ */
