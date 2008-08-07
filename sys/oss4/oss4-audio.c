/* GStreamer OSS4 audio plugin
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "gst/gst-i18n-plugin.h"
#include <gst/audio/multichannel.h>

#include "oss4-audio.h"
#include "oss4-mixer.h"
#include "oss4-property-probe.h"
#include "oss4-sink.h"
#include "oss4-source.h"
#include "oss4-soundcard.h"

GST_DEBUG_CATEGORY (oss4mixer_debug);
GST_DEBUG_CATEGORY (oss4sink_debug);
GST_DEBUG_CATEGORY (oss4src_debug);
GST_DEBUG_CATEGORY (oss4_debug);

#define GST_CAT_DEFAULT oss4_debug

static const struct
{
  const GstBufferFormat gst_fmt;
  const gint oss_fmt;
  const gchar name[16];
  const gint depth;
  const gint width;
  const gint endianness;
  const gboolean signedness;
} fmt_map[] = {
  /* note: keep sorted by preference, prefered formats first */
  {
  GST_MU_LAW, AFMT_MU_LAW, "audio/x-mulaw", 0, 0, 0, FALSE}, {
  GST_A_LAW, AFMT_A_LAW, "audio/x-alaw", 0, 0, 0, FALSE}, {
  GST_S32_LE, AFMT_S32_LE, "audio/x-raw-int", 32, 32, G_LITTLE_ENDIAN, TRUE}, {
  GST_S32_BE, AFMT_S32_BE, "audio/x-raw-int", 32, 32, G_BIG_ENDIAN, TRUE}, {
  GST_S24_LE, AFMT_S24_LE, "audio/x-raw-int", 24, 32, G_LITTLE_ENDIAN, TRUE}, {
  GST_S24_BE, AFMT_S24_BE, "audio/x-raw-int", 24, 32, G_BIG_ENDIAN, TRUE}, {
  GST_S24_3LE, AFMT_S24_PACKED, "audio/x-raw-int", 24, 24, G_LITTLE_ENDIAN,
        TRUE}, {
  GST_S16_LE, AFMT_S16_LE, "audio/x-raw-int", 16, 16, G_LITTLE_ENDIAN, TRUE}, {
  GST_S16_BE, AFMT_S16_BE, "audio/x-raw-int", 16, 16, G_BIG_ENDIAN, TRUE}, {
  GST_U16_LE, AFMT_U16_LE, "audio/x-raw-int", 16, 16, G_LITTLE_ENDIAN, FALSE}, {
  GST_U16_BE, AFMT_U16_BE, "audio/x-raw-int", 16, 16, G_BIG_ENDIAN, FALSE}, {
  GST_S8, AFMT_S8, "audio/x-raw-int", 8, 8, 0, TRUE}, {
  GST_U8, AFMT_U8, "audio/x-raw-int", 8, 8, 0, FALSE}
};

static gboolean
gst_oss4_append_format_to_caps (gint fmt, GstCaps * caps)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (fmt_map); ++i) {
    if (fmt_map[i].oss_fmt == fmt) {
      GstStructure *s;

      s = gst_structure_empty_new (fmt_map[i].name);
      if (fmt_map[i].width != 0 && fmt_map[i].depth != 0) {
        gst_structure_set (s, "width", G_TYPE_INT, fmt_map[i].width,
            "depth", G_TYPE_INT, fmt_map[i].depth, "endianness", G_TYPE_INT,
            fmt_map[i].endianness, "signed", G_TYPE_BOOLEAN,
            fmt_map[i].signedness, NULL);
      }
      gst_caps_append_structure (caps, s);
      return TRUE;
    }
  }
  return FALSE;
}

static gint
gst_oss4_audio_get_oss_format (GstBufferFormat fmt)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (fmt_map); ++i) {
    if (fmt_map[i].gst_fmt == fmt)
      return fmt_map[i].oss_fmt;
  }
  return 0;
}

/* These are pretty random */
#define GST_OSS4_MIN_SAMPLE_RATE 1
#define GST_OSS4_MAX_SAMPLE_RATE 192000

