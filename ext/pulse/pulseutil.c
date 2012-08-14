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

#ifdef HAVE_UNISTD_H
# include <unistd.h>            /* getpid on UNIX */
#endif
#ifdef HAVE_PROCESS_H
# include <process.h>           /* getpid on win32 */
#endif

static const struct
{
  GstAudioChannelPosition gst_pos;
  pa_channel_position_t pa_pos;
} gst_pa_pos_table[] = {
  {
  GST_AUDIO_CHANNEL_POSITION_MONO, PA_CHANNEL_POSITION_MONO}, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_LEFT}, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT, PA_CHANNEL_POSITION_FRONT_RIGHT}, {
  GST_AUDIO_CHANNEL_POSITION_REAR_CENTER, PA_CHANNEL_POSITION_REAR_CENTER}, {
  GST_AUDIO_CHANNEL_POSITION_REAR_LEFT, PA_CHANNEL_POSITION_REAR_LEFT}, {
  GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT, PA_CHANNEL_POSITION_REAR_RIGHT}, {
  GST_AUDIO_CHANNEL_POSITION_LFE1, PA_CHANNEL_POSITION_LFE}, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER, PA_CHANNEL_POSITION_FRONT_CENTER}, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
        PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER}, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
        PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER}, {
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT, PA_CHANNEL_POSITION_SIDE_LEFT}, {
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT, PA_CHANNEL_POSITION_SIDE_RIGHT}, {
  GST_AUDIO_CHANNEL_POSITION_TOP_CENTER, PA_CHANNEL_POSITION_TOP_CENTER}, {
  GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
        PA_CHANNEL_POSITION_TOP_FRONT_LEFT}, {
  GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
        PA_CHANNEL_POSITION_TOP_FRONT_RIGHT}, {
  GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER,
        PA_CHANNEL_POSITION_TOP_FRONT_CENTER}, {
  GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT, PA_CHANNEL_POSITION_TOP_REAR_LEFT}, {
  GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT,
        PA_CHANNEL_POSITION_TOP_REAR_RIGHT}, {
  GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER,
        PA_CHANNEL_POSITION_TOP_REAR_CENTER}, {
  GST_AUDIO_CHANNEL_POSITION_NONE, PA_CHANNEL_POSITION_INVALID}
};

static gboolean
gstaudioformat_to_pasampleformat (GstAudioFormat format,
    pa_sample_format_t * sf)
{
  switch (format) {
    case GST_AUDIO_FORMAT_U8:
      *sf = PA_SAMPLE_U8;
      break;
    case GST_AUDIO_FORMAT_S16LE:
      *sf = PA_SAMPLE_S16LE;
      break;
    case GST_AUDIO_FORMAT_S16BE:
      *sf = PA_SAMPLE_S16BE;
      break;
    case GST_AUDIO_FORMAT_F32LE:
      *sf = PA_SAMPLE_FLOAT32LE;
      break;
    case GST_AUDIO_FORMAT_F32BE:
      *sf = PA_SAMPLE_FLOAT32BE;
      break;
    case GST_AUDIO_FORMAT_S32LE:
      *sf = PA_SAMPLE_S32LE;
      break;
    case GST_AUDIO_FORMAT_S32BE:
      *sf = PA_SAMPLE_S32BE;
      break;
    case GST_AUDIO_FORMAT_S24LE:
      *sf = PA_SAMPLE_S24LE;
      break;
    case GST_AUDIO_FORMAT_S24BE:
      *sf = PA_SAMPLE_S24BE;
      break;
    case GST_AUDIO_FORMAT_S24_32LE:
      *sf = PA_SAMPLE_S24_32LE;
      break;
    case GST_AUDIO_FORMAT_S24_32BE:
      *sf = PA_SAMPLE_S24_32BE;
      break;
    default:
      return FALSE;
  }
  return TRUE;
}

gboolean
gst_pulse_fill_sample_spec (GstAudioRingBufferSpec * spec, pa_sample_spec * ss)
{
  if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_RAW) {
    if (!gstaudioformat_to_pasampleformat (GST_AUDIO_INFO_FORMAT (&spec->info),
            &ss->format))
      return FALSE;
  } else if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_MU_LAW) {
    ss->format = PA_SAMPLE_ULAW;
  } else if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_A_LAW) {
    ss->format = PA_SAMPLE_ALAW;
  } else
    return FALSE;

  ss->channels = GST_AUDIO_INFO_CHANNELS (&spec->info);
  ss->rate = GST_AUDIO_INFO_RATE (&spec->info);

  if (!pa_sample_spec_valid (ss))
    return FALSE;

  return TRUE;
}

