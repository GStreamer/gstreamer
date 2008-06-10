/*
 *  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2004-2008 Lennart Poettering
 *
 *  gst-pulse is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  gst-pulse is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with gst-pulse; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pulseutil.h"
#include <gst/audio/multichannel.h>

static const pa_channel_position_t gst_pos_to_pa[GST_AUDIO_CHANNEL_POSITION_NUM]
    = {
  [GST_AUDIO_CHANNEL_POSITION_FRONT_MONO] = PA_CHANNEL_POSITION_MONO,
  [GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT] = PA_CHANNEL_POSITION_FRONT_LEFT,
  [GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT] = PA_CHANNEL_POSITION_FRONT_RIGHT,
  [GST_AUDIO_CHANNEL_POSITION_REAR_CENTER] = PA_CHANNEL_POSITION_REAR_CENTER,
  [GST_AUDIO_CHANNEL_POSITION_REAR_LEFT] = PA_CHANNEL_POSITION_REAR_LEFT,
  [GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT] = PA_CHANNEL_POSITION_REAR_RIGHT,
  [GST_AUDIO_CHANNEL_POSITION_LFE] = PA_CHANNEL_POSITION_LFE,
  [GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER] = PA_CHANNEL_POSITION_FRONT_CENTER,
  [GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER] =
      PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
  [GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER] =
      PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
  [GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT] = PA_CHANNEL_POSITION_SIDE_LEFT,
  [GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT] = PA_CHANNEL_POSITION_SIDE_RIGHT,
  [GST_AUDIO_CHANNEL_POSITION_NONE] = PA_CHANNEL_POSITION_INVALID
};

gboolean
gst_pulse_fill_sample_spec (GstRingBufferSpec * spec, pa_sample_spec * ss)
{

  if (spec->format == GST_MU_LAW && spec->width == 8)
    ss->format = PA_SAMPLE_ULAW;
  else if (spec->format == GST_A_LAW && spec->width == 8)
    ss->format = PA_SAMPLE_ALAW;
  else if (spec->format == GST_U8 && spec->width == 8)
    ss->format = PA_SAMPLE_U8;
  else if (spec->format == GST_S16_LE && spec->width == 16)
    ss->format = PA_SAMPLE_S16LE;
  else if (spec->format == GST_S16_BE && spec->width == 16)
    ss->format = PA_SAMPLE_S16BE;
  else if (spec->format == GST_FLOAT32_LE && spec->width == 32)
    ss->format = PA_SAMPLE_FLOAT32LE;
  else if (spec->format == GST_FLOAT32_BE && spec->width == 32)
    ss->format = PA_SAMPLE_FLOAT32BE;
  else if (spec->format == GST_S32_LE && spec->width == 32)
    ss->format = PA_SAMPLE_S32LE;
  else if (spec->format == GST_S32_BE && spec->width == 32)
    ss->format = PA_SAMPLE_S32BE;
  else
    return FALSE;

  ss->channels = spec->channels;
  ss->rate = spec->rate;

  if (!pa_sample_spec_valid (ss))
    return FALSE;

  return TRUE;
}

gchar *
gst_pulse_client_name (void)
{
  gchar buf[PATH_MAX];

  const char *c;

  if ((c = g_get_application_name ()))
    return g_strdup_printf ("%s", c);
  else if (pa_get_binary_name (buf, sizeof (buf)))
    return g_strdup_printf ("%s", buf);
  else
    return g_strdup ("GStreamer");
}

pa_channel_map *
gst_pulse_gst_to_channel_map (pa_channel_map * map, GstRingBufferSpec * spec)
{
  int i;

  GstAudioChannelPosition *pos;

  pa_channel_map_init (map);

  if (!(pos =
          gst_audio_get_channel_positions (gst_caps_get_structure (spec->caps,
                  0)))) {
/*         g_debug("%s: No channel positions!\n", G_STRFUNC); */
    return NULL;
  }

/*     g_debug("%s: Got channel positions:\n", G_STRFUNC); */

  for (i = 0; i < spec->channels; i++) {

    if (pos[i] == GST_AUDIO_CHANNEL_POSITION_NONE) {
      /* no valid mappings for these channels */
      g_free (pos);
      return NULL;
    } else if (pos[i] < GST_AUDIO_CHANNEL_POSITION_NUM)
      map->map[i] = gst_pos_to_pa[pos[i]];
    else
      map->map[i] = PA_CHANNEL_POSITION_INVALID;

    /*g_debug("  channel %d: gst: %d pulse: %d\n", i, pos[i], map->map[i]); */
  }

  g_free (pos);
  map->channels = spec->channels;

  if (!pa_channel_map_valid (map)) {
/*         g_debug("generated invalid map!\n"); */
    return NULL;
  }

  return map;
}
