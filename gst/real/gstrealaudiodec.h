/* GStreamer
 *
 * Copyright (C) 2006 Lutz Mueller <lutz@topfrose.de>
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

#ifndef __GST_REAL_AUDIO_DEC_H__
#define __GST_REAL_AUDIO_DEC_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_REAL_AUDIO_DEC (gst_real_audio_dec_get_type())
#define GST_REAL_AUDIO_DEC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REAL_AUDIO_DEC,GstRealAudioDec))
#define GST_REAL_AUDIO_DEC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REAL_AUDIO_DEC,GstRealAudioDecClass))

typedef struct _GstRealAudioDec GstRealAudioDec;
typedef struct _GstRealAudioDecClass GstRealAudioDecClass;

GType gst_real_audio_dec_get_type (void);

G_END_DECLS

#endif /* __GST_REAL_AUDIO_DEC_H__ */
