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
 * License along with mixer library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-oss4mixer
 *
 * <refsect2>
 * <para>
 * This element lets you adjust sound input and output levels with the
 * Open Sound System (OSS) version 4. It supports the GstMixer interface, which
 * can be used to obtain a list of available mixer tracks. Set the mixer
 * element to READY state before using the GstMixer interface on it.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * oss4mixer can&apos;t be used in a sensible way in gst-launch.
 * </para>
 * </refsect2>
 *
 * Since: 0.10.7
 */

/* Note: ioctl calls on the same open mixer device are serialised via
 * the object lock to make sure we don't do concurrent ioctls from two
 * different threads (e.g. app thread and mixer watch thread), since that
 * will probably confuse OSS. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <gst/interfaces/mixer.h>
#include <gst/gst-i18n-plugin.h>

#include <glib/gprintf.h>

#define NO_LEGACY_MIXER

#include "oss4-audio.h"
#include "oss4-mixer.h"
#include "oss4-mixer-enum.h"
#include "oss4-mixer-slider.h"
#include "oss4-mixer-switch.h"
#include "oss4-property-probe.h"
#include "oss4-soundcard.h"

#define GST_OSS4_MIXER_WATCH_INTERVAL   500     /* in millisecs, ie. 0.5s */

GST_DEBUG_CATEGORY_EXTERN (oss4mixer_debug);
#define GST_CAT_DEFAULT oss4mixer_debug

#define DEFAULT_DEVICE       NULL
#define DEFAULT_DEVICE_NAME  NULL

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME
};

static void gst_oss4_mixer_init_interfaces (GType type);

GST_BOILERPLATE_FULL (GstOss4Mixer, gst_oss4_mixer, GstElement,
    GST_TYPE_ELEMENT, gst_oss4_mixer_init_interfaces);

static GstStateChangeReturn gst_oss4_mixer_change_state (GstElement *
    element, GstStateChange transition);

static void gst_oss4_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_oss4_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_oss4_mixer_finalize (GObject * object);

static gboolean gst_oss4_mixer_open (GstOss4Mixer * mixer,
    gboolean silent_errors);
static void gst_oss4_mixer_close (GstOss4Mixer * mixer);

static gboolean gst_oss4_mixer_enum_control_update_enum_list (GstOss4Mixer * m,
    GstOss4MixerControl * mc);

#ifndef GST_DISABLE_DEBUG
static const gchar *mixer_ext_type_get_name (gint type);
static const gchar *mixer_ext_flags_get_string (gint flags);
#endif

static void
gst_oss4_mixer_base_init (gpointer klass)
{
  gst_element_class_set_details_simple (GST_ELEMENT_CLASS (klass),
      "OSS v4 Audio Mixer", "Generic/Audio",
      "Control sound input and output levels with OSS4",
      "Tim-Philipp Müller <tim centricular net>");
}

static void
gst_oss4_mixer_class_init (GstOss4MixerClass * klass)
{
  GstElementClass *element_class;
  GObjectClass *gobject_class;

  element_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_oss4_mixer_finalize;
  gobject_class->set_property = gst_oss4_mixer_set_property;
  gobject_class->get_property = gst_oss4_mixer_get_property;

  /**
   * GstOss4Mixer:device
   *
   * OSS4 mixer device (e.g. /dev/oss/hdaudio0/mix0 or /dev/mixerN)
   *
   **/
  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "OSS mixer device (e.g. /dev/oss/hdaudio0/mix0 or /dev/mixerN) "
          "(NULL = use first mixer device found)", DEFAULT_DEVICE,
          G_PARAM_READWRITE));

  /**
   * GstOss4Mixer:device-name
   *
   * Human-readable name of the sound device. May be NULL if the device is
   * not open (ie. when the mixer is in NULL state)
   *
   **/
  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", DEFAULT_DEVICE_NAME,
          G_PARAM_READABLE));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_oss4_mixer_change_state);
}

