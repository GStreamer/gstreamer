/* ALSA mixer interface implementation.
 * Copyright (C) 2003 Leif Johnson <leif@ambient.2y.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __GST_ALSA_MIXER_H__
#define __GST_ALSA_MIXER_H__

#include "gstalsa.h"
#include "gstalsamixertrack.h"
#include <gst/mixer/mixer.h>

G_BEGIN_DECLS

#define GST_ALSA_MIXER(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ALSA_MIXER,GstAlsaMixer))
#define GST_ALSA_MIXER_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ALSA_MIXER,GstAlsaMixerClass))
#define GST_IS_ALSA_MIXER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ALSA_MIXER))
#define GST_IS_ALSA_MIXER_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ALSA_MIXER))
#define GST_TYPE_ALSA_MIXER		(gst_alsa_mixer_get_type())

typedef struct _GstAlsaMixer GstAlsaMixer;
typedef struct _GstAlsaMixerClass GstAlsaMixerClass;

struct _GstAlsaMixer {
  GstAlsa	parent;
  GList *	tracklist;	/* list of available tracks */
  snd_mixer_t *	mixer_handle;
};

struct _GstAlsaMixerClass {
  GstAlsaClass	parent;
};

GType		gst_alsa_mixer_get_type		(void);

G_END_DECLS

#endif /* __GST_ALSA_MIXER_H__ */
