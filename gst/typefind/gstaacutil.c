/* GStreamer
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2010 Collabora Multimedia
 * Copyright (C) 2010 Arun Raghavan <arun.raghavan@collabora.co.uk>
 *
 * gstaacutil.c: collection of AAC helper utilities
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

#include <gst/gst.h>

#include "gstaacutil.h"

/* FIXME: This file is duplicated in gst-plugins-* wherever needed, so if you
 * update this file, please find all other instances and update them as well.
 * This less-than-optimal setup is being used till there is a standard location
 * for such common functionality.
 */

/* Determines the level of a stream as defined in ISO/IEC 14496-3. The
 * sample_frequency_index and channel_configuration must be got from the ESDS
 * for MP4 files and the ADTS header for ADTS streams.
 *
 * For AAC LC streams, we assume that apply the constraints from the AAC audio
 * profile. For AAC Main/LTP/SSR/..., we use the Main profile.
 *
 * FIXME: HE-AAC support is TBD.
 *
 * Returns -1 if the level could not be determined.
 */
gint
gst_aac_level_from_header (guint profile, guint rate, guint channel_config)
{
  /* Number of single channel elements, channel pair elements, low frequency
   * elements, independently switched coupling channel elements, and
   * dependently switched coupling channel elements.
   *
   * Note: The 2 CCE types are ignored for now as they require us to actually
   * parse the first frame, and they are rarely found in actual streams.
   */
  int num_sce = 0, num_cpe = 0, num_lfe = 0, num_cce_indep = 0, num_cce_dep = 0;
  int num_channels;
  /* Processor and RAM Complexity Units (calculated and "reference" for single
   * channel) */
  int pcu, rcu, pcu_ref, rcu_ref;

  switch (channel_config) {
    case 0:
      /* Channel config is defined in the AudioObjectType's SpecificConfig,
       * which requires some amount of digging through the headers. I only see
       * this done in the MPEG conformance streams - FIXME */
      GST_WARNING ("Found a stream with channel configuration in the "
          "AudioSpecificConfig. Please file a bug with a link to the media if "
          "possible.");
      return -1;
    case 1:
      /* front center */
      num_sce = 1;
      break;
    case 2:
      /* front left and right */
      num_cpe = 1;
      break;
    case 3:
      /* front left, right, and center */
      num_sce = 1;
      num_cpe = 1;
      break;
    case 4:
      /* front left, right, and center; rear surround */
      num_sce = 2;
      num_cpe = 1;
      break;
    case 5:
      /* front left, right, and center; rear left and right surround */
      num_sce = 1;
      num_cpe = 2;
      break;
    case 6:
      /* front left, right, center and LFE; rear left and right surround */
      num_sce = 1;
      num_cpe = 2;
      break;
    case 7:
      /* front left, right, center and LFE; outside front left and right;
       * rear left and right surround */
      num_sce = 1;
      num_cpe = 3;
      num_lfe = 1;
      break;
    default:
      GST_WARNING ("Unknown channel config in header: %d", channel_config);
      return -1;
  }

  switch (profile) {
    case 0:                    /* NULL */
      GST_WARNING ("profile 0 is not a valid profile");
      return -1;
    case 2:                    /* LC */
      pcu_ref = 3;
      rcu_ref = 3;
      break;
    case 3:                    /* SSR */
      pcu_ref = 4;
      rcu_ref = 3;
      break;
    case 4:                    /* LTP */
      pcu_ref = 4;
      rcu_ref = 4;
      break;
    case 1:                    /* Main */
    default:
      /* Other than a couple of ER profiles, Main is the worst-case */
      pcu_ref = 5;
      rcu_ref = 5;
      break;
  }

  /* "fs_ref" is 48000 Hz for AAC Main/LC/SSR/LTP. SBR's fs_ref is defined as
   * 24000/48000 (in/out), for SBR streams. Actual support is a FIXME */

  pcu = ((float) rate / 48000) * pcu_ref *
      ((2 * num_cpe) + num_sce + num_lfe + num_cce_indep + (0.3 * num_cce_dep));

  rcu = ((float) rcu_ref) * (num_sce + (0.5 * num_lfe) + (0.5 * num_cce_indep) +
      (0.4 * num_cce_dep));

  if (num_cpe < 2)
    rcu += (rcu_ref + (rcu_ref - 1)) * num_cpe;
  else
    rcu += (rcu_ref + (rcu_ref - 1) * ((2 * num_cpe) - 1));

  num_channels = num_sce + (2 * num_cpe) + num_lfe;

  if (profile == 2) {
    /* AAC LC => return the level as per the 'AAC Profile' */
    if (num_channels <= 2 && rate <= 24000 && pcu <= 3 && rcu <= 5)
      return 1;
    if (num_channels <= 2 && rate <= 48000 && pcu <= 6 && rcu <= 5)
      return 2;
    /* There is no level 3 for the AAC Profile */
    if (num_channels <= 5 && rate <= 48000 && pcu <= 19 && rcu <= 15)
      return 4;
    if (num_channels <= 5 && rate <= 96000 && pcu <= 38 && rcu <= 15)
      return 5;
  } else {
    /* Return the level as per the 'Main Profile' */
    if (pcu < 40 && rcu < 20)
      return 1;
    if (pcu < 80 && rcu < 64)
      return 2;
    if (pcu < 160 && rcu < 128)
      return 3;
    if (pcu < 320 && rcu < 256)
      return 4;
  }

  GST_WARNING ("couldn't determine level: profile=%u,rate=%u,channel_config=%u,"
      "pcu=%d,rcu=%d", profile, rate, channel_config, pcu, rcu);
  return -1;
}
