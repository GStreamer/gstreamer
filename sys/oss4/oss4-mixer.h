/* GStreamer OSS4 mixer implementation
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

#ifndef OSS4_MIXER_H
#define OSS4_MIXER_H

#include <gst/gst.h>

#include "oss4-soundcard.h"

#define GST_OSS4_MIXER(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSS4_MIXER,GstOss4Mixer))
#define GST_OSS4_MIXER_CAST(obj)         ((GstOss4Mixer *)(obj))
#define GST_OSS4_MIXER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSS4_MIXER,GstOss4MixerClass))
#define GST_IS_OSS4_MIXER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSS4_MIXER))
#define GST_IS_OSS4_MIXER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSS4_MIXER))
#define GST_TYPE_OSS4_MIXER              (gst_oss4_mixer_get_type())

#define GST_OSS4_MIXER_IS_OPEN(obj)      (GST_OSS4_MIXER(obj)->fd != -1)

typedef struct _GstOss4Mixer GstOss4Mixer;
typedef struct _GstOss4MixerClass GstOss4MixerClass;

struct _GstOss4Mixer {
  GstElement            element;

  /*< private >*/

  /* element bits'n'bops */ 
  gchar               * device;

  /* mixer details */
  gint                  fd;             /* file descriptor if open, or -1    */
  gchar               * device_name;    /* device description, or NULL       */
  gchar               * open_device;    /* the device we opened              */

  GList               * tracks;         /* list of available tracks          */
  GList               * controls;       /* list of available controls        */
  gboolean              need_update;    /* re-read list of available tracks? */

  oss_mixext            last_mixext;    /* we keep this around so we can
                                         * easily check if the mixer
                                         * interface has changed             */

  GThread             * watch_thread;   /* thread watching for value changes */
  GCond               * watch_cond;
  gint                  watch_shutdown;
  gint                  modify_counter; /* from MIXERINFO */

  /* for property probe interface */
  GList               * property_probe_list;
};

struct _GstOss4MixerClass {
  GstElementClass       element_class;
};

/* helper struct holding info about one control */
typedef struct _GstOss4MixerControl GstOss4MixerControl;

struct _GstOss4MixerControl {
  oss_mixext           mixext;
  GstOss4MixerControl *parent;         /* NULL if root                         */
  GstOss4MixerControl *mute;           /* sibling with mute function, or NULL  */
  GList               *mute_group;     /* group of mute controls, or NULL      */
  GList               *children;       /* GstOss4MixerControls (no ownership)  */

  GQuark              *enum_vals;      /* 0-terminated array of values or NULL */
  int                  enum_version;   /* 0 = list won't change                */

  int                  last_val;       /* last value seen                      */

  gboolean             is_virtual : 1; /* is a vmix control with dynamic label */
  gboolean             is_master  : 1;
  gboolean             is_slider  : 1; /* represent as slider                  */
  gboolean             is_switch  : 1; /* represent as switch                  */
  gboolean             is_enum    : 1; /* represent as combo/enumeration       */
  gboolean             no_list    : 1; /* enumeration with no list available   */
  gboolean             is_input   : 1; /* is an input-related control          */
  gboolean             is_output  : 1; /* is an output-related control         */
  gboolean             used       : 1; /* whether we know what to do with this */

  gboolean             changed      : 1; /* transient flag used by watch thread */
  gboolean             list_changed : 1; /* transient flag used by watch thread */
};

/* header says parent=-1 means root, but it can also be parent=ctrl */
#define MIXEXT_IS_ROOT(me) ((me).parent == -1 || (me).parent == (me).ctrl)

#define MIXEXT_IS_SLIDER(me) ((me).type == MIXT_MONOSLIDER ||            \
    (me).type == MIXT_STEREOSLIDER || (me).type == MIXT_MONOSLIDER16 ||  \
    (me).type == MIXT_STEREOSLIDER16 || (me).type == MIXT_SLIDER)

#define MIXEXT_HAS_DESCRIPTION(me) (((me).flags & MIXF_DESCR) != 0)

#define MIXEXT_ENUM_IS_AVAILABLE(me,num) \
    (((me).enum_present[num/8]) & (1 << (num % 8)))


GType     gst_oss4_mixer_get_type (void);

gboolean  gst_oss4_mixer_get_control_val (GstOss4Mixer        * mixer,
                                          GstOss4MixerControl * mc,
                                          int                 * val);

gboolean  gst_oss4_mixer_set_control_val (GstOss4Mixer        * mixer,
                                          GstOss4MixerControl * mc,
                                          int                   val);

G_END_DECLS

#endif /* OSS4_MIXER_H */

