/* GStreamer OSS4 audio source
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

/**
 * SECTION:element-oss4src
 *
 * This element lets you record sound using the Open Sound System (OSS)
 * version 4.
 * 
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v oss4src ! queue ! audioconvert ! vorbisenc ! oggmux ! filesink location=mymusic.ogg
 * ]| will record sound from your sound card using OSS4 and encode it to an
 * Ogg/Vorbis file (this will only work if your mixer settings are right
 * and the right inputs areenabled etc.)
 * </refsect2>
 *
 * Since: 0.10.7
 */

/* FIXME: make sure we're not doing ioctls from the app thread (e.g. via the
 * mixer interface) while recording */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <gst/interfaces/mixer.h>
#include <gst/gst-i18n-plugin.h>

#define NO_LEGACY_MIXER
#include "oss4-audio.h"
#include "oss4-source.h"
#include "oss4-property-probe.h"
#include "oss4-soundcard.h"

#define GST_OSS4_SOURCE_IS_OPEN(src)  (GST_OSS4_SOURCE(src)->fd != -1)

GST_DEBUG_CATEGORY_EXTERN (oss4src_debug);
#define GST_CAT_DEFAULT oss4src_debug

#define DEFAULT_DEVICE       NULL
#define DEFAULT_DEVICE_NAME  NULL

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME
};

static void gst_oss4_source_init_interfaces (GType type);

GST_BOILERPLATE_FULL (GstOss4Source, gst_oss4_source, GstAudioSrc,
    GST_TYPE_AUDIO_SRC, gst_oss4_source_init_interfaces);

static void gst_oss4_source_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_oss4_source_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_oss4_source_dispose (GObject * object);
static void gst_oss4_source_finalize (GstOss4Source * osssrc);

static GstCaps *gst_oss4_source_getcaps (GstBaseSrc * bsrc);

static gboolean gst_oss4_source_open (GstAudioSrc * asrc,
    gboolean silent_errors);
static gboolean gst_oss4_source_open_func (GstAudioSrc * asrc);
static gboolean gst_oss4_source_close (GstAudioSrc * asrc);
static gboolean gst_oss4_source_prepare (GstAudioSrc * asrc,
    GstRingBufferSpec * spec);
static gboolean gst_oss4_source_unprepare (GstAudioSrc * asrc);
static guint gst_oss4_source_read (GstAudioSrc * asrc, gpointer data,
    guint length);
static guint gst_oss4_source_delay (GstAudioSrc * asrc);
static void gst_oss4_source_reset (GstAudioSrc * asrc);

static void
gst_oss4_source_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstPadTemplate *templ;

  gst_element_class_set_details_simple (element_class,
      "OSS v4 Audio Source", "Source/Audio",
      "Capture from a sound card via OSS version 4",
      "Tim-Philipp Müller <tim centricular net>");

  templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_oss4_audio_get_template_caps ());
  gst_element_class_add_pad_template (element_class, templ);
}

static void
gst_oss4_source_class_init (GstOss4SourceClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstAudioSrcClass *gstaudiosrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstaudiosrc_class = (GstAudioSrcClass *) klass;

  gobject_class->dispose = gst_oss4_source_dispose;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_oss4_source_finalize;
  gobject_class->get_property = gst_oss4_source_get_property;
  gobject_class->set_property = gst_oss4_source_set_property;

  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_oss4_source_getcaps);

  gstaudiosrc_class->open = GST_DEBUG_FUNCPTR (gst_oss4_source_open_func);
  gstaudiosrc_class->prepare = GST_DEBUG_FUNCPTR (gst_oss4_source_prepare);
  gstaudiosrc_class->unprepare = GST_DEBUG_FUNCPTR (gst_oss4_source_unprepare);
  gstaudiosrc_class->close = GST_DEBUG_FUNCPTR (gst_oss4_source_close);
  gstaudiosrc_class->read = GST_DEBUG_FUNCPTR (gst_oss4_source_read);
  gstaudiosrc_class->delay = GST_DEBUG_FUNCPTR (gst_oss4_source_delay);
  gstaudiosrc_class->reset = GST_DEBUG_FUNCPTR (gst_oss4_source_reset);

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "OSS4 device (e.g. /dev/oss/hdaudio0/pcm0 or /dev/dspN) "
          "(NULL = use first available device)",
          DEFAULT_DEVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", DEFAULT_DEVICE_NAME,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_oss4_source_init (GstOss4Source * osssrc, GstOss4SourceClass * g_class)
{
  const gchar *device;

  device = g_getenv ("AUDIODEV");
  if (device == NULL)
    device = DEFAULT_DEVICE;

  osssrc->fd = -1;
  osssrc->device = g_strdup (device);
  osssrc->device_name = g_strdup (DEFAULT_DEVICE_NAME);
  osssrc->device_name = NULL;
}

