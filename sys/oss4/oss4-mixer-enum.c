/* GStreamer OSS4 mixer enumeration control
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

/* An 'enum' in gnome-volume-control / GstMixer is represented by a
 * GstMixerOptions object
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#define NO_LEGACY_MIXER
#include "oss4-mixer.h"
#include "oss4-mixer-enum.h"
#include "oss4-soundcard.h"

GST_DEBUG_CATEGORY_EXTERN (oss4mixer_debug);
#define GST_CAT_DEFAULT oss4mixer_debug

static GList *gst_oss4_mixer_enum_get_values (GstMixerOptions * options);

/* GstMixerTrack is a plain GObject, so let's just use the GLib macro here */
G_DEFINE_TYPE (GstOss4MixerEnum, gst_oss4_mixer_enum, GST_TYPE_MIXER_OPTIONS);

static void
gst_oss4_mixer_enum_init (GstOss4MixerEnum * e)
{
  e->need_update = TRUE;
}

static void
gst_oss4_mixer_enum_dispose (GObject * obj)
{
  GstMixerOptions *options = GST_MIXER_OPTIONS (obj);

  /* our list is a flat list with constant strings, but the GstMixerOptions
   * dispose will try to g_free the contained strings, so clean up the list
   * before chaining up to GstMixerOptions */
  g_list_free (options->values);
  options->values = NULL;

  G_OBJECT_CLASS (gst_oss4_mixer_enum_parent_class)->dispose (obj);
}

static void
gst_oss4_mixer_enum_class_init (GstOss4MixerEnumClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstMixerOptionsClass *mixeroptions_class = (GstMixerOptionsClass *) klass;

  gobject_class->dispose = gst_oss4_mixer_enum_dispose;
  mixeroptions_class->get_values = gst_oss4_mixer_enum_get_values;
}

static GList *
gst_oss4_mixer_enum_get_values_locked (GstMixerOptions * options)
{
  GstOss4MixerEnum *e = GST_OSS4_MIXER_ENUM_CAST (options);
  GList *oldlist, *list = NULL;
  int i;

  /* if current list of values is empty, update/re-check in any case */
  if (!e->need_update && options->values != NULL)
    return options->values;

  GST_LOG_OBJECT (e, "updating available values for %s", e->mc->mixext.extname);

  for (i = 0; i < e->mc->mixext.maxvalue; ++i) {
    const gchar *s;

    s = g_quark_to_string (e->mc->enum_vals[i]);
    if (MIXEXT_ENUM_IS_AVAILABLE (e->mc->mixext, i)) {
      GST_LOG_OBJECT (e, "option '%s' is available", s);
      list = g_list_prepend (list, (gpointer) s);
    } else {
      GST_LOG_OBJECT (e, "option '%s' is currently not available", s);
    }
  }

  list = g_list_reverse (list);

  /* this is not thread-safe, but then the entire GstMixer API isn't really,
   * since we return foo->list and not a copy and don't take any locks, so
   * not much we can do here but pray; we're usually either called from _new()
   * or from within _get_values() though, so it should be okay. We could use
   * atomic ops here, but I'm not sure how much more that really buys us.*/
  oldlist = options->values;    /* keep window small */
  options->values = list;
  g_list_free (oldlist);

  e->need_update = FALSE;

  return options->values;
}

static GList *
gst_oss4_mixer_enum_get_values (GstMixerOptions * options)
{
  GstOss4MixerEnum *e = GST_OSS4_MIXER_ENUM (options);
  GList *list;

  /* we take the lock here mostly to serialise ioctls with the watch thread */
  GST_OBJECT_LOCK (e->mixer);

  list = gst_oss4_mixer_enum_get_values_locked (options);

  GST_OBJECT_UNLOCK (e->mixer);

  return list;
}

static const gchar *
gst_oss4_mixer_enum_get_current_value (GstOss4MixerEnum * e)
{
  const gchar *cur_val = NULL;

  if (e->mc->enum_vals != NULL && e->mc->last_val < e->mc->mixext.maxvalue) {
    cur_val = g_quark_to_string (e->mc->enum_vals[e->mc->last_val]);
  }

  return cur_val;
}

