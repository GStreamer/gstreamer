/* GStreamer OSS4 mixer on/off enum control
 * Copyright (C) 2007-2008 Tim-Philipp Müller <tim centricular net>
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

#ifndef GST_OSS4_MIXER_ENUM_H
#define GST_OSS4_MIXER_ENUM_H

#include <gst/gst.h>
#include <gst/interfaces/mixer.h>

#include "oss4-mixer.h"

G_BEGIN_DECLS

#define GST_TYPE_OSS4_MIXER_ENUM            (gst_oss4_mixer_enum_get_type())
#define GST_OSS4_MIXER_ENUM(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSS4_MIXER_ENUM,GstOss4MixerEnum))
#define GST_OSS4_MIXER_ENUM_CAST(obj)       ((GstOss4MixerEnum *)(obj))
#define GST_OSS4_MIXER_ENUM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSS4_MIXER_ENUM,GstOss4MixerEnumClass))
#define GST_IS_OSS4_MIXER_ENUM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSS4_MIXER_ENUM))
#define GST_IS_OSS4_MIXER_ENUM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSS4_MIXER_ENUM))

typedef struct _GstOss4MixerEnum GstOss4MixerEnum;
typedef struct _GstOss4MixerEnumClass GstOss4MixerEnumClass;

struct _GstOss4MixerEnum {
  GstMixerOptions  mixer_option;

  GstOss4MixerControl  * mc;
  GstOss4Mixer         * mixer;      /* the mixer we belong to (no ref taken) */

  gboolean               need_update;
};

struct _GstOss4MixerEnumClass {
  GstMixerOptionsClass mixer_option_class;
};

GType           gst_oss4_mixer_enum_get_type (void);

gboolean        gst_oss4_mixer_enum_set_option (GstOss4MixerEnum * e, const gchar * value);

const gchar   * gst_oss4_mixer_enum_get_option (GstOss4MixerEnum * e);

GstMixerTrack * gst_oss4_mixer_enum_new (GstOss4Mixer * mixer, GstOss4MixerControl * mc);

void            gst_oss4_mixer_enum_process_change_unlocked (GstMixerTrack * track);

G_END_DECLS

#endif /* GST_OSS4_MIXER_ENUM_H */