gboolean
gst_pulse_fill_format_info (GstAudioRingBufferSpec * spec, pa_format_info ** f,
    guint * channels)
{
  pa_format_info *format;
  pa_sample_format_t sf = PA_SAMPLE_INVALID;
  GstAudioInfo *ainfo = &spec->info;

  format = pa_format_info_new ();

  if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_MU_LAW
      && GST_AUDIO_INFO_WIDTH (ainfo) == 8) {
    format->encoding = PA_ENCODING_PCM;
    sf = PA_SAMPLE_ULAW;
  } else if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_A_LAW
      && GST_AUDIO_INFO_WIDTH (ainfo) == 8) {
    format->encoding = PA_ENCODING_PCM;
    sf = PA_SAMPLE_ALAW;
  } else if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_RAW) {
    format->encoding = PA_ENCODING_PCM;
    if (!gstaudioformat_to_pasampleformat (GST_AUDIO_INFO_FORMAT (ainfo), &sf))
      goto fail;
  } else if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_AC3) {
    format->encoding = PA_ENCODING_AC3_IEC61937;
  } else if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_EAC3) {
    format->encoding = PA_ENCODING_EAC3_IEC61937;
  } else if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_DTS) {
    format->encoding = PA_ENCODING_DTS_IEC61937;
  } else if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_MPEG) {
    format->encoding = PA_ENCODING_MPEG_IEC61937;
  } else {
    goto fail;
  }

  if (format->encoding == PA_ENCODING_PCM) {
    pa_format_info_set_sample_format (format, sf);
    pa_format_info_set_channels (format, GST_AUDIO_INFO_CHANNELS (ainfo));
  }

  pa_format_info_set_rate (format, GST_AUDIO_INFO_RATE (ainfo));

  if (!pa_format_info_valid (format))
    goto fail;

  *f = format;
  *channels = GST_AUDIO_INFO_CHANNELS (ainfo);

  return TRUE;

fail:
  if (format)
    pa_format_info_free (format);
  return FALSE;
}

/* PATH_MAX is not defined everywhere, e.g. on GNU Hurd */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

gchar *
gst_pulse_client_name (void)
{
  gchar buf[PATH_MAX];

  const char *c;

  if ((c = g_get_application_name ()))
    return g_strdup (c);
  else if (pa_get_binary_name (buf, sizeof (buf)))
    return g_strdup (buf);
  else
    return g_strdup_printf ("GStreamer-pid-%lu", (gulong) getpid ());
}

pa_channel_map *
gst_pulse_gst_to_channel_map (pa_channel_map * map,
    const GstAudioRingBufferSpec * spec)
{
  gint i, j;
  gint channels;
  const GstAudioChannelPosition *pos;

  pa_channel_map_init (map);

  channels = GST_AUDIO_INFO_CHANNELS (&spec->info);
  pos = spec->info.position;

  for (j = 0; j < channels; j++) {
    for (i = 0; i < G_N_ELEMENTS (gst_pa_pos_table); i++) {
      if (pos[j] == gst_pa_pos_table[i].gst_pos) {
        map->map[j] = gst_pa_pos_table[i].pa_pos;
        break;
      }
    }
    if (i == G_N_ELEMENTS (gst_pa_pos_table))
      return NULL;
  }

  if (j != spec->info.channels) {
    return NULL;
  }

  map->channels = spec->info.channels;

  if (!pa_channel_map_valid (map)) {
    return NULL;
  }

  return map;
}

GstAudioRingBufferSpec *
gst_pulse_channel_map_to_gst (const pa_channel_map * map,
    GstAudioRingBufferSpec * spec)
{
  gint i, j;
  gboolean invalid = FALSE;
  gint channels;
  GstAudioChannelPosition *pos;

  channels = GST_AUDIO_INFO_CHANNELS (&spec->info);

  g_return_val_if_fail (map->channels == channels, NULL);

  pos = spec->info.position;

  for (j = 0; j < channels; j++) {
    for (i = 0; j < channels && i < G_N_ELEMENTS (gst_pa_pos_table); i++) {
      if (map->map[j] == gst_pa_pos_table[i].pa_pos) {
        pos[j] = gst_pa_pos_table[i].gst_pos;
        break;
      }
    }
    if (i == G_N_ELEMENTS (gst_pa_pos_table))
      return NULL;
  }

  if (!invalid
      && !gst_audio_check_valid_channel_positions (pos, channels, FALSE))
    invalid = TRUE;

  if (invalid) {
    for (i = 0; i < channels; i++)
      pos[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
  } else {
    if (pos[0] != GST_AUDIO_CHANNEL_POSITION_NONE)
      spec->info.flags &= ~GST_AUDIO_FLAG_UNPOSITIONED;
  }

  return spec;
}

void
gst_pulse_cvolume_from_linear (pa_cvolume * v, unsigned channels,
    gdouble volume)
{
  pa_cvolume_set (v, channels, pa_sw_volume_from_linear (volume));
}

static gboolean
make_proplist_item (GQuark field_id, const GValue * value, gpointer user_data)
{
  pa_proplist *p = (pa_proplist *) user_data;
  gchar *prop_id = (gchar *) g_quark_to_string (field_id);

  /* http://0pointer.de/lennart/projects/pulseaudio/doxygen/proplist_8h.html */

  /* match prop id */

  /* check type */
  switch (G_VALUE_TYPE (value)) {
    case G_TYPE_STRING:
      pa_proplist_sets (p, prop_id, g_value_get_string (value));
      break;
    default:
      GST_WARNING ("unmapped property type %s", G_VALUE_TYPE_NAME (value));
      break;
  }

  return TRUE;
}

pa_proplist *
gst_pulse_make_proplist (const GstStructure * properties)
{
  pa_proplist *proplist = pa_proplist_new ();

  /* iterate the structure and fill the proplist */
  gst_structure_foreach (properties, make_proplist_item, proplist);
  return proplist;
}