static void
gst_oss4_mixer_finalize (GObject * obj)
{
  GstOss4Mixer *mixer = GST_OSS4_MIXER (obj);

  g_free (mixer->device);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_oss4_mixer_reset (GstOss4Mixer * mixer)
{
  mixer->fd = -1;
  mixer->need_update = TRUE;
  memset (&mixer->last_mixext, 0, sizeof (oss_mixext));
}

static void
gst_oss4_mixer_init (GstOss4Mixer * mixer, GstOss4MixerClass * g_class)
{
  mixer->device = g_strdup (DEFAULT_DEVICE);
  mixer->device_name = NULL;

  gst_oss4_mixer_reset (mixer);
}

static void
gst_oss4_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOss4Mixer *mixer = GST_OSS4_MIXER (object);

  switch (prop_id) {
    case PROP_DEVICE:
      GST_OBJECT_LOCK (mixer);
      if (!GST_OSS4_MIXER_IS_OPEN (mixer)) {
        g_free (mixer->device);
        mixer->device = g_value_dup_string (value);
        /* unset any cached device-name */
        g_free (mixer->device_name);
        mixer->device_name = NULL;
      } else {
        g_warning ("%s: can't change \"device\" property while mixer is open",
            GST_OBJECT_NAME (mixer));
      }
      GST_OBJECT_UNLOCK (mixer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_oss4_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOss4Mixer *mixer = GST_OSS4_MIXER (object);

  switch (prop_id) {
    case PROP_DEVICE:
      GST_OBJECT_LOCK (mixer);
      g_value_set_string (value, mixer->device);
      GST_OBJECT_UNLOCK (mixer);
      break;
    case PROP_DEVICE_NAME:
      GST_OBJECT_LOCK (mixer);
      /* If device is set, try to retrieve the name even if we're not open */
      if (mixer->fd == -1 && mixer->device != NULL) {
        if (gst_oss4_mixer_open (mixer, TRUE)) {
          g_value_set_string (value, mixer->device_name);
          gst_oss4_mixer_close (mixer);
        } else {
          g_value_set_string (value, mixer->device_name);
        }
      } else {
        g_value_set_string (value, mixer->device_name);
      }
      GST_OBJECT_UNLOCK (mixer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_oss4_mixer_open (GstOss4Mixer * mixer, gboolean silent_errors)
{
  struct oss_mixerinfo mi = { 0, };
  gchar *device;

  g_return_val_if_fail (!GST_OSS4_MIXER_IS_OPEN (mixer), FALSE);

  if (mixer->device)
    device = g_strdup (mixer->device);
  else
    device = gst_oss4_audio_find_device (GST_OBJECT_CAST (mixer));

  /* desperate times, desperate measures */
  if (device == NULL)
    device = g_strdup ("/dev/mixer");

  GST_INFO_OBJECT (mixer, "Trying to open OSS4 mixer device '%s'", device);

  mixer->fd = open (device, O_RDWR, 0);
  if (mixer->fd < 0)
    goto open_failed;

  /* Make sure it's OSS4. If it's old OSS, let the old ossmixer handle it */
  if (!gst_oss4_audio_check_version (GST_OBJECT (mixer), mixer->fd))
    goto legacy_oss;

  GST_INFO_OBJECT (mixer, "Opened mixer device '%s', which is mixer %d",
      device, mi.dev);

  /* Get device name for currently open fd */
  mi.dev = -1;
  if (ioctl (mixer->fd, SNDCTL_MIXERINFO, &mi) == 0) {
    mixer->modify_counter = mi.modify_counter;
    if (mi.name[0] != '\0') {
      mixer->device_name = g_strdup (mi.name);
    }
  } else {
    mixer->modify_counter = 0;
  }

  if (mixer->device_name == NULL) {
    mixer->device_name = g_strdup ("Unknown");
  }
  GST_INFO_OBJECT (mixer, "device name = '%s'", mixer->device_name);

  mixer->open_device = device;

  return TRUE;

  /* ERRORS */
open_failed:
  {
    if (!silent_errors) {
      GST_ELEMENT_ERROR (mixer, RESOURCE, OPEN_READ_WRITE,
          (_("Could not open audio device for mixer control handling.")),
          GST_ERROR_SYSTEM);
    } else {
      GST_DEBUG_OBJECT (mixer, "open failed: %s (ignoring errors)",
          g_strerror (errno));
    }
    g_free (device);
    return FALSE;
  }
legacy_oss:
  {
    gst_oss4_mixer_close (mixer);
    if (!silent_errors) {
      GST_ELEMENT_ERROR (mixer, RESOURCE, OPEN_READ_WRITE,
          (_("Could not open audio device for mixer control handling."
                  "This version of the Open Sound System is not supported by this "
                  "element.")), ("Try the 'ossmixer' element instead"));
    } else {
      GST_DEBUG_OBJECT (mixer, "open failed: legacy oss (ignoring errors)");
    }
    g_free (device);
    return FALSE;
  }
}

static void
gst_oss4_mixer_control_free (GstOss4MixerControl * mc)
{
  g_list_free (mc->children);
  g_list_free (mc->mute_group);
  g_free (mc->enum_vals);
  memset (mc, 0, sizeof (GstOss4MixerControl));
  g_free (mc);
}

static void
gst_oss4_mixer_free_tracks (GstOss4Mixer * mixer)
{
  g_list_foreach (mixer->tracks, (GFunc) g_object_unref, NULL);
  g_list_free (mixer->tracks);
  mixer->tracks = NULL;

  g_list_foreach (mixer->controls, (GFunc) gst_oss4_mixer_control_free, NULL);
  g_list_free (mixer->controls);
  mixer->controls = NULL;
}

static void
gst_oss4_mixer_close (GstOss4Mixer * mixer)
{
  g_free (mixer->device_name);
  mixer->device_name = NULL;

  g_free (mixer->open_device);
  mixer->open_device = NULL;

  gst_oss4_mixer_free_tracks (mixer);

  if (mixer->fd != -1) {
    close (mixer->fd);
    mixer->fd = -1;
  }

  gst_oss4_mixer_reset (mixer);
}

static void
gst_oss4_mixer_watch_process_changes (GstOss4Mixer * mixer)
{
  GList *c, *t, *tracks = NULL;

  GST_INFO_OBJECT (mixer, "mixer interface or control changed");

  /* this is all with the mixer object lock held */

  /* we go through the list backwards so we can bail out faster when the entire
   * interface needs to be rebuilt */
  for (c = g_list_last (mixer->controls); c != NULL; c = c->prev) {
    GstOss4MixerControl *mc = c->data;
    oss_mixer_value ossval = { 0, };

    mc->changed = FALSE;
    mc->list_changed = FALSE;

    /* not interested in controls we don't expose in the mixer interface */
    if (!mc->used)
      continue;

    /* don't try to read a value from controls that don't have one */
    if (mc->mixext.type == MIXT_DEVROOT || mc->mixext.type == MIXT_GROUP)
      continue;

    /* is this an enum control whose list may change? */
    if (mc->mixext.type == MIXT_ENUM && mc->enum_version != 0) {
      if (gst_oss4_mixer_enum_control_update_enum_list (mixer, mc))
        mc->list_changed = TRUE;
    }

    ossval.dev = mc->mixext.dev;
    ossval.ctrl = mc->mixext.ctrl;
    ossval.timestamp = mc->mixext.timestamp;

    if (ioctl (mixer->fd, SNDCTL_MIX_READ, &ossval) == -1) {
      if (errno == EIDRM || errno == EFAULT) {
        GST_DEBUG ("%s has disappeared", mc->mixext.extname);
        goto mixer_changed;
      }
      GST_WARNING_OBJECT (mixer, "MIX_READ failed: %s", g_strerror (errno));
      /* just ignore, move on to next one */
      continue;
    }

    if (ossval.value == mc->last_val) { /* no change */
      /* GST_LOG_OBJECT (mixer, "%s hasn't changed", mc->mixext.extname); */
      continue;
    }

    mc->last_val = ossval.value;
    GST_LOG_OBJECT (mixer, "%s changed value to %u 0x%08x",
        mc->mixext.extname, ossval.value, ossval.value);
    mc->changed = TRUE;
  }

  /* copy list and take track refs, so we can safely drop the object lock,
   * which we need to do to be able to post messages on the bus */
  tracks = g_list_copy (mixer->tracks);
  g_list_foreach (tracks, (GFunc) g_object_ref, NULL);

  GST_OBJECT_UNLOCK (mixer);

  /* since we don't know (or want to know exactly) which controls belong to
   * which track, we just go through the tracks one-by-one now and make them
   * check themselves if any of their controls have changed and which messages
   * to post on the bus as a result */
  for (t = tracks; t != NULL; t = t->next) {
    GstMixerTrack *track = t->data;

    if (GST_IS_OSS4_MIXER_SLIDER (track)) {
      gst_oss4_mixer_slider_process_change_unlocked (track);
    } else if (GST_IS_OSS4_MIXER_SWITCH (track)) {
      gst_oss4_mixer_switch_process_change_unlocked (track);
    } else if (GST_IS_OSS4_MIXER_ENUM (track)) {
      gst_oss4_mixer_enum_process_change_unlocked (track);
    }

    g_object_unref (track);
  }
  g_list_free (tracks);

  GST_OBJECT_LOCK (mixer);
  return;

mixer_changed:
  {
    GST_OBJECT_UNLOCK (mixer);
    gst_mixer_mixer_changed (GST_MIXER (mixer));
    GST_OBJECT_LOCK (mixer);
    return;
  }
}

/* This thread watches the mixer for changes in a somewhat inefficient way
 * (running an ioctl every half second or so). This is still better and
 * cheaper than apps polling all tracks for changes a few times a second
 * though. Needs more thought. There's probably (hopefully) a way to get
 * notifications via the fd directly somehow. */
static gpointer
gst_oss4_mixer_watch_thread (gpointer thread_data)
{
  GstOss4Mixer *mixer = GST_OSS4_MIXER_CAST (thread_data);

  GST_DEBUG_OBJECT (mixer, "watch thread running");

  GST_OBJECT_LOCK (mixer);
  while (!mixer->watch_shutdown) {
    oss_mixerinfo mi = { 0, };
    GTimeVal tv;

    mi.dev = -1;
    if (ioctl (mixer->fd, SNDCTL_MIXERINFO, &mi) == 0) {
      if (mixer->modify_counter != mi.modify_counter) {
        /* GST_LOG ("processing changes"); */
        gst_oss4_mixer_watch_process_changes (mixer);
        mixer->modify_counter = mi.modify_counter;
      } else {
        /* GST_LOG ("no changes"); */
      }
    } else {
      GST_WARNING_OBJECT (mixer, "MIXERINFO failed: %s", g_strerror (errno));
    }

    /* we could move the _get_current_time out of the loop and just do the
     * add in ever iteration, which would be less exact, but who cares */
    g_get_current_time (&tv);
    g_time_val_add (&tv, GST_OSS4_MIXER_WATCH_INTERVAL * 1000);
    g_cond_timed_wait (mixer->watch_cond, GST_OBJECT_GET_LOCK (mixer), &tv);
  }
  GST_OBJECT_UNLOCK (mixer);

  GST_DEBUG_OBJECT (mixer, "watch thread done");
  gst_object_unref (mixer);
  return NULL;
}

/* call with object lock held */
static void
gst_oss4_mixer_wake_up_watch_task (GstOss4Mixer * mixer)
{
  GST_LOG_OBJECT (mixer, "signalling watch thread to wake up");
  g_cond_signal (mixer->watch_cond);
}

static void
gst_oss4_mixer_stop_watch_task (GstOss4Mixer * mixer)
{
  if (mixer->watch_thread) {
    GST_OBJECT_LOCK (mixer);
    mixer->watch_shutdown = TRUE;
    GST_LOG_OBJECT (mixer, "signalling watch thread to stop");
    g_cond_signal (mixer->watch_cond);
    GST_OBJECT_UNLOCK (mixer);
    GST_LOG_OBJECT (mixer, "waiting for watch thread to join");
    g_thread_join (mixer->watch_thread);
    GST_DEBUG_OBJECT (mixer, "watch thread stopped");
    mixer->watch_thread = NULL;
  }

  if (mixer->watch_cond) {
    g_cond_free (mixer->watch_cond);
    mixer->watch_cond = NULL;
  }
}

static void
gst_oss4_mixer_start_watch_task (GstOss4Mixer * mixer)
{
  GError *err = NULL;

  mixer->watch_cond = g_cond_new ();
  mixer->watch_shutdown = FALSE;

  mixer->watch_thread = g_thread_create (gst_oss4_mixer_watch_thread,
      gst_object_ref (mixer), TRUE, &err);

  if (mixer->watch_thread == NULL) {
    GST_ERROR_OBJECT (mixer, "Could not create watch thread: %s", err->message);
    g_cond_free (mixer->watch_cond);
    mixer->watch_cond = NULL;
    g_error_free (err);
  }
}

static GstStateChangeReturn
gst_oss4_mixer_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstOss4Mixer *mixer = GST_OSS4_MIXER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_oss4_mixer_open (mixer, FALSE))
        return GST_STATE_CHANGE_FAILURE;
      gst_oss4_mixer_start_watch_task (mixer);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_oss4_mixer_stop_watch_task (mixer);
      gst_oss4_mixer_close (mixer);
      break;
    default:
      break;
  }

  return ret;
}

/* === GstMixer interface === */

static inline gboolean
gst_oss4_mixer_contains_track (GstMixer * mixer, GstMixerTrack * track)
{
  return (g_list_find (GST_OSS4_MIXER (mixer)->tracks, track) != NULL);
}

static inline gboolean
gst_oss4_mixer_contains_options (GstMixer * mixer, GstMixerOptions * options)
{
  return (g_list_find (GST_OSS4_MIXER (mixer)->tracks, options) != NULL);
}

static void
gst_oss4_mixer_post_mixer_changed_msg (GstOss4Mixer * mixer)
{
  /* only post mixer-changed message once */
  if (!mixer->need_update) {
    gst_mixer_mixer_changed (GST_MIXER (mixer));
    mixer->need_update = TRUE;
  }
}

/* call with mixer object lock held to serialise ioctl */
gboolean
gst_oss4_mixer_get_control_val (GstOss4Mixer * mixer, GstOss4MixerControl * mc,
    int *val)
{
  oss_mixer_value ossval = { 0, };

  if (GST_OBJECT_TRYLOCK (mixer)) {
    GST_ERROR ("must be called with mixer lock held");
    GST_OBJECT_UNLOCK (mixer);
  }

  ossval.dev = mc->mixext.dev;
  ossval.ctrl = mc->mixext.ctrl;
  ossval.timestamp = mc->mixext.timestamp;

  if (ioctl (mixer->fd, SNDCTL_MIX_READ, &ossval) == -1) {
    if (errno == EIDRM) {
      GST_DEBUG_OBJECT (mixer, "MIX_READ failed: mixer interface has changed");
      gst_oss4_mixer_post_mixer_changed_msg (mixer);
    } else {
      GST_WARNING_OBJECT (mixer, "MIX_READ failed: %s", g_strerror (errno));
    }
    *val = 0;
    mc->last_val = 0;
    return FALSE;
  }

  *val = ossval.value;
  mc->last_val = ossval.value;
  GST_LOG_OBJECT (mixer, "got value 0x%08x from %s)", *val, mc->mixext.extname);
  return TRUE;
}

/* call with mixer object lock held to serialise ioctl */
gboolean
gst_oss4_mixer_set_control_val (GstOss4Mixer * mixer, GstOss4MixerControl * mc,
    int val)
{
  oss_mixer_value ossval = { 0, };

  ossval.dev = mc->mixext.dev;
  ossval.ctrl = mc->mixext.ctrl;
  ossval.timestamp = mc->mixext.timestamp;
  ossval.value = val;

  if (GST_OBJECT_TRYLOCK (mixer)) {
    GST_ERROR ("must be called with mixer lock held");
    GST_OBJECT_UNLOCK (mixer);
  }

  if (ioctl (mixer->fd, SNDCTL_MIX_WRITE, &ossval) == -1) {
    if (errno == EIDRM) {
      GST_LOG_OBJECT (mixer, "MIX_WRITE failed: mixer interface has changed");
      gst_oss4_mixer_post_mixer_changed_msg (mixer);
    } else {
      GST_WARNING_OBJECT (mixer, "MIX_WRITE failed: %s", g_strerror (errno));
    }
    return FALSE;
  }

  mc->last_val = val;
  GST_LOG_OBJECT (mixer, "set value 0x%08x on %s", val, mc->mixext.extname);
  return TRUE;
}

static gchar *
gst_oss4_mixer_control_get_pretty_name (GstOss4MixerControl * mc)
{
  gchar *name;

#if 0
  const gchar *name, *u;

  /* "The id field is the original name given by the driver when it called
   * mixer_ext_create_control. This name can be used by fully featured GUI
   * mixers. However this name should be downshifted and cut before the last
   * underscore ("_") to get the proper name. For example mixer control name
   * created as "MYDRV_MAINVOL" will become just "mainvol" after this
   * transformation." */
  name = mc->mixext.extname;
  u = MAX (strrchr (name, '_'), strrchr (name, '.'));
  if (u != NULL)
    name = u + 1;

  /* maybe capitalize the first letter? */
  return g_ascii_strdown (name, -1);
#endif
  /* the .id thing doesn't really seem to work right, ie. for some sliders
   * it's just '-' so you have to use the name of the parent control etc.
   * let's not use it for now, much too painful. */
  if (g_str_has_prefix (mc->mixext.extname, "misc."))
    name = g_strdup (mc->mixext.extname + 5);
  else
    name = g_strdup (mc->mixext.extname);
  /* chop off trailing digit (only one for now) */
  if (strlen (name) > 0 && g_ascii_isdigit (name[strlen (name) - 1]))
    name[strlen (name) - 1] = '\0';
  g_strdelimit (name, ".", ' ');
  return name;
}

#if 0
/* FIXME: translations for common option strings */
static struct
{
  const gchar oss_name[32];
  const gchar *label;
} option_labels[] = {
  {
  "Fast", N_("Fast")}, {
  "Low", N_("Low")}, {
  "Medium", N_("Medium")}, {
  "High", N_("High")}, {
  "High+", N_("Very high")}, {
  "Production", N_("Production")}, {
  "OFF", N_("Off")}, {
  "ON", N_("On")}, {
  "Stereo", N_("Stereo")}, {
  "Multich", N_("Surround sound")}, {
  "input-mix", N_("Input mix")}, {
  "front", N_("Front")}, {
  "rear", N_("Rear")}, {
  "side", N_("Side")}, {
  "center/LFE", N_("Center / LFE")}, {
  "mic", N_("Microphone")}, {
  "fp-mic", N_("Front panel microphone")}, {
  "input", N_("Input")}, {
  "linein", N_("Line-in")}, {
  "pcm1", N_("PCM 1")}, {
  "pcm2", N_("PCM 2")}, {
  "pcm3", N_("PCM 3")}, {
"pcm4", N_("PCM 4")},};
#endif

/* these translations are a bit ad-hoc and horribly incomplete; it's not
 * really going to work this way with all the different chipsets and drivers */
static struct
{
  const gchar oss_name[32];
  const gchar *label;
} labels[] = {
  /* connectors (e.g. hdaudio) */
  {
  "jack.green", N_("Green connector")}, {
  "jack.fp-green", N_("Green front panel connector")}, {
  "jack.pink", N_("Pink connector")}, {
  "jack.fp-pink", N_("Pink front panel connector")}, {
  "jack.blue", N_("Blue connector")}, {
  "jack.fp-blue", N_("Blue front panel connector")}, {
  "jack.orange", N_("Orange connector")}, {
  "jack.fp-orange", N_("Orange front panel connector")}, {
  "jack.black", N_("Black connector")}, {
  "jack.fp-black", N_("Black front panel connector")}, {
  "jack.gray", N_("Gray connector")}, {
  "jack.fp-gray", N_("Gray front panel connector")}, {
  "jack.white", N_("White connector")}, {
  "jack.fp-white", N_("White front panel connector")}, {
  "jack.red", N_("Red connector")}, {
  "jack.fp-red", N_("Red front panel connector")}, {
  "jack.yellow", N_("Yellow connector")}, {
  "jack.fp-yellow", N_("Yellow front panel connector")},
      /* connector functions (e.g. hdaudio) */
  {
  "jack.green.mode", N_("Green connector function")}, {
  "jack.fp-green.mode", N_("Green front panel connector function")}, {
  "jack.pink.mode", N_("Pink connector function")}, {
  "jack.fp-pink.mode", N_("Pink front panel connector function")}, {
  "jack.blue.mode", N_("Blue connector function")}, {
  "jack.fp-blue.mode", N_("Blue front panel connector function")}, {
  "jack.orange.mode", N_("Orange connector function")}, {
  "jack.fp-orange.mode", N_("Orange front panel connector function")}, {
  "jack.black.mode", N_("Black connector function")}, {
  "jack.fp-black.mode", N_("Black front panel connector function")}, {
  "jack.gray.mode", N_("Gray connector function")}, {
  "jack.fp-gray.mode", N_("Gray front panel connector function")}, {
  "jack.white.mode", N_("White connector function")}, {
  "jack.fp-white.mode", N_("White front panel connector function")}, {
  "jack.red.mode", N_("Red connector function")}, {
  "jack.fp-red.mode", N_("Red front panel connector function")}, {
  "jack.yellow.mode", N_("Yellow connector function")}, {
  "jack.fp-yellow.mode", N_("Yellow front panel connector function")},
      /* other */
  {
  "misc.mic", N_("Microphone")}, {
  "misc.fp-mic", N_("Front panel microphone")}, {
  "misc.linein", N_("Line-in")}, {
  "misc.fp-linein", N_("Front panel line-in")}, {
  "misc.headphone", N_("Headphones")}, {
  "misc.fp-headphone", N_("Front panel headphones")}, {
  "misc.front", N_("Front")}, {
  "misc.rear", N_("Rear")}, {
  "misc.side", N_("Side")}, {
  "misc.center/lfe", N_("Center / LFE")}, {
  "misc.pcm", N_("PCM")}, {
  "misc.input-mix", N_("Input mix")}
  /* FIXME translate Audigy NX USB labels) */
/*
  { "rec.src", N_("Record Source") },
  { "output.mute", N_("Mute output") }
 headph (Controller group)
   headph.src (Enumeration control)
   headph.mute (On/Off switch)
 digital2 (Controller group)
   digital2.src (Enumeration control)
   digital2.mute (On/Off switch)
 digital (Controller group)
   digital.mute1 (On/Off switch)
   digital.vol (Controller group)
     digital.vol.front (Stereo slider (0-255))
     digital.vol.surr (Stereo slider (0-255))
     digital.vol.c/l (Stereo slider (0-255))
     digital.vol.center (Stereo slider (0-255))
   digital.mute2 (On/Off switch)
   digital.vol (Stereo slider (0-255))
 line (Controller group)
   line.mute (On/Off switch)
   line.vol (Stereo slider (0-255))
 play-altset (Enumeration control)
 rec-altset (Enumeration control)
*/
};

/* Decent i18n is pretty much impossible with OSS's way of providing us with
 * mixer labels (and the fact that they are pretty much random), but that
 * doesn't mean we shouldn't at least try. */
const gchar *
gst_oss4_mixer_control_get_translated_name (GstOss4MixerControl * mc)
{
  gchar name[33] = { 0, };
  char vmix_str[32] = { '\0', };
  int dummy, i;

  /* main virtual mixer controls (we hide the stream volumes) */
  if (sscanf (mc->mixext.extname, "vmix%d-%32c", &dummy, vmix_str) == 2) {
    if (strcmp (vmix_str, "src") == 0)
      return _("Virtual mixer input");
    else if (strcmp (vmix_str, "vol") == 0)
      return _("Virtual mixer output");
    else if (strcmp (vmix_str, "channels") == 0)
      return _("Virtual mixer channel configuration");
  }

  /* munge connector.foo => jack.foo (change from 4.0 -> 4.1) */
  if (g_str_has_prefix (mc->mixext.extname, "connector.")) {
    g_snprintf (name, sizeof (name), "jack.%s", mc->mixext.extname + 10);
  } else {
    g_strlcpy (name, mc->mixext.extname, sizeof (name));
  }

  /* munge foo.function => foo.mode (change from 4.0 -> 4.1) */
  if (g_str_has_suffix (name, ".function"))
    memcpy (name + strlen (name) - strlen (".function"), ".mode", 5 + 1);

  /* chop off trailing numbers */
  while (strlen (name) > 0 && g_ascii_isdigit (name[strlen (name) - 1]))
    name[strlen (name) - 1] = '\0';

  for (i = 0; i < G_N_ELEMENTS (labels); ++i) {
    if (strcmp (name, labels[i].oss_name) == 0)
      return _(labels[i].label);
  }

  g_strdelimit (name, ".", ' ');
  return g_quark_to_string (g_quark_from_string (name));        /* eek */
}

#ifndef GST_DISABLE_DEBUG
static const gchar *
mixer_ext_type_get_name (gint type)
{
  switch (type) {
    case MIXT_DEVROOT:
      return "Device root entry";
    case MIXT_GROUP:
      return "Controller group";
    case MIXT_ONOFF:
      return "On/Off switch";
    case MIXT_ENUM:
      return "Enumeration control";
    case MIXT_MONOSLIDER:
      return "Mono slider (0-255)";
    case MIXT_STEREOSLIDER:
      return "Stereo slider (0-255)";
    case MIXT_MESSAGE:
      return "Textual message"; /* whatever this is */
    case MIXT_MONOVU:
      return "Mono VU meter value";
    case MIXT_STEREOVU:
      return "Stereo VU meter value";
    case MIXT_MONOPEAK:
      return "Mono VU meter peak value";
    case MIXT_STEREOPEAK:
      return "Stereo VU meter peak value";
    case MIXT_RADIOGROUP:
      return "Radio button group";
    case MIXT_MARKER:          /* Separator between normal and extension entries */
      return "Separator";
    case MIXT_VALUE:
      return "Decimal value entry";
    case MIXT_HEXVALUE:
      return "Hex value entry";
    case MIXT_SLIDER:
      return "Mono slider (31-bit value range)";
    case MIXT_3D:
      return "3D";              /* what's this? */
    case MIXT_MONOSLIDER16:
      return "Mono slider (0-32767)";
    case MIXT_STEREOSLIDER16:
      return "Stereo slider (0-32767)";
    case MIXT_MUTE:
      return "Mute switch";
    default:
      break;
  }
  return "unknown";
}
#endif /* GST_DISABLE_DEBUG */

#ifndef GST_DISABLE_DEBUG
static const gchar *
mixer_ext_flags_get_string (gint flags)
{
  struct
  {
    gint flag;
    gchar nick[16];
  } all_flags[] = {
    /* first the important ones */
    {
    MIXF_MAINVOL, "MAINVOL"}, {
    MIXF_PCMVOL, "PCMVOL"}, {
    MIXF_RECVOL, "RECVOL"}, {
    MIXF_MONVOL, "MONVOL"}, {
    MIXF_DESCR, "DESCR"},
        /* now the rest in the right order */
    {
    MIXF_READABLE, "READABLE"}, {
    MIXF_WRITEABLE, "WRITABLE"}, {
    MIXF_POLL, "POLL"}, {
    MIXF_HZ, "HZ"}, {
    MIXF_STRING, "STRING"}, {
    MIXF_DYNAMIC, "DYNAMIC"}, {
    MIXF_OKFAIL, "OKFAIL"}, {
    MIXF_FLAT, "FLAT"}, {
    MIXF_LEGACY, "LEGACY"}, {
    MIXF_CENTIBEL, "CENTIBEL"}, {
    MIXF_DECIBEL, "DECIBEL"}, {
    MIXF_WIDE, "WIDE"}
  };
  GString *s;
  GQuark q;
  gint i;

  if (flags == 0)
    return "None";

  s = g_string_new ("");
  for (i = 0; i < G_N_ELEMENTS (all_flags); ++i) {
    if ((flags & all_flags[i].flag)) {
      if (s->len > 0)
        g_string_append (s, " | ");
      g_string_append (s, all_flags[i].nick);
      flags &= ~all_flags[i].flag;      /* unset */
    }
  }

  /* unknown flags? */
  if (flags != 0) {
    if (s->len > 0)
      g_string_append (s, " | ");
    g_string_append (s, "???");
  }

  /* we'll just put it into the global quark table (cheeky, eh?) */
  q = g_quark_from_string (s->str);
  g_string_free (s, TRUE);

  return g_quark_to_string (q);
}
#endif /* GST_DISABLE_DEBUG */

#ifndef GST_DISABLE_DEBUG
static void
gst_oss4_mixer_control_dump_tree (GstOss4MixerControl * mc, gint depth)
{
  GList *c;
  gchar spaces[64];
  gint i;

  depth = MIN (sizeof (spaces) - 1, depth);
  for (i = 0; i < depth; ++i)
    spaces[i] = ' ';
  spaces[i] = '\0';

  GST_LOG ("%s%s (%s)", spaces, mc->mixext.extname,
      mixer_ext_type_get_name (mc->mixext.type));
  for (c = mc->children; c != NULL; c = c->next) {
    GstOss4MixerControl *child_mc = (GstOss4MixerControl *) c->data;

    gst_oss4_mixer_control_dump_tree (child_mc, depth + 2);
  }
}
#endif /* GST_DISABLE_DEBUG */

static GList *
gst_oss4_mixer_get_controls (GstOss4Mixer * mixer)
{
  GstOss4MixerControl *root_mc = NULL;
  oss_mixerinfo mi = { 0, };
  GList *controls = NULL;
  GList *l;
  guint i;

  /* Get info for currently open fd */
  mi.dev = -1;
  if (ioctl (mixer->fd, SNDCTL_MIXERINFO, &mi) == -1)
    goto no_mixerinfo;

  if (mi.nrext <= 0) {
    GST_DEBUG ("mixer %s has no controls", mi.id);
    return NULL;
  }

  GST_INFO ("Reading controls for mixer %s", mi.id);

  for (i = 0; i < mi.nrext; ++i) {
    GstOss4MixerControl *mc;
    oss_mixext mix_ext = { 0, };

    mix_ext.dev = mi.dev;
    mix_ext.ctrl = i;

    if (ioctl (mixer->fd, SNDCTL_MIX_EXTINFO, &mix_ext) == -1) {
      GST_DEBUG ("SNDCTL_MIX_EXTINFO failed on mixer %s, control %d: %s",
          mi.id, i, g_strerror (errno));
      continue;
    }

    /* if this is the last one, save it for is-interface-up-to-date checking */
    if (i == mi.nrext)
      mixer->last_mixext = mix_ext;

    mc = g_new0 (GstOss4MixerControl, 1);
    mc->mixext = mix_ext;

    /* both control_no and desc fields are pretty useless, ie. not always set,
     * if ever, so not listed here */
    GST_INFO ("Control %d", mix_ext.ctrl);
    GST_INFO ("  name   : %s", mix_ext.extname);
    GST_INFO ("  type   : %s (%d)", mixer_ext_type_get_name (mix_ext.type),
        mix_ext.type);
    GST_INFO ("  flags  : %s (0x%04x)",
        mixer_ext_flags_get_string (mix_ext.flags), mix_ext.flags);
    GST_INFO ("  parent : %d", mix_ext.parent);

    /* get tooltip (just for informational purposes for now) */
    if (MIXEXT_HAS_DESCRIPTION (mix_ext)) {
      oss_mixer_enuminfo desc = { 0, };

      desc.dev = mix_ext.dev;
      desc.ctrl = mix_ext.ctrl;
      if (ioctl (mixer->fd, SNDCTL_MIX_DESCRIPTION, &desc) >= 0) {
        /* "The string may contain multiple lines. The first line is the
         *  'tooltip'. Optional subsequent lines may contain more detailed
         *  help text. Lines are separated by a linefeed character." */
        g_strdelimit (&desc.strings[desc.strindex[0]], "\n\r", '\0');
        GST_INFO ("  tooltip: %s", &desc.strings[desc.strindex[0]]);
      }
    }

    if (!MIXEXT_IS_ROOT (mix_ext)) {
      /* find parent (we assume it comes in the list before the child) */
      for (l = controls; l != NULL; l = l->next) {
        GstOss4MixerControl *parent_mc = (GstOss4MixerControl *) l->data;

        if (parent_mc->mixext.ctrl == mix_ext.parent) {
          mc->parent = parent_mc;
          parent_mc->children = g_list_append (parent_mc->children, mc);
          break;
        }
      }
      if (mc->parent == NULL) {
        GST_ERROR_OBJECT (mixer, "couldn't find parent for control %d", i);
        g_free (mc);
        continue;
      }
    } else if (root_mc == NULL) {
      root_mc = mc;
    } else {
      GST_WARNING_OBJECT (mixer, "two root controls?!");
    }

    controls = g_list_prepend (controls, mc);
  }

#ifndef GST_DISABLE_DEBUG
  gst_oss4_mixer_control_dump_tree (root_mc, 0);
#endif

  return g_list_reverse (controls);

/* ERRORS */
no_mixerinfo:
  {
    GST_WARNING ("SNDCTL_MIXERINFO failed on mixer device %s: %s", mi.id,
        g_strerror (errno));
    return NULL;
  }
}

static void
gst_oss4_mixer_controls_guess_master (GstOss4Mixer * mixer,
    const GList * controls)
{
  GstOss4MixerControl *firstpcm_mc = NULL;
  GstOss4MixerControl *master_mc = NULL;
  const GList *l;

  for (l = controls; l != NULL; l = l->next) {
    GstOss4MixerControl *mc = (GstOss4MixerControl *) l->data;

    if (((mc->mixext.flags & MIXF_MAINVOL)) && master_mc == NULL) {
      GST_INFO_OBJECT (mixer, "Master control: %s", mc->mixext.extname);
      master_mc = mc;
      break;
    }
    /* do we need to check if it's a slider type here? */
    if (((mc->mixext.flags & MIXF_PCMVOL)) && firstpcm_mc == NULL) {
      GST_INFO_OBJECT (mixer, "First PCM control: %s", mc->mixext.extname);
      firstpcm_mc = mc;
    }
  }

  /* if no control with MIXF_MAINVOL found, use first one with PCMVOL */
  if (master_mc == NULL && firstpcm_mc != NULL) {
    GST_INFO_OBJECT (mixer, "Marking first PCM control as master: %s",
        firstpcm_mc->mixext.extname);
    master_mc = firstpcm_mc;
  }

  if (master_mc != NULL)
    master_mc->is_master = TRUE;
}

/* type: -1 for all types, otherwise just return siblings with requested type */
static GList *
gst_oss4_mixer_control_get_siblings (GstOss4MixerControl * mc, gint type)
{
  GList *s, *siblings = NULL;

  if (mc->parent == NULL)
    return NULL;

  for (s = mc->parent->children; s != NULL; s = s->next) {
    GstOss4MixerControl *sibling = (GstOss4MixerControl *) s->data;

    if (mc != sibling && (type < 0 || sibling->mixext.type == type))
      siblings = g_list_append (siblings, sibling);
  }

  return siblings;
}

static void
gst_oss4_mixer_controls_find_sliders (GstOss4Mixer * mixer,
    const GList * controls)
{
  const GList *l;

  for (l = controls; l != NULL; l = l->next) {
    GstOss4MixerControl *mc = (GstOss4MixerControl *) l->data;
    GList *s, *siblings;

    if (!MIXEXT_IS_SLIDER (mc->mixext) || mc->parent == NULL || mc->used)
      continue;

    mc->is_slider = TRUE;
    mc->used = TRUE;

    siblings = gst_oss4_mixer_control_get_siblings (mc, -1);

    /* Note: the names can be misleading and may not reflect the actual
     * hierarchy of the controls, e.g. it's possible that a slider is called
     * connector.green and the mute control then connector.green.mute, but
     * both controls are in fact siblings and both children of the group
     * 'green' instead of mute being a child of connector.green as the naming
     * would seem to suggest */
    GST_LOG ("Slider: %s, parent=%s, %d siblings", mc->mixext.extname,
        mc->parent->mixext.extname, g_list_length (siblings));

    for (s = siblings; s != NULL; s = s->next) {
      GstOss4MixerControl *sibling = (GstOss4MixerControl *) s->data;

      GST_LOG ("    %s (%s)", sibling->mixext.extname,
          mixer_ext_type_get_name (sibling->mixext.type));

      if (sibling->mixext.type == MIXT_MUTE ||
          g_str_has_suffix (sibling->mixext.extname, ".mute")) {
        /* simple case: slider with single mute sibling. We assume the .mute
         * suffix in the name won't change - can't really do much else anyway */
        if (sibling->mixext.type == MIXT_ONOFF ||
            sibling->mixext.type == MIXT_MUTE) {
          GST_LOG ("    mute control for %s is %s", mc->mixext.extname,
              sibling->mixext.extname);
          mc->mute = sibling;
          sibling->used = TRUE;
        }
        /* a group of .mute controls. We assume they are all switches here */
        if (sibling->mixext.type == MIXT_GROUP) {
          GList *m;

          for (m = sibling->children; m != NULL; m = m->next) {
            GstOss4MixerControl *grouped_sibling = m->data;

            if (grouped_sibling->mixext.type == MIXT_ONOFF ||
                grouped_sibling->mixext.type == MIXT_MUTE) {
              GST_LOG ("    %s is grouped mute control for %s",
                  grouped_sibling->mixext.extname, mc->mixext.extname);
              mc->mute_group = g_list_append (mc->mute_group, grouped_sibling);
            }
          }

          GST_LOG ("    %s has a group of %d mute controls",
              mc->mixext.extname, g_list_length (mc->mute_group));

          /* we don't mark the individual mute controls as used, only the
           * group control, as we still want individual switches for the
           * individual controls */
          sibling->used = TRUE;
        }
      }
    }
    g_list_free (siblings);
  }
}

/* should be called with the mixer object lock held because of the ioctl;
 * returns TRUE if the list was read the first time or modified */
static gboolean
gst_oss4_mixer_enum_control_update_enum_list (GstOss4Mixer * mixer,
    GstOss4MixerControl * mc)
{
  oss_mixer_enuminfo ei = { 0, };
  guint num_existing = 0;
  int i;

  /* count and existing values */
  while (mc->enum_vals != NULL && mc->enum_vals[num_existing] != 0)
    ++num_existing;

  ei.dev = mc->mixext.dev;
  ei.ctrl = mc->mixext.ctrl;

  /* if we have create a generic list with numeric IDs already and the
   * number of values hasn't changed, then there's not much to do here */
  if (mc->no_list && mc->enum_vals != NULL &&
      num_existing == mc->mixext.maxvalue) {
    return FALSE;
  }

  /* if we have a list and it doesn't change, there's nothing to do either */
  if (mc->enum_vals != NULL && mc->enum_version == 0)
    return FALSE;

  if (ioctl (mixer->fd, SNDCTL_MIX_ENUMINFO, &ei) == -1) {
    g_free (mc->enum_vals);
    mc->enum_vals = g_new0 (GQuark, mc->mixext.maxvalue + 1);

    GST_DEBUG ("no enum info available, creating numeric values from 0-%d",
        mc->mixext.maxvalue - 1);

    /* "It is possible that some enum controls don't have any name list
     * available. In this case the application should automatically generate
     * list of numbers (0 to N-1)" */
    for (i = 0; i < mc->mixext.maxvalue; ++i) {
      gchar num_str[8];

      g_snprintf (num_str, sizeof (num_str), "%d", i);
      mc->enum_vals[i] = g_quark_from_string (num_str);
    }
    mc->enum_version = 0;       /* the only way to change is via maxvalue */
  } else {
    /* old list same as current list? */
    if (mc->enum_vals != NULL && mc->enum_version == ei.version)
      return FALSE;

    /* no list yet, or the list has changed */
    GST_LOG ("%s", (mc->enum_vals) ? "enum list has changed" : "reading list");
    if (ei.nvalues != mc->mixext.maxvalue) {
      GST_WARNING_OBJECT (mixer, "Enum: %s, nvalues %d != maxvalue %d",
          mc->mixext.extname, ei.nvalues, mc->mixext.maxvalue);
      mc->mixext.maxvalue = MIN (ei.nvalues, mc->mixext.maxvalue);
    }

    mc->mixext.maxvalue = MIN (mc->mixext.maxvalue, OSS_ENUM_MAXVALUE);

    g_free (mc->enum_vals);
    mc->enum_vals = g_new0 (GQuark, mc->mixext.maxvalue + 1);
    for (i = 0; i < mc->mixext.maxvalue; ++i) {
      GST_LOG ("  %s", ei.strings + ei.strindex[i]);
      mc->enum_vals[i] = g_quark_from_string (ei.strings + ei.strindex[i]);
    }
  }

  return TRUE;
}

static void
gst_oss4_mixer_controls_find_enums (GstOss4Mixer * mixer,
    const GList * controls)
{
  const GList *l;

  for (l = controls; l != NULL; l = l->next) {
    GstOss4MixerControl *mc = (GstOss4MixerControl *) l->data;

    if (mc->mixext.type != MIXT_ENUM || mc->used)
      continue;

    mc->is_enum = TRUE;
    mc->used = TRUE;

    /* Note: enums are special: for most controls, the maxvalue is inclusive,
     * but for enum controls it's actually exclusive (boggle), so that
     * mixext.maxvalue = num_values */

    GST_LOG ("Enum: %s, parent=%s, num_enums=%d", mc->mixext.extname,
        mc->parent->mixext.extname, mc->mixext.maxvalue);

    gst_oss4_mixer_enum_control_update_enum_list (mixer, mc);
  }
}

static void
gst_oss4_mixer_controls_find_switches (GstOss4Mixer * mixer,
    const GList * controls)
{
  const GList *l;

  for (l = controls; l != NULL; l = l->next) {
    GstOss4MixerControl *mc = (GstOss4MixerControl *) l->data;

    if (mc->used)
      continue;

    if (mc->mixext.type != MIXT_ONOFF && mc->mixext.type != MIXT_MUTE)
      continue;

    mc->is_switch = TRUE;
    mc->used = TRUE;

    GST_LOG ("Switch: %s, parent=%s", mc->mixext.extname,
        mc->parent->mixext.extname);
  }
}

static void
gst_oss4_mixer_controls_find_virtual (GstOss4Mixer * mixer,
    const GList * controls)
{
  const GList *l;

  for (l = controls; l != NULL; l = l->next) {
    GstOss4MixerControl *mc = (GstOss4MixerControl *) l->data;

    /* or sscanf (mc->mixext.extname, "vmix%d-out.", &n) == 1 ? */
    /* for now we just flag all virtual controls with managed labels, those
     * are really more appropriate for a pavucontrol-type control thing than
     * the (more hardware-oriented) mixer interface */
    if (mc->mixext.id[0] == '@') {
      mc->is_virtual = TRUE;
      GST_LOG ("%s is virtual control with managed label", mc->mixext.extname);
    }
  }
}

static void
gst_oss4_mixer_controls_dump_unused (GstOss4Mixer * mixer,
    const GList * controls)
{
  const GList *l;

  for (l = controls; l != NULL; l = l->next) {
    GstOss4MixerControl *mc = (GstOss4MixerControl *) l->data;

    if (mc->used)
      continue;

    switch (mc->mixext.type) {
      case MIXT_DEVROOT:
      case MIXT_GROUP:
      case MIXT_MESSAGE:
      case MIXT_MONOVU:
      case MIXT_STEREOVU:
      case MIXT_MONOPEAK:
      case MIXT_STEREOPEAK:
      case MIXT_MARKER:
        continue;               /* not interested in these types of controls */
      case MIXT_MONODB:
      case MIXT_STEREODB:
        GST_DEBUG ("obsolete control type %d", mc->mixext.type);
        continue;
      case MIXT_MONOSLIDER:
      case MIXT_STEREOSLIDER:
      case MIXT_SLIDER:
      case MIXT_MONOSLIDER16:
      case MIXT_STEREOSLIDER16:
        /* this shouldn't happen */
        GST_ERROR ("unused slider control?!");
        continue;
      case MIXT_VALUE:
      case MIXT_HEXVALUE:
        /* value entry, not sure what to do with that, skip for now */
        continue;
      case MIXT_ONOFF:
      case MIXT_MUTE:
      case MIXT_ENUM:
      case MIXT_3D:
      case MIXT_RADIOGROUP:
        GST_DEBUG ("FIXME: handle %s %s",
            mixer_ext_type_get_name (mc->mixext.type), mc->mixext.extname);
        break;
      default:
        GST_WARNING ("unknown control type %d", mc->mixext.type);
        continue;
    }
  }
}

static GList *
gst_oss4_mixer_create_tracks (GstOss4Mixer * mixer, const GList * controls)
{
  const GList *c;
  GList *tracks = NULL;

  for (c = controls; c != NULL; c = c->next) {
    GstOss4MixerControl *mc = (GstOss4MixerControl *) c->data;
    GstMixerTrack *track = NULL;

    if (mc->is_virtual)
      continue;

    if (mc->is_slider) {
      track = gst_oss4_mixer_slider_new (mixer, mc);
    } else if (mc->is_enum) {
      track = gst_oss4_mixer_enum_new (mixer, mc);
    } else if (mc->is_switch) {
      track = gst_oss4_mixer_switch_new (mixer, mc);
    }

    if (track == NULL)
      continue;

    track->label = gst_oss4_mixer_control_get_pretty_name (mc);
    track->flags = 0;

    GST_LOG ("translated label: %s [%s] = %s", track->label, mc->mixext.id,
        gst_oss4_mixer_control_get_translated_name (mc));

    /* This whole 'a track is either INPUT or OUTPUT' model is just flawed,
     * esp. if a slider's role can be changed on the fly, like when you change
     * function of a connector. What should we do in that case? Change the flag
     * and make the app rebuild the interface? Ignore it? */
    if (g_str_has_prefix (mc->mixext.extname, "record.")) {
      mc->is_output = FALSE;
      mc->is_input = TRUE;
    }

    /* FIXME: determine is_input and is_output */
    /* must be either INPUT or OUTPUT (but not both and not neither) for now,
     * or gnome-volume-control aborts */
    if (mc->is_input)
      track->flags |= GST_MIXER_TRACK_INPUT;
    else if (mc->is_output)
      track->flags |= GST_MIXER_TRACK_OUTPUT;
    else
      track->flags |= GST_MIXER_TRACK_OUTPUT;

    if (mc->is_master)
      track->flags |= GST_MIXER_TRACK_MASTER;

    tracks = g_list_append (tracks, track);
  }

  return tracks;
}

static void
gst_oss4_mixer_update_tracks (GstOss4Mixer * mixer)
{
  GList *controls, *tracks;

  /* read and process controls */
  controls = gst_oss4_mixer_get_controls (mixer);

  gst_oss4_mixer_controls_guess_master (mixer, controls);

  gst_oss4_mixer_controls_find_sliders (mixer, controls);

  gst_oss4_mixer_controls_find_enums (mixer, controls);

  gst_oss4_mixer_controls_find_switches (mixer, controls);

  gst_oss4_mixer_controls_find_virtual (mixer, controls);

  gst_oss4_mixer_controls_dump_unused (mixer, controls);

  tracks = gst_oss4_mixer_create_tracks (mixer, controls);

  /* free old tracks and controls */
  gst_oss4_mixer_free_tracks (mixer);

  /* replace old with new */
  mixer->tracks = tracks;
  mixer->controls = controls;
}

static const GList *
gst_oss4_mixer_list_tracks (GstMixer * mixer_iface)
{
  GstOss4Mixer *mixer = GST_OSS4_MIXER (mixer_iface);

  g_return_val_if_fail (mixer != NULL, NULL);
  g_return_val_if_fail (GST_OSS4_MIXER_IS_OPEN (mixer), NULL);

  GST_OBJECT_LOCK (mixer);

  /* Do a read on the last control to check if the interface has changed */
  if (!mixer->need_update && mixer->last_mixext.ctrl > 0) {
    GstOss4MixerControl mc = { {0,}
    ,
    };
    int val;

    mc.mixext = mixer->last_mixext;
    gst_oss4_mixer_get_control_val (mixer, &mc, &val);
  }

  if (mixer->need_update || mixer->tracks == NULL) {
    gst_oss4_mixer_update_tracks (mixer);
    mixer->need_update = FALSE;
  }

  GST_OBJECT_UNLOCK (mixer);

  return (const GList *) mixer->tracks;
}

static void
gst_oss4_mixer_set_volume (GstMixer * mixer, GstMixerTrack * track,
    gint * volumes)
{
  GstOss4Mixer *oss;

  g_return_if_fail (mixer != NULL);
  g_return_if_fail (GST_IS_OSS4_MIXER (mixer));
  g_return_if_fail (GST_OSS4_MIXER_IS_OPEN (mixer));
  g_return_if_fail (gst_oss4_mixer_contains_track (mixer, track));
  g_return_if_fail (volumes != NULL);

  oss = GST_OSS4_MIXER (mixer);

  GST_OBJECT_LOCK (oss);

  if (GST_IS_OSS4_MIXER_SLIDER (track)) {
    gst_oss4_mixer_slider_set_volume (GST_OSS4_MIXER_SLIDER (track), volumes);
  }

  GST_OBJECT_UNLOCK (oss);
}

static void
gst_oss4_mixer_get_volume (GstMixer * mixer, GstMixerTrack * track,
    gint * volumes)
{
  GstOss4Mixer *oss;

  g_return_if_fail (mixer != NULL);
  g_return_if_fail (GST_IS_OSS4_MIXER (mixer));
  g_return_if_fail (GST_OSS4_MIXER_IS_OPEN (mixer));
  g_return_if_fail (gst_oss4_mixer_contains_track (mixer, track));
  g_return_if_fail (volumes != NULL);

  oss = GST_OSS4_MIXER (mixer);

  GST_OBJECT_LOCK (oss);

  memset (volumes, 0, track->num_channels * sizeof (gint));

  if (GST_IS_OSS4_MIXER_SLIDER (track)) {
    gst_oss4_mixer_slider_get_volume (GST_OSS4_MIXER_SLIDER (track), volumes);
  }

  GST_OBJECT_UNLOCK (oss);
}

static void
gst_oss4_mixer_set_record (GstMixer * mixer, GstMixerTrack * track,
    gboolean record)
{
  GstOss4Mixer *oss;

  g_return_if_fail (mixer != NULL);
  g_return_if_fail (GST_IS_OSS4_MIXER (mixer));
  g_return_if_fail (GST_OSS4_MIXER_IS_OPEN (mixer));
  g_return_if_fail (gst_oss4_mixer_contains_track (mixer, track));

  oss = GST_OSS4_MIXER (mixer);

  GST_OBJECT_LOCK (oss);

  if (GST_IS_OSS4_MIXER_SLIDER (track)) {
    gst_oss4_mixer_slider_set_record (GST_OSS4_MIXER_SLIDER (track), record);
  } else if (GST_IS_OSS4_MIXER_SWITCH (track)) {
    if ((track->flags & GST_MIXER_TRACK_INPUT)) {
      gst_oss4_mixer_switch_set (GST_OSS4_MIXER_SWITCH (track), record);
    } else {
      GST_WARNING_OBJECT (track, "set_record called on non-INPUT track");
    }
  }

  GST_OBJECT_UNLOCK (oss);
}

static void
gst_oss4_mixer_set_mute (GstMixer * mixer, GstMixerTrack * track, gboolean mute)
{
  GstOss4Mixer *oss;

  g_return_if_fail (mixer != NULL);
  g_return_if_fail (GST_IS_OSS4_MIXER (mixer));
  g_return_if_fail (GST_OSS4_MIXER_IS_OPEN (mixer));
  g_return_if_fail (gst_oss4_mixer_contains_track (mixer, track));

  oss = GST_OSS4_MIXER (mixer);

  GST_OBJECT_LOCK (oss);

  if (GST_IS_OSS4_MIXER_SLIDER (track)) {
    gst_oss4_mixer_slider_set_mute (GST_OSS4_MIXER_SLIDER (track), mute);
  } else if (GST_IS_OSS4_MIXER_SWITCH (track)) {
    if ((track->flags & GST_MIXER_TRACK_OUTPUT)) {
      gst_oss4_mixer_switch_set (GST_OSS4_MIXER_SWITCH (track), mute);
    } else {
      GST_WARNING_OBJECT (track, "set_mute called on non-OUTPUT track");
    }
  }

  GST_OBJECT_UNLOCK (oss);
}

static void
gst_oss4_mixer_set_option (GstMixer * mixer, GstMixerOptions * options,
    gchar * value)
{
  GstOss4Mixer *oss;

  g_return_if_fail (mixer != NULL);
  g_return_if_fail (value != NULL);
  g_return_if_fail (GST_IS_OSS4_MIXER (mixer));
  g_return_if_fail (GST_OSS4_MIXER_IS_OPEN (mixer));
  g_return_if_fail (GST_IS_OSS4_MIXER_ENUM (options));
  g_return_if_fail (gst_oss4_mixer_contains_options (mixer, options));

  oss = GST_OSS4_MIXER (mixer);

  GST_OBJECT_LOCK (oss);

  if (!gst_oss4_mixer_enum_set_option (GST_OSS4_MIXER_ENUM (options), value)) {
    /* not much we can do here but wake up the watch thread early, so it
     * can do its thing and post messages if anything has changed */
    gst_oss4_mixer_wake_up_watch_task (oss);
  }

  GST_OBJECT_UNLOCK (oss);
}

static const gchar *
gst_oss4_mixer_get_option (GstMixer * mixer, GstMixerOptions * options)
{
  GstOss4Mixer *oss;
  const gchar *current_val;

  g_return_val_if_fail (mixer != NULL, NULL);
  g_return_val_if_fail (GST_IS_OSS4_MIXER (mixer), NULL);
  g_return_val_if_fail (GST_OSS4_MIXER_IS_OPEN (mixer), NULL);
  g_return_val_if_fail (GST_IS_OSS4_MIXER_ENUM (options), NULL);
  g_return_val_if_fail (gst_oss4_mixer_contains_options (mixer, options), NULL);

  oss = GST_OSS4_MIXER (mixer);

  GST_OBJECT_LOCK (oss);

  current_val = gst_oss4_mixer_enum_get_option (GST_OSS4_MIXER_ENUM (options));

  if (current_val == NULL) {
    /* not much we can do here but wake up the watch thread early, so it
     * can do its thing and post messages if anything has changed */
    gst_oss4_mixer_wake_up_watch_task (oss);
  }

  GST_OBJECT_UNLOCK (oss);

  return current_val;
}

static GstMixerFlags
gst_oss4_mixer_get_mixer_flags (GstMixer * mixer)
{
  return GST_MIXER_FLAG_AUTO_NOTIFICATIONS;
}

static void
gst_oss4_mixer_interface_init (GstMixerClass * klass)
{
  GST_MIXER_TYPE (klass) = GST_MIXER_HARDWARE;

  klass->list_tracks = gst_oss4_mixer_list_tracks;
  klass->set_volume = gst_oss4_mixer_set_volume;
  klass->get_volume = gst_oss4_mixer_get_volume;
  klass->set_mute = gst_oss4_mixer_set_mute;
  klass->set_record = gst_oss4_mixer_set_record;
  klass->set_option = gst_oss4_mixer_set_option;
  klass->get_option = gst_oss4_mixer_get_option;
  klass->get_mixer_flags = gst_oss4_mixer_get_mixer_flags;
}

/* Implement the horror that is GstImplementsInterface */

static gboolean
gst_oss4_mixer_supported (GstImplementsInterface * iface, GType iface_type)
{
  GstOss4Mixer *mixer;

  g_return_val_if_fail (iface_type == GST_TYPE_MIXER, FALSE);

  mixer = GST_OSS4_MIXER (iface);

  return GST_OSS4_MIXER_IS_OPEN (mixer);
}

static void
gst_oss4_mixer_implements_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_oss4_mixer_supported;
}

static void
gst_oss4_mixer_init_interfaces (GType type)
{
  static const GInterfaceInfo implements_iface_info = {
    (GInterfaceInitFunc) gst_oss4_mixer_implements_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo mixer_iface_info = {
    (GInterfaceInitFunc) gst_oss4_mixer_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);
  g_type_add_interface_static (type, GST_TYPE_MIXER, &mixer_iface_info);

  gst_oss4_add_property_probe_interface (type);
}
