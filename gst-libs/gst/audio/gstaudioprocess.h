/* GStreamer Audio Process
 * Copyright (C) 2010 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstaudioprocess.h: Audio processing extension
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

#ifndef __GST_AUDIO_PROCESS_H__
#define __GST_AUDIO_PROCESS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_AUDIO_PROCESS \
  (gst_audio_process_get_type ())
#define GST_AUDIO_PROCESS(obj) \
  (GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AUDIO_PROCESS, GstAudioProcess))
#define GST_IS_AUDIO_PROCESS(obj) \
  (GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AUDIO_PROCESS))
#define GST_AUDIO_PROCESS_GET_IFACE(inst) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GST_TYPE_AUDIO_PROCESS, GstAudioProcessInterface))

typedef struct _GstAudioProcess GstAudioProcess;
typedef struct _GstAudioProcessInterface GstAudioProcessInterface;

struct _GstAudioProcessInterface {
  GTypeInterface parent;

  /* vfunctions */
  gint          (*activate)       (GstAudioProcess *process, gboolean active);
  gint          (*process)        (GstAudioProcess *process, gpointer src_in, gpointer sink_in,
                                   gpointer src_out, guint length);

  /*< private >*/
  gpointer                 _gst_reserved[GST_PADDING];
};

GType           gst_audio_process_get_type          (void);

/* invoke vfunction on interface */
gint    gst_audio_process_process   (GstAudioProcess *ext, gpointer src_in, gpointer sink_in,
                                     gpointer src_out, guint length);

G_END_DECLS

#endif /* __GST_AUDIO_PROCESS_H__ */