static void
gst_oss4_source_finalize (GstOss4Source * oss)
{
  g_free (oss->device);
  oss->device = NULL;

  g_list_free (oss->property_probe_list);
  oss->property_probe_list = NULL;

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (oss));
}

static void
gst_oss4_source_dispose (GObject * object)
{
  GstOss4Source *oss = GST_OSS4_SOURCE (object);

  if (oss->probed_caps) {
    gst_caps_unref (oss->probed_caps);
    oss->probed_caps = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_oss4_source_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOss4Source *oss;

  oss = GST_OSS4_SOURCE (object);

  switch (prop_id) {
    case PROP_DEVICE:
      GST_OBJECT_LOCK (oss);
      if (oss->fd == -1) {
        g_free (oss->device);
        oss->device = g_value_dup_string (value);
        g_free (oss->device_name);
        oss->device_name = NULL;
      } else {
        g_warning ("%s: can't change \"device\" property while audio source "
            "is open", GST_OBJECT_NAME (oss));
      }
      GST_OBJECT_UNLOCK (oss);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_oss4_source_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOss4Source *oss;

  oss = GST_OSS4_SOURCE (object);

  switch (prop_id) {
    case PROP_DEVICE:
      GST_OBJECT_LOCK (oss);
      g_value_set_string (value, oss->device);
      GST_OBJECT_UNLOCK (oss);
      break;
    case PROP_DEVICE_NAME:
      GST_OBJECT_LOCK (oss);
      /* If device is set, try to retrieve the name even if we're not open */
      if (oss->fd == -1 && oss->device != NULL) {
        if (gst_oss4_source_open (GST_AUDIO_SRC (oss), TRUE)) {
          g_value_set_string (value, oss->device_name);
          gst_oss4_source_close (GST_AUDIO_SRC (oss));
        } else {
          gchar *name = NULL;

          gst_oss4_property_probe_find_device_name_nofd (GST_OBJECT (oss),
              oss->device, &name);
          g_value_set_string (value, name);
          g_free (name);
        }
      } else {
        g_value_set_string (value, oss->device_name);
      }

      GST_OBJECT_UNLOCK (oss);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_oss4_source_getcaps (GstBaseSrc * bsrc)
{
  GstOss4Source *oss;
  GstCaps *caps;

  oss = GST_OSS4_SOURCE (bsrc);

  if (oss->fd == -1) {
    caps = gst_caps_copy (gst_oss4_audio_get_template_caps ());
  } else if (oss->probed_caps) {
    caps = gst_caps_copy (oss->probed_caps);
  } else {
    caps = gst_oss4_audio_probe_caps (GST_OBJECT (oss), oss->fd);
    if (caps != NULL && !gst_caps_is_empty (caps)) {
      oss->probed_caps = gst_caps_copy (caps);
    }
  }

  return caps;
}

/* note: we must not take the object lock here unless we fix up get_property */
static gboolean
gst_oss4_source_open (GstAudioSrc * asrc, gboolean silent_errors)
{
  GstOss4Source *oss;
  gchar *device;
  int mode;

  oss = GST_OSS4_SOURCE (asrc);

  if (oss->device)
    device = g_strdup (oss->device);
  else
    device = gst_oss4_audio_find_device (GST_OBJECT_CAST (oss));

  /* desperate times, desperate measures */
  if (device == NULL)
    device = g_strdup ("/dev/dsp0");

  GST_INFO_OBJECT (oss, "Trying to open OSS4 device '%s'", device);

  /* we open in non-blocking mode even if we don't really want to do writes
   * non-blocking because we can't be sure that this is really a genuine
   * OSS4 device with well-behaved drivers etc. We really don't want to
   * hang forever under any circumstances. */
  oss->fd = open (device, O_RDONLY | O_NONBLOCK, 0);
  if (oss->fd == -1) {
    switch (errno) {
      case EBUSY:
        goto busy;
      case EACCES:
        goto no_permission;
      default:
        goto open_failed;
    }
  }

  GST_INFO_OBJECT (oss, "Opened device");

  /* Make sure it's OSS4. If it's old OSS, let osssink handle it */
  if (!gst_oss4_audio_check_version (GST_OBJECT_CAST (oss), oss->fd))
    goto legacy_oss;

  /* now remove the non-blocking flag. */
  mode = fcntl (oss->fd, F_GETFL);
  mode &= ~O_NONBLOCK;
  if (fcntl (oss->fd, F_SETFL, mode) < 0) {
    /* some drivers do no support unsetting the non-blocking flag, try to
     * close/open the device then. This is racy but we error out properly. */
    GST_WARNING_OBJECT (oss, "failed to unset O_NONBLOCK (buggy driver?), "
        "will try to re-open device now");
    gst_oss4_source_close (asrc);
    if ((oss->fd = open (device, O_RDONLY, 0)) == -1)
      goto non_block;
  }

  oss->open_device = device;

  /* not using ENGINEINFO here because it sometimes returns a different and
   * less useful name than AUDIOINFO for the same device */
  if (!gst_oss4_property_probe_find_device_name (GST_OBJECT (oss), oss->fd,
          oss->open_device, &oss->device_name)) {
    oss->device_name = NULL;
  }

  return TRUE;

  /* ERRORS */
busy:
  {
    if (!silent_errors) {
      GST_ELEMENT_ERROR (oss, RESOURCE, BUSY,
          (_("Could not open audio device for playback. "
                  "Device is being used by another application.")), (NULL));
    }
    g_free (device);
    return FALSE;
  }
no_permission:
  {
    if (!silent_errors) {
      GST_ELEMENT_ERROR (oss, RESOURCE, OPEN_READ,
          (_("Could not open audio device for playback. "
                  "You don't have permission to open the device.")),
          GST_ERROR_SYSTEM);
    }
    g_free (device);
    return FALSE;
  }
open_failed:
  {
    if (!silent_errors) {
      GST_ELEMENT_ERROR (oss, RESOURCE, OPEN_READ,
          (_("Could not open audio device for playback.")), GST_ERROR_SYSTEM);
    }
    g_free (device);
    return FALSE;
  }
legacy_oss:
  {
    gst_oss4_source_close (asrc);
    if (!silent_errors) {
      GST_ELEMENT_ERROR (oss, RESOURCE, OPEN_READ,
          (_("Could not open audio device for playback. "
                  "This version of the Open Sound System is not supported by this "
                  "element.")), ("Try the 'osssink' element instead"));
    }
    g_free (device);
    return FALSE;
  }
non_block:
  {
    if (!silent_errors) {
      GST_ELEMENT_ERROR (oss, RESOURCE, SETTINGS, (NULL),
          ("Unable to set device %s into non-blocking mode: %s",
              oss->device, g_strerror (errno)));
    }
    g_free (device);
    return FALSE;
  }
}

static gboolean
gst_oss4_source_open_func (GstAudioSrc * asrc)
{
  return gst_oss4_source_open (asrc, FALSE);
}

static void
gst_oss4_source_free_mixer_tracks (GstOss4Source * oss)
{
  g_list_foreach (oss->tracks, (GFunc) g_object_unref, NULL);
  g_list_free (oss->tracks);
  oss->tracks = NULL;
}

static gboolean
gst_oss4_source_close (GstAudioSrc * asrc)
{
  GstOss4Source *oss;

  oss = GST_OSS4_SOURCE (asrc);

  if (oss->fd != -1) {
    GST_DEBUG_OBJECT (oss, "closing device");
    close (oss->fd);
    oss->fd = -1;
  }

  oss->bytes_per_sample = 0;

  gst_caps_replace (&oss->probed_caps, NULL);

  g_free (oss->open_device);
  oss->open_device = NULL;

  g_free (oss->device_name);
  oss->device_name = NULL;

  gst_oss4_source_free_mixer_tracks (oss);

  return TRUE;
}

static gboolean
gst_oss4_source_prepare (GstAudioSrc * asrc, GstRingBufferSpec * spec)
{
  GstOss4Source *oss;

  oss = GST_OSS4_SOURCE (asrc);

  if (!gst_oss4_audio_set_format (GST_OBJECT_CAST (oss), oss->fd, spec)) {
    GST_WARNING_OBJECT (oss, "Couldn't set requested format %" GST_PTR_FORMAT,
        spec->caps);
    return FALSE;
  }

  oss->bytes_per_sample = spec->bytes_per_sample;
  return TRUE;
}

static gboolean
gst_oss4_source_unprepare (GstAudioSrc * asrc)
{
  /* could do a SNDCTL_DSP_HALT, but the OSS manual recommends a close/open,
   * since HALT won't properly reset some devices, apparently */

  if (!gst_oss4_source_close (asrc))
    goto couldnt_close;

  if (!gst_oss4_source_open_func (asrc))
    goto couldnt_reopen;

  return TRUE;

  /* ERRORS */
couldnt_close:
  {
    GST_DEBUG_OBJECT (asrc, "Couldn't close the audio device");
    return FALSE;
  }
couldnt_reopen:
  {
    GST_DEBUG_OBJECT (asrc, "Couldn't reopen the audio device");
    return FALSE;
  }
}

static guint
gst_oss4_source_read (GstAudioSrc * asrc, gpointer data, guint length)
{
  GstOss4Source *oss;
  int n;

  oss = GST_OSS4_SOURCE_CAST (asrc);

  n = read (oss->fd, data, length);
  GST_LOG_OBJECT (asrc, "%u bytes, %u samples", n, n / oss->bytes_per_sample);

  if (G_UNLIKELY (n < 0)) {
    switch (errno) {
      case ENOTSUP:
      case EACCES:{
        /* This is the most likely cause, I think */
        GST_ELEMENT_ERROR (asrc, RESOURCE, READ,
            (_("Recording is not supported by this audio device.")),
            ("read: %s (device: %s) (maybe this is an output-only device?)",
                g_strerror (errno), oss->open_device));
        break;
      }
      default:{
        GST_ELEMENT_ERROR (asrc, RESOURCE, READ,
            (_("Error recording from audio device.")),
            ("read: %s (device: %s)", g_strerror (errno), oss->open_device));
        break;
      }
    }
  }

  return (guint) n;
}

static guint
gst_oss4_source_delay (GstAudioSrc * asrc)
{
  audio_buf_info info = { 0, };
  GstOss4Source *oss;
  guint delay;

  oss = GST_OSS4_SOURCE_CAST (asrc);

  if (ioctl (oss->fd, SNDCTL_DSP_GETISPACE, &info) == -1) {
    GST_LOG_OBJECT (oss, "GETISPACE failed: %s", g_strerror (errno));
    return 0;
  }

  delay = (info.fragstotal * info.fragsize) - info.bytes;
  GST_LOG_OBJECT (oss, "fragstotal:%d, fragsize:%d, bytes:%d, delay:%d",
      info.fragstotal, info.fragsize, info.bytes, delay);
  return delay;
}

static void
gst_oss4_source_reset (GstAudioSrc * asrc)
{
  /* There's nothing we can do here really: OSS can't handle access to the
   * same device/fd from multiple threads and might deadlock or blow up in
   * other ways if we try an ioctl SNDCTL_DSP_HALT or similar */
}

/* GstMixer interface, which we abuse here for input selection, because we
 * don't have a proper interface for that and because that's what
 * gnome-sound-recorder does. */

/* GstMixerTrack is a plain GObject, so let's just use the GLib macro here */
G_DEFINE_TYPE (GstOss4SourceInput, gst_oss4_source_input, GST_TYPE_MIXER_TRACK);

static void
gst_oss4_source_input_class_init (GstOss4SourceInputClass * klass)
{
  /* nothing to do here */
}

static void
gst_oss4_source_input_init (GstOss4SourceInput * i)
{
  /* nothing to do here */
}

#if 0

static void
gst_ossmixer_ensure_track_list (GstOssMixer * mixer)
{
  gint i, master = -1;

  g_return_if_fail (mixer->fd != -1);

  if (mixer->tracklist)
    return;

  /* find master volume */
  if (mixer->devmask & SOUND_MASK_VOLUME)
    master = SOUND_MIXER_VOLUME;
  else if (mixer->devmask & SOUND_MASK_PCM)
    master = SOUND_MIXER_PCM;
  else if (mixer->devmask & SOUND_MASK_SPEAKER)
    master = SOUND_MIXER_SPEAKER;       /* doubtful... */
  /* else: no master, so we won't set any */

  /* build track list */
  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
    if (mixer->devmask & (1 << i)) {
      GstMixerTrack *track;
      gboolean input = FALSE, stereo = FALSE, record = FALSE;

      /* track exists, make up capabilities */
      if (MASK_BIT_IS_SET (mixer->stereomask, i))
        stereo = TRUE;
      if (MASK_BIT_IS_SET (mixer->recmask, i))
        input = TRUE;
      if (MASK_BIT_IS_SET (mixer->recdevs, i))
        record = TRUE;

      /* do we want mixer in our list? */
      if (!((mixer->dir & GST_OSS_MIXER_CAPTURE && input == TRUE) ||
              (mixer->dir & GST_OSS_MIXER_PLAYBACK && i != SOUND_MIXER_PCM)))
        /* the PLAYBACK case seems hacky, but that's how 0.8 had it */
        continue;

      /* add track to list */
      track = gst_ossmixer_track_new (mixer->fd, i, stereo ? 2 : 1,
          (record ? GST_MIXER_TRACK_RECORD : 0) |
          (input ? GST_MIXER_TRACK_INPUT :
              GST_MIXER_TRACK_OUTPUT) |
          ((master != i) ? 0 : GST_MIXER_TRACK_MASTER));
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }
  }
}

/* unused with G_DISABLE_* */
static G_GNUC_UNUSED gboolean
gst_ossmixer_contains_track (GstOssMixer * mixer, GstOssMixerTrack * osstrack)
{
  const GList *item;

  for (item = mixer->tracklist; item != NULL; item = item->next)
    if (item->data == osstrack)
      return TRUE;

  return FALSE;
}

const GList *
gst_ossmixer_list_tracks (GstOssMixer * mixer)
{
  gst_ossmixer_ensure_track_list (mixer);

  return (const GList *) mixer->tracklist;
}

void
gst_ossmixer_get_volume (GstOssMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint volume;
  GstOssMixerTrack *osstrack = GST_OSSMIXER_TRACK (track);

  g_return_if_fail (mixer->fd != -1);
  g_return_if_fail (gst_ossmixer_contains_track (mixer, osstrack));

  if (track->flags & GST_MIXER_TRACK_MUTE) {
    volumes[0] = osstrack->lvol;
    if (track->num_channels == 2) {
      volumes[1] = osstrack->rvol;
    }
  } else {
    /* get */
    if (ioctl (mixer->fd, MIXER_READ (osstrack->track_num), &volume) < 0) {
      g_warning ("Error getting recording device (%d) volume: %s",
          osstrack->track_num, g_strerror (errno));
      volume = 0;
    }

    osstrack->lvol = volumes[0] = (volume & 0xff);
    if (track->num_channels == 2) {
      osstrack->rvol = volumes[1] = ((volume >> 8) & 0xff);
    }
  }
}

void
gst_ossmixer_set_mute (GstOssMixer * mixer, GstMixerTrack * track,
    gboolean mute)
{
  int volume;
  GstOssMixerTrack *osstrack = GST_OSSMIXER_TRACK (track);

  g_return_if_fail (mixer->fd != -1);
  g_return_if_fail (gst_ossmixer_contains_track (mixer, osstrack));

  if (mute) {
    volume = 0;
  } else {
    volume = (osstrack->lvol & 0xff);
    if (MASK_BIT_IS_SET (mixer->stereomask, osstrack->track_num)) {
      volume |= ((osstrack->rvol & 0xff) << 8);
    }
  }

  if (ioctl (mixer->fd, MIXER_WRITE (osstrack->track_num), &volume) < 0) {
    g_warning ("Error setting mixer recording device volume (0x%x): %s",
        volume, g_strerror (errno));
    return;
  }

  if (mute) {
    track->flags |= GST_MIXER_TRACK_MUTE;
  } else {
    track->flags &= ~GST_MIXER_TRACK_MUTE;
  }
}
#endif

static gint
gst_oss4_source_mixer_get_current_input (GstOss4Source * oss)
{
  int cur = -1;

  if (ioctl (oss->fd, SNDCTL_DSP_GET_RECSRC, &cur) == -1 || cur < 0)
    return -1;

  return cur;
}

static const gchar *
gst_oss4_source_mixer_update_record_flags (GstOss4Source * oss, gint cur_route)
{
  const gchar *cur_name = "";
  GList *t;

  for (t = oss->tracks; t != NULL; t = t->next) {
    GstMixerTrack *track = t->data;

    if (GST_OSS4_SOURCE_INPUT (track)->route == cur_route) {
      if (!GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_RECORD)) {
        track->flags |= GST_MIXER_TRACK_RECORD;
        /* no point in sending a mixer-record-changes message here */
      }
      cur_name = track->label;
    } else {
      if (GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_RECORD)) {
        track->flags &= ~GST_MIXER_TRACK_RECORD;
        /* no point in sending a mixer-record-changes message here */
      }
    }
  }

  return cur_name;
}

static const GList *
gst_oss4_source_mixer_list_tracks (GstMixer * mixer)
{
  oss_mixer_enuminfo names = { 0, };
  GstOss4Source *oss;
  const gchar *cur_name;
  GList *tracks = NULL;
  gint i, cur;

  g_return_val_if_fail (mixer != NULL, NULL);
  g_return_val_if_fail (GST_IS_OSS4_SOURCE (mixer), NULL);
  g_return_val_if_fail (GST_OSS4_SOURCE_IS_OPEN (mixer), NULL);

  oss = GST_OSS4_SOURCE (mixer);

  if (oss->tracks != NULL && oss->tracks_static)
    goto done;

  if (ioctl (oss->fd, SNDCTL_DSP_GET_RECSRC_NAMES, &names) == -1)
    goto get_recsrc_names_error;

  oss->tracks_static = (names.version == 0);

  GST_INFO_OBJECT (oss, "%d inputs (list is static: %s):", names.nvalues,
      (oss->tracks_static) ? "yes" : "no");

  for (i = 0; i < MIN (names.nvalues, OSS_ENUM_MAXVALUE + 1); ++i) {
    GstMixerTrack *track;

    track = g_object_new (GST_TYPE_OSS4_SOURCE_INPUT, NULL);
    track->label = g_strdup (&names.strings[names.strindex[i]]);
    track->flags = GST_MIXER_TRACK_INPUT;
    track->num_channels = 2;
    track->min_volume = 0;
    track->max_volume = 100;
    GST_OSS4_SOURCE_INPUT (track)->route = i;

    GST_INFO_OBJECT (oss, " [%d] %s", i, track->label);
    tracks = g_list_append (tracks, track);
  }

  gst_oss4_source_free_mixer_tracks (oss);
  oss->tracks = tracks;

done:

  /* update RECORD flags */
  cur = gst_oss4_source_mixer_get_current_input (oss);
  cur_name = gst_oss4_source_mixer_update_record_flags (oss, cur);
  GST_DEBUG_OBJECT (oss, "current input route: %d (%s)", cur, cur_name);

  return (const GList *) oss->tracks;

/* ERRORS */
get_recsrc_names_error:
  {
    GST_WARNING_OBJECT (oss, "GET_RECSRC_NAMES failed: %s", g_strerror (errno));
    return NULL;
  }
}

static void
gst_oss4_source_mixer_set_volume (GstMixer * mixer, GstMixerTrack * track,
    gint * volumes)
{
  GstOss4Source *oss;
  int new_vol, cur;

  g_return_if_fail (mixer != NULL);
  g_return_if_fail (track != NULL);
  g_return_if_fail (GST_IS_MIXER_TRACK (track));
  g_return_if_fail (GST_IS_OSS4_SOURCE (mixer));
  g_return_if_fail (GST_OSS4_SOURCE_IS_OPEN (mixer));

  oss = GST_OSS4_SOURCE (mixer);

  cur = gst_oss4_source_mixer_get_current_input (oss);
  if (cur != GST_OSS4_SOURCE_INPUT (track)->route) {
    GST_DEBUG_OBJECT (oss, "track not selected input route, ignoring request");
    return;
  }

  new_vol = (volumes[1] << 8) | volumes[0];
  if (ioctl (oss->fd, SNDCTL_DSP_SETRECVOL, &new_vol) == -1) {
    GST_WARNING_OBJECT (oss, "SETRECVOL failed: %s", g_strerror (errno));
  }
}

static void
gst_oss4_source_mixer_get_volume (GstMixer * mixer, GstMixerTrack * track,
    gint * volumes)
{
  GstOss4Source *oss;
  int cur;

  g_return_if_fail (mixer != NULL);
  g_return_if_fail (GST_IS_OSS4_SOURCE (mixer));
  g_return_if_fail (GST_OSS4_SOURCE_IS_OPEN (mixer));

  oss = GST_OSS4_SOURCE (mixer);

  cur = gst_oss4_source_mixer_get_current_input (oss);
  if (cur != GST_OSS4_SOURCE_INPUT (track)->route) {
    volumes[0] = 0;
    volumes[1] = 0;
  } else {
    int vol = -1;

    if (ioctl (oss->fd, SNDCTL_DSP_GETRECVOL, &vol) == -1 || vol < 0) {
      GST_WARNING_OBJECT (oss, "GETRECVOL failed: %s", g_strerror (errno));
      volumes[0] = 100;
      volumes[1] = 100;
    } else {
      volumes[0] = MIN (100, vol & 0xff);
      volumes[1] = MIN (100, (vol >> 8) & 0xff);
    }
  }
}

static void
gst_oss4_source_mixer_set_record (GstMixer * mixer, GstMixerTrack * track,
    gboolean record)
{
  GstOss4Source *oss;
  const gchar *cur_name;
  gint cur;

  g_return_if_fail (mixer != NULL);
  g_return_if_fail (track != NULL);
  g_return_if_fail (GST_IS_MIXER_TRACK (track));
  g_return_if_fail (GST_IS_OSS4_SOURCE (mixer));
  g_return_if_fail (GST_OSS4_SOURCE_IS_OPEN (mixer));

  oss = GST_OSS4_SOURCE (mixer);

  cur = gst_oss4_source_mixer_get_current_input (oss);

  /* stop recording for an input that's not selected anyway => nothing to do */
  if (!record && cur != GST_OSS4_SOURCE_INPUT (track)->route)
    goto done;

  /* select recording for an input that's already selected => nothing to do
   * (or should we mess with the recording volume in this case maybe?) */
  if (record && cur == GST_OSS4_SOURCE_INPUT (track)->route)
    goto done;

  /* make current input stop recording: we can't really make an input stop
   * recording, we can only select an input FOR recording, so we'll just ignore
   * all requests to stop for now */
  if (!record) {
    GST_WARNING_OBJECT (oss, "Can't un-select an input as such, only switch "
        "to a different input source");
    /* FIXME: set recording volume to 0 maybe? */
  } else {
    int new_route = GST_OSS4_SOURCE_INPUT (track)->route;

    /* select this input for recording */

    if (ioctl (oss->fd, SNDCTL_DSP_SET_RECSRC, &new_route) == -1) {
      GST_WARNING_OBJECT (oss, "Could not select input %d for recording: %s",
          new_route, g_strerror (errno));
    } else {
      cur = new_route;
    }
  }

done:

  cur_name = gst_oss4_source_mixer_update_record_flags (oss, cur);
  GST_DEBUG_OBJECT (oss, "active input route: %d (%s)", cur, cur_name);
}

static void
gst_oss4_source_mixer_set_mute (GstMixer * mixer, GstMixerTrack * track,
    gboolean mute)
{
  g_return_if_fail (mixer != NULL);
  g_return_if_fail (track != NULL);
  g_return_if_fail (GST_IS_MIXER_TRACK (track));
  g_return_if_fail (GST_IS_OSS4_SOURCE (mixer));
  g_return_if_fail (GST_OSS4_SOURCE_IS_OPEN (mixer));

  /* FIXME: implement gst_oss4_source_mixer_set_mute() - what to do here? */
  /* oss4_mixer_set_mute (mixer->mixer, track, mute); */
}

static void
gst_oss4_source_mixer_interface_init (GstMixerClass * klass)
{
  GST_MIXER_TYPE (klass) = GST_MIXER_HARDWARE;

  klass->list_tracks = gst_oss4_source_mixer_list_tracks;
  klass->set_volume = gst_oss4_source_mixer_set_volume;
  klass->get_volume = gst_oss4_source_mixer_get_volume;
  klass->set_mute = gst_oss4_source_mixer_set_mute;
  klass->set_record = gst_oss4_source_mixer_set_record;
}

/* Implement the horror that is GstImplementsInterface */

static gboolean
gst_oss4_source_mixer_supported (GstImplementsInterface * iface,
    GType iface_type)
{
  GstOss4Source *oss;
  gboolean is_open;

  g_return_val_if_fail (GST_IS_OSS4_SOURCE (iface), FALSE);
  g_return_val_if_fail (iface_type == GST_TYPE_MIXER, FALSE);

  oss = GST_OSS4_SOURCE (iface);

  GST_OBJECT_LOCK (oss);
  is_open = GST_OSS4_SOURCE_IS_OPEN (iface);
  GST_OBJECT_UNLOCK (oss);

  return is_open;
}

static void
gst_oss4_source_mixer_implements_interface_init (GstImplementsInterfaceClass *
    klass)
{
  klass->supported = gst_oss4_source_mixer_supported;
}

static void
gst_oss4_source_init_interfaces (GType type)
{
  static const GInterfaceInfo implements_iface_info = {
    (GInterfaceInitFunc) gst_oss4_source_mixer_implements_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo mixer_iface_info = {
    (GInterfaceInitFunc) gst_oss4_source_mixer_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);
  g_type_add_interface_static (type, GST_TYPE_MIXER, &mixer_iface_info);

  gst_oss4_add_property_probe_interface (type);
}