static gboolean
gst_oss4_audio_detect_rates (GstObject * obj, oss_audioinfo * ai,
    GstCaps * caps)
{
  GValue val = { 0, };
  int minrate, maxrate, i;

  minrate = ai->min_rate;
  maxrate = ai->max_rate;

  /* sanity check */
  if (minrate > maxrate) {
    GST_WARNING_OBJECT (obj, "min_rate %d > max_rate %d (buggy driver?)",
        minrate, maxrate);
    maxrate = ai->min_rate;     /* swap */
    minrate = ai->max_rate;
  }

  /* limit to something sensible */
  if (minrate < GST_OSS4_MIN_SAMPLE_RATE)
    minrate = GST_OSS4_MIN_SAMPLE_RATE;
  if (maxrate > GST_OSS4_MAX_SAMPLE_RATE)
    maxrate = GST_OSS4_MAX_SAMPLE_RATE;

  if (maxrate < GST_OSS4_MIN_SAMPLE_RATE) {
    GST_WARNING_OBJECT (obj, "max_rate < %d, which makes no sense",
        GST_OSS4_MIN_SAMPLE_RATE);
    return FALSE;
  }

  GST_LOG_OBJECT (obj, "min_rate %d, max_rate %d (originally: %d, %d)",
      minrate, maxrate, ai->min_rate, ai->max_rate);

  if ((ai->caps & PCM_CAP_FREERATE)) {
    GST_LOG_OBJECT (obj, "device supports any sample rate between min and max");
    if (minrate == maxrate) {
      g_value_init (&val, G_TYPE_INT);
      g_value_set_int (&val, maxrate);
    } else {
      g_value_init (&val, GST_TYPE_INT_RANGE);
      gst_value_set_int_range (&val, minrate, maxrate);
    }
  } else {
    GST_LOG_OBJECT (obj, "%d sample rates:", ai->nrates);
    g_value_init (&val, GST_TYPE_LIST);
    for (i = 0; i < ai->nrates; ++i) {
      GST_LOG_OBJECT (obj, " rate: %d", ai->rates[i]);

      if (ai->rates[i] >= minrate && ai->rates[i] <= maxrate) {
        GValue rate_val = { 0, };

        g_value_init (&rate_val, G_TYPE_INT);
        g_value_set_int (&rate_val, ai->rates[i]);
        gst_value_list_append_value (&val, &rate_val);
        g_value_unset (&rate_val);
      }
    }

    if (gst_value_list_get_size (&val) == 0) {
      g_value_unset (&val);
      return FALSE;
    }
  }

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    GstStructure *s;

    s = gst_caps_get_structure (caps, i);
    gst_structure_set_value (s, "rate", &val);
  }

  g_value_unset (&val);

  return TRUE;
}