static gboolean
gst_oss4_mixer_enum_update_current (GstOss4MixerEnum * e)
{
  int cur = -1;

  if (!gst_oss4_mixer_get_control_val (e->mixer, e->mc, &cur))
    return FALSE;

  if (cur < 0 || cur >= e->mc->mixext.maxvalue) {
    GST_WARNING_OBJECT (e, "read value %d out of bounds [0-%d]", cur,
        e->mc->mixext.maxvalue - 1);
    e->mc->last_val = 0;
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_oss4_mixer_enum_set_option (GstOss4MixerEnum * e, const gchar * value)
{
  GQuark q;
  int i;

  q = g_quark_try_string (value);
  if (q == 0) {
    GST_WARNING_OBJECT (e, "unknown option '%s'", value);
    return FALSE;
  }

  for (i = 0; i < e->mc->mixext.maxvalue; ++i) {
    if (q == e->mc->enum_vals[i])
      break;
  }

  if (i >= e->mc->mixext.maxvalue) {
    GST_WARNING_OBJECT (e, "option '%s' is not valid for this control", value);
    return FALSE;
  }

  GST_LOG_OBJECT (e, "option '%s' = %d", value, i);

  if (!MIXEXT_ENUM_IS_AVAILABLE (e->mc->mixext, i)) {
    GST_WARNING_OBJECT (e, "option '%s' is not selectable currently", value);
    return FALSE;
  }

  if (!gst_oss4_mixer_set_control_val (e->mixer, e->mc, i)) {
    GST_WARNING_OBJECT (e, "could not set option '%s' (%d)", value, i);
    return FALSE;
  }

  /* and re-read current value with sanity checks (or could just assign here) */
  gst_oss4_mixer_enum_update_current (e);

  return TRUE;
}

const gchar *
gst_oss4_mixer_enum_get_option (GstOss4MixerEnum * e)
{
  const gchar *cur_str = NULL;

  if (!gst_oss4_mixer_enum_update_current (e)) {
    GST_WARNING_OBJECT (e, "failed to read current value");
    return NULL;
  }

  cur_str = gst_oss4_mixer_enum_get_current_value (e);
  GST_LOG_OBJECT (e, "%s (%d)", GST_STR_NULL (cur_str), e->mc->last_val);
  return cur_str;
}

GstMixerTrack *
gst_oss4_mixer_enum_new (GstOss4Mixer * mixer, GstOss4MixerControl * mc)
{
  GstOss4MixerEnum *e;
  GstMixerTrack *track;

  e = g_object_new (GST_TYPE_OSS4_MIXER_ENUM, "untranslated-label",
      mc->mixext.extname, NULL);
  e->mixer = mixer;
  e->mc = mc;

  track = GST_MIXER_TRACK (e);

  /* caller will set track->label and track->flags */

  track->num_channels = 0;
  track->min_volume = 0;
  track->max_volume = 0;

  (void) gst_oss4_mixer_enum_get_values_locked (GST_MIXER_OPTIONS (track));

  if (!gst_oss4_mixer_enum_update_current (e)) {
    GST_WARNING_OBJECT (track, "failed to read current value, returning NULL");
    g_object_unref (track);
    track = NULL;
  }

  GST_LOG_OBJECT (e, "current value: %d (%s)", e->mc->last_val,
      gst_oss4_mixer_enum_get_current_value (e));

  return track;
}

/* This is called from the watch thread */
void
gst_oss4_mixer_enum_process_change_unlocked (GstMixerTrack * track)
{
  GstOss4MixerEnum *e = GST_OSS4_MIXER_ENUM_CAST (track);

  gchar *cur;

  if (!e->mc->changed && !e->mc->list_changed)
    return;

  if (e->mc->list_changed) {
    gst_mixer_options_list_changed (GST_MIXER (e->mixer),
        GST_MIXER_OPTIONS (e));
  }

  GST_OBJECT_LOCK (e->mixer);
  cur = (gchar *) gst_oss4_mixer_enum_get_current_value (e);
  GST_OBJECT_UNLOCK (e->mixer);

  gst_mixer_option_changed (GST_MIXER (e->mixer), GST_MIXER_OPTIONS (e), cur);
}
