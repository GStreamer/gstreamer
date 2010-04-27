/* GStreamer Jack utility functions
 * Copyright (C) 2010 Tristan Matthews <tristan@sat.qc.ca>
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

#include "gstjackutil.h"
#include <gst/audio/multichannel.h>

static const GstAudioChannelPosition default_positions[8][8] = {
  /* 1 channel */
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_MONO,
      },
  /* 2 channels */
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      },
  /* 3 channels (2.1) */
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_LFE, /* or FRONT_CENTER for 3.0? */
      },
  /* 4 channels (4.0 or 3.1?) */
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
      },
  /* 5 channels */
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      },
  /* 6 channels */
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE,
      },
  /* 7 channels */
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE,
        GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
      },
  /* 8 channels */
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE,
        GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
        GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
      }
};


/* if channels are less than or equal to 8, we set a default layout,
 * otherwise set layout to an array of GST_AUDIO_CHANNEL_POSITION_NONE */
void
gst_jack_set_layout_on_caps (GstCaps ** caps, gint channels)
{
  int c;
  GValue pos = { 0 };
  GValue chanpos = { 0 };
  gst_caps_unref (*caps);

  if (channels <= 8) {
    g_assert (channels >= 1);
    gst_audio_set_channel_positions (gst_caps_get_structure (*caps, 0),
        default_positions[channels - 1]);
  } else {
    g_value_init (&chanpos, GST_TYPE_ARRAY);
    g_value_init (&pos, GST_TYPE_AUDIO_CHANNEL_POSITION);
    for (c = 0; c < channels; c++) {
      g_value_set_enum (&pos, GST_AUDIO_CHANNEL_POSITION_NONE);
      gst_value_array_append_value (&chanpos, &pos);
    }
    g_value_unset (&pos);
    gst_structure_set_value (gst_caps_get_structure (*caps, 0),
        "channel-positions", &chanpos);
    g_value_unset (&chanpos);
  }
  gst_caps_ref (*caps);
}