static void
gst_oss4_audio_add_channel_layout (GstObject * obj, guint64 layout,
    guint num_channels, GstStructure * s)
{
  const GstAudioChannelPosition pos_map[16] = {
    GST_AUDIO_CHANNEL_POSITION_NONE,    /* 0 = dunno          */
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,      /* 1 = left           */
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,     /* 2 = right          */
    GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,    /* 3 = center         */
    GST_AUDIO_CHANNEL_POSITION_LFE,     /* 4 = lfe            */
    GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,       /* 5 = left surround  */
    GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,      /* 6 = right surround */
    GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,       /* 7 = left rear      */
    GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,      /* 8 = right rear     */
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE
  };
  GstAudioChannelPosition ch_layout[8] = { 0, };
  guint speaker_pos;            /* speaker position as defined by OSS */
  guint i;

  g_return_if_fail (num_channels <= G_N_ELEMENTS (ch_layout));

  for (i = 0; i < num_channels; ++i) {
    /* layout contains up to 16 speaker positions, with each taking up 4 bits */
    speaker_pos = (guint) ((layout >> (i * 4)) & 0x0f);

    /* if it's a channel position that's unknown to us, set all to NONE and
     * bail out */
    if (G_UNLIKELY (pos_map[speaker_pos] == GST_AUDIO_CHANNEL_POSITION_NONE))
      goto no_layout;

    ch_layout[i] = pos_map[speaker_pos];
  }
  gst_audio_set_channel_positions (s, ch_layout);
  return;

no_layout:
  {
    /* only warn if it's really unknown, position 0 is ok and represents NONE
     * (in which case we also just set all others to NONE ignoring the other
     * positions in the OSS-given layout, because that's what we currently
     * require in GStreamer) */
    if (speaker_pos != 0) {
      GST_WARNING_OBJECT (obj, "unknown OSS channel position %x", ch_layout[i]);
    }
    for (i = 0; i < num_channels; ++i) {
      ch_layout[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
    }
    gst_audio_set_channel_positions (s, ch_layout);
    return;
  }
}

/* arbitrary max. limit */
#define GST_OSS4_MIN_CHANNELS 1
#define GST_OSS4_MAX_CHANNELS 4096

/* takes ownership of the input caps */
static GstCaps *
gst_oss4_audio_detect_channels (GstObject * obj, int fd, oss_audioinfo * ai,
    GstCaps * in_caps)
{
  const gchar *forced_layout;
  GstStructure *s = NULL;
  guint64 layout = 0;
  GstCaps *chan_caps = NULL;
  GstCaps *out_caps = NULL;
  int minchans, maxchans;
  int c, i, j;

  /* GST_OSS4_CHANNEL_LAYOUT environment variable: may be used to force a
   * particular channel layout (if it contains an odd number of channel
   * positions it will also make us advertise a channel layout for that
   * channel count, even if we'd usually skip it; this is especially useful
   * for folks with 2.1 speakers, I guess) */
  forced_layout = g_getenv ("GST_OSS4_CHANNEL_LAYOUT");

  minchans = ai->min_channels;
  maxchans = ai->max_channels;

  /* sanity check */
  if (minchans > maxchans) {
    GST_WARNING_OBJECT (obj, "min_chans %d > max_chans %d (buggy driver?)",
        minchans, maxchans);
    maxchans = ai->min_channels;        /* swap */
    minchans = ai->max_channels;
  }

  /* limit to something sensible */
  if (minchans < GST_OSS4_MIN_CHANNELS)
    minchans = GST_OSS4_MIN_CHANNELS;
  if (maxchans > GST_OSS4_MAX_CHANNELS)
    maxchans = GST_OSS4_MAX_CHANNELS;

  if (maxchans < GST_OSS4_MIN_CHANNELS) {
    GST_WARNING_OBJECT (obj, "max_chans < %d, which makes no sense",
        GST_OSS4_MIN_CHANNELS);
    gst_caps_unref (in_caps);
    return NULL;
  }

  GST_LOG_OBJECT (obj, "min_channels %d, max_channels %d (originally: %d, %d)",
      minchans, maxchans, ai->min_channels, ai->max_channels);

  chan_caps = gst_caps_new_empty ();

  /* first do the simple cases: mono + stereo (channel layout implied) */
  if (minchans == 1 && maxchans == 1)
    s = gst_structure_new ("x", "channels", G_TYPE_INT, 1, NULL);
  else if (minchans == 2 && maxchans >= 2)
    s = gst_structure_new ("x", "channels", G_TYPE_INT, 2, NULL);
  else if (minchans == 1 && maxchans >= 2)
    s = gst_structure_new ("x", "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
  gst_caps_append_structure (chan_caps, s);
  s = NULL;

  /* TODO: we assume all drivers use a left/right layout for stereo here */
  if (maxchans <= 2)
    goto done;

  if (ioctl (fd, SNDCTL_DSP_GET_CHNORDER, &layout) == -1) {
    GST_WARNING_OBJECT (obj, "couldn't query channel layout, assuming default");
    layout = CHNORDER_NORMAL;
  }
  GST_DEBUG_OBJECT (obj, "channel layout: %08" G_GINT64_MODIFIER "x", layout);

  /* e.g. forced 2.1 layout would be GST_OSS4_CHANNEL_LAYOUT=421 */
  if (forced_layout != NULL && *forced_layout != '\0') {
    guint layout_len;

    layout_len = strlen (forced_layout);
    if (layout_len >= minchans && layout_len <= maxchans) {
      layout = g_ascii_strtoull (forced_layout, NULL, 16);
      maxchans = layout_len;
      GST_DEBUG_OBJECT (obj, "forced channel layout: %08" G_GINT64_MODIFIER "x"
          " ('%s'), maxchans now %d", layout, forced_layout, maxchans);
    } else {
      GST_WARNING_OBJECT (obj, "ignoring forced channel layout: layout has %d "
          "channel positions but maxchans is %d", layout_len, maxchans);
    }
  }

  /* need to advertise channel layouts for anything >2 and <=8 channels */
  for (c = MAX (3, minchans); c <= MIN (maxchans, 8); c++) {
    /* "The min_channels and max_channels fields define the limits for the
     * number of channels. However some devices don't support all channels
     * within this range. It's possible that the odd values (3, 5, 7, 9, etc).
     * are not supported. There is currently no way to check for this other
     * than checking if SNDCTL_DSP_CHANNELS accepts the requested value.
     * Another approach is trying to avoid using odd number of channels."
     *
     * So, we don't know for sure if these odd values are supported:
     */
    if ((c == 3 || c == 5 || c == 7) && (c != maxchans)) {
      GST_LOG_OBJECT (obj, "not adding layout with %d channels", c);
      continue;
    }

    s = gst_structure_new ("x", "channels", G_TYPE_INT, c, NULL);
    gst_oss4_audio_add_channel_layout (obj, layout, c, s);
    GST_LOG_OBJECT (obj, "c=%u, appending struct %" GST_PTR_FORMAT, c, s);
    gst_caps_append_structure (chan_caps, s);
    s = NULL;
  }

  if (maxchans <= 8)
    goto done;

  /* for everything >8 channels, CHANNEL_POSITION_NONE is implied. */
  if (minchans == maxchans || maxchans == 9) {
    s = gst_structure_new ("x", "channels", G_TYPE_INT, maxchans, NULL);
  } else {
    s = gst_structure_new ("x", "channels", GST_TYPE_INT_RANGE,
        MAX (9, minchans), maxchans, NULL);
  }
  gst_caps_append_structure (chan_caps, s);
  s = NULL;

done:

  GST_LOG_OBJECT (obj, "channel structures: %" GST_PTR_FORMAT, chan_caps);

  out_caps = gst_caps_new_empty ();

  /* combine each structure in the input caps with each channel caps struct */
  for (i = 0; i < gst_caps_get_size (in_caps); ++i) {
    const GstStructure *in_s;

    in_s = gst_caps_get_structure (in_caps, i);

    for (j = 0; j < gst_caps_get_size (chan_caps); ++j) {
      const GstStructure *chan_s;
      const GValue *val;

      s = gst_structure_copy (in_s);
      chan_s = gst_caps_get_structure (chan_caps, j);
      if ((val = gst_structure_get_value (chan_s, "channels")))
        gst_structure_set_value (s, "channels", val);
      if ((val = gst_structure_get_value (chan_s, "channel-positions")))
        gst_structure_set_value (s, "channel-positions", val);

      gst_caps_append_structure (out_caps, s);
      s = NULL;
    }
  }

  gst_caps_unref (in_caps);
  gst_caps_unref (chan_caps);
  return out_caps;
}

GstCaps *
gst_oss4_audio_probe_caps (GstObject * obj, int fd)
{
  oss_audioinfo ai = { 0, };
  gboolean output;
  GstCaps *caps;
  int formats, i;

  output = GST_IS_OSS4_SINK (obj);

  /* -1 = get info for currently open device (fd). This will fail with
   * OSS build <= 1013 because of a bug in OSS */
  ai.dev = -1;
  if (ioctl (fd, SNDCTL_ENGINEINFO, &ai) == -1)
    goto engineinfo_failed;

  formats = (output) ? ai.oformats : ai.iformats;

  GST_LOG_OBJECT (obj, "%s formats : 0x%08x", (output) ? "out" : "in", formats);

  caps = gst_caps_new_empty ();

  for (i = 0; i < G_N_ELEMENTS (fmt_map); ++i) {
    if ((formats & fmt_map[i].oss_fmt)) {
      gst_oss4_append_format_to_caps (fmt_map[i].oss_fmt, caps);
    }
  }

  gst_caps_do_simplify (caps);
  GST_LOG_OBJECT (obj, "formats: %" GST_PTR_FORMAT, caps);

  if (!gst_oss4_audio_detect_rates (obj, &ai, caps))
    goto detect_rates_failed;

  caps = gst_oss4_audio_detect_channels (obj, fd, &ai, caps);
  if (caps == NULL)
    goto detect_channels_failed;

  GST_LOG_OBJECT (obj, "probed caps: %" GST_PTR_FORMAT, caps);

  return caps;

/* ERRORS */
engineinfo_failed:
  {
    GST_WARNING ("ENGINEINFO supported formats probe failed: %s",
        g_strerror (errno));
    return NULL;
  }
detect_rates_failed:
  {
    GST_WARNING_OBJECT (obj, "failed to detect supported sample rates");
    gst_caps_unref (caps);
    return NULL;
  }
detect_channels_failed:
  {
    GST_WARNING_OBJECT (obj, "failed to detect supported channels");
    gst_caps_unref (caps);
    return NULL;
  }
}

GstCaps *
gst_oss4_audio_get_template_caps (void)
{
  GstCaps *caps;
  gint i;

  caps = gst_caps_new_empty ();

  for (i = 0; i < G_N_ELEMENTS (fmt_map); ++i) {
    gst_oss4_append_format_to_caps (fmt_map[i].oss_fmt, caps);
  }

  gst_caps_do_simplify (caps);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    GstStructure *s;

    s = gst_caps_get_structure (caps, i);
    gst_structure_set (s, "rate", GST_TYPE_INT_RANGE, GST_OSS4_MIN_SAMPLE_RATE,
        GST_OSS4_MAX_SAMPLE_RATE, "channels", GST_TYPE_INT_RANGE,
        GST_OSS4_MIN_CHANNELS, GST_OSS4_MAX_CHANNELS, NULL);
  }

  return caps;
}

static gint
gst_oss4_audio_ilog2 (gint x)
{
  /* well... hacker's delight explains... */
  x = x | (x >> 1);
  x = x | (x >> 2);
  x = x | (x >> 4);
  x = x | (x >> 8);
  x = x | (x >> 16);
  x = x - ((x >> 1) & 0x55555555);
  x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
  x = (x + (x >> 4)) & 0x0f0f0f0f;
  x = x + (x >> 8);
  x = x + (x >> 16);
  return (x & 0x0000003f) - 1;
}

/* called by gst_oss4_sink_prepare() and gst_oss4_source_prepare() */
gboolean
gst_oss4_audio_set_format (GstObject * obj, int fd, GstRingBufferSpec * spec)
{
  struct audio_buf_info info = { 0, };
  int fmt, chans, rate, fragsize;

  fmt = gst_oss4_audio_get_oss_format (spec->format);
  if (fmt == 0)
    goto wrong_format;

  if (spec->type == GST_BUFTYPE_LINEAR && spec->width != 32 &&
      spec->width != 24 && spec->width != 16 && spec->width != 8) {
    goto dodgy_width;
  }

  /* format */
  GST_LOG_OBJECT (obj, "setting format: %d", fmt);
  if (ioctl (fd, SNDCTL_DSP_SETFMT, &fmt) == -1)
    goto set_format_failed;

  /* channels */
  GST_LOG_OBJECT (obj, "setting channels: %d", spec->channels);
  chans = spec->channels;
  if (ioctl (fd, SNDCTL_DSP_CHANNELS, &chans) == -1)
    goto set_channels_failed;

  /* rate */
  GST_LOG_OBJECT (obj, "setting rate: %d", spec->rate);
  rate = spec->rate;
  if (ioctl (fd, SNDCTL_DSP_SPEED, &rate) == -1)
    goto set_rate_failed;

  GST_DEBUG_OBJECT (obj, "effective format   : %d", fmt);
  GST_DEBUG_OBJECT (obj, "effective channels : %d", chans);
  GST_DEBUG_OBJECT (obj, "effective rate     : %d", rate);

  /* make sure format, channels, and rate are the ones we requested */
  if (fmt != gst_oss4_audio_get_oss_format (spec->format) ||
      chans != spec->channels || rate != spec->rate) {
    /* This shouldn't happen, but hey */
    goto format_not_what_was_requested;
  }

  /* CHECKME: maybe we should just leave the fragsize alone? (tpm) */
  fragsize = gst_oss4_audio_ilog2 (spec->segsize);
  fragsize = ((spec->segtotal & 0x7fff) << 16) | fragsize;
  GST_DEBUG_OBJECT (obj, "setting segsize: %d, segtotal: %d, value: %08x",
      spec->segsize, spec->segtotal, fragsize);

  /* we could also use the new SNDCTL_DSP_POLICY if there's something in
   * particular we're trying to achieve here */
  if (ioctl (fd, SNDCTL_DSP_SETFRAGMENT, &fragsize) == -1)
    goto set_fragsize_failed;

  if (GST_IS_OSS4_SOURCE (obj)) {
    if (ioctl (fd, SNDCTL_DSP_GETISPACE, &info) == -1)
      goto get_ispace_failed;
  } else {
    if (ioctl (fd, SNDCTL_DSP_GETOSPACE, &info) == -1)
      goto get_ospace_failed;
  }

  spec->segsize = info.fragsize;
  spec->segtotal = info.fragstotal;

  spec->bytes_per_sample = (spec->width / 8) * spec->channels;

  GST_DEBUG_OBJECT (obj, "got segsize: %d, segtotal: %d, value: %08x",
      spec->segsize, spec->segtotal, fragsize);

  return TRUE;

/* ERRORS */
wrong_format:
  {
    GST_ELEMENT_ERROR (obj, RESOURCE, SETTINGS, (NULL),
        ("Unable to get format %d", spec->format));
    return FALSE;
  }
dodgy_width:
  {
    GST_ELEMENT_ERROR (obj, RESOURCE, SETTINGS, (NULL),
        ("unexpected width %d", spec->width));
    return FALSE;
  }
set_format_failed:
  {
    GST_ELEMENT_ERROR (obj, RESOURCE, SETTINGS, (NULL),
        ("DSP_SETFMT(%d) failed: %s", fmt, g_strerror (errno)));
    return FALSE;
  }
set_channels_failed:
  {
    GST_ELEMENT_ERROR (obj, RESOURCE, SETTINGS, (NULL),
        ("DSP_CHANNELS(%d) failed: %s", chans, g_strerror (errno)));
    return FALSE;
  }
set_rate_failed:
  {
    GST_ELEMENT_ERROR (obj, RESOURCE, SETTINGS, (NULL),
        ("DSP_SPEED(%d) failed: %s", rate, g_strerror (errno)));
    return FALSE;
  }
set_fragsize_failed:
  {
    GST_ELEMENT_ERROR (obj, RESOURCE, SETTINGS, (NULL),
        ("DSP_SETFRAGMENT(%d) failed: %s", fragsize, g_strerror (errno)));
    return FALSE;
  }
get_ospace_failed:
  {
    GST_ELEMENT_ERROR (obj, RESOURCE, SETTINGS, (NULL),
        ("DSP_GETOSPACE failed: %s", g_strerror (errno)));
    return FALSE;
  }
get_ispace_failed:
  {
    GST_ELEMENT_ERROR (obj, RESOURCE, SETTINGS, (NULL),
        ("DSP_GETISPACE failed: %s", g_strerror (errno)));
    return FALSE;
  }
format_not_what_was_requested:
  {
    GST_ELEMENT_ERROR (obj, RESOURCE, SETTINGS, (NULL),
        ("Format actually configured wasn't the one we requested. This is "
            "probably either a bug in the driver or in the format probing code."));
    return FALSE;
  }
}

int
gst_oss4_audio_get_version (GstObject * obj, int fd)
{
  gint ver = 0;

  /* we use the old ioctl here on purpose instead of SNDCTL_SYSINFO */
  if (ioctl (fd, OSS_GETVERSION, &ver) < 0) {
    GST_LOG_OBJECT (obj, "OSS_GETVERSION failed: %s", g_strerror (errno));
    return -1;
  }
  GST_LOG_OBJECT (obj, "OSS version: 0x%08x", ver);
  return ver;
}

gboolean
gst_oss4_audio_check_version (GstObject * obj, int fd)
{
  return (gst_oss4_audio_get_version (obj, fd) >= GST_MIN_OSS4_VERSION);
}

gchar *
gst_oss4_audio_find_device (GstObject * oss)
{
  GValueArray *arr;
  gchar *ret = NULL;

  arr = gst_property_probe_probe_and_get_values_name (GST_PROPERTY_PROBE (oss),
      "device");

  if (arr != NULL) {
    if (arr->n_values > 0) {
      const GValue *val;

      val = g_value_array_get_nth (arr, 0);
      ret = g_value_dup_string (val);
    }
    g_value_array_free (arr);
  }

  GST_LOG_OBJECT (oss, "first device found: %s", GST_STR_NULL (ret));

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gint rank;

  GST_DEBUG_CATEGORY_INIT (oss4sink_debug, "oss4sink", 0, "OSS4 audio sink");
  GST_DEBUG_CATEGORY_INIT (oss4src_debug, "oss4src", 0, "OSS4 audio src");
  GST_DEBUG_CATEGORY_INIT (oss4mixer_debug, "oss4mixer", 0, "OSS4 mixer");
  GST_DEBUG_CATEGORY_INIT (oss4_debug, "oss4", 0, "OSS4 plugin");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

  /* we want a higher rank than the legacy OSS elements have now */
  rank = GST_RANK_SECONDARY + 1;

  if (!gst_element_register (plugin, "oss4sink", rank, GST_TYPE_OSS4_SINK) ||
      !gst_element_register (plugin, "oss4src", rank, GST_TYPE_OSS4_SOURCE) ||
      !gst_element_register (plugin, "oss4mixer", rank, GST_TYPE_OSS4_MIXER)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "oss4",
    "Open Sound System (OSS) version 4 support for GStreamer",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
