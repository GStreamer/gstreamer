/*
 *
 * GStreamer
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
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

/*
 * Relicensed for GStreamer from GPL to LGPL with permit from Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "greedyh.h"
#include "greedyhmacros.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "gst/gst.h"
#include "plugins.h"
#include "gstdeinterlace2.h"
#include "speedy.h"


#define MAXCOMB_DEFAULT          5
#define MOTIONTHRESHOLD_DEFAULT 25
#define MOTIONSENSE_DEFAULT     30

unsigned int GreedyMaxComb;

unsigned int GreedyMotionThreshold;

unsigned int GreedyMotionSense;


#define IS_SSE
#define SSE_TYPE SSE
#define FUNCT_NAME greedyDScaler_SSE
#include "greedyh.asm"
#undef SSE_TYPE
#undef IS_SSE
#undef FUNCT_NAME

#define IS_3DNOW
#define FUNCT_NAME greedyDScaler_3DNOW
#define SSE_TYPE 3DNOW
#include "greedyh.asm"
#undef SSE_TYPE
#undef IS_3DNOW
#undef FUNCT_NAME

#define IS_MMX
#define SSE_TYPE MMX
#define FUNCT_NAME greedyDScaler_MMX
#include "greedyh.asm"
#undef SSE_TYPE
#undef IS_MMX
#undef FUNCT_NAME

void
deinterlace_frame_di_greedyh (GstDeinterlace2 * object)
{
  if (object->cpu_feature_flags & OIL_IMPL_FLAG_SSE) {
    greedyh_filter_sse (object);
  } else if (object->cpu_feature_flags & OIL_IMPL_FLAG_3DNOW) {
    greedyh_filter_3dnow (object);
  } else {
    greedyh_filter_mmx (object);
  }
}

static deinterlace_method_t greedyh_method = {
  0,                            //DEINTERLACE_PLUGIN_API_VERSION,
  "Motion Adaptive: Advanced Detection",
  "AdaptiveAdvanced",
  4,
  OIL_IMPL_FLAG_MMX,
  0,
  0,
  0,
  0,
  0,
  0,
  deinterlace_frame_di_greedyh,
  {"Uses heuristics to detect motion in the input",
        "frames and reconstruct image detail where",
        "possible.  Use this for high quality output",
        "even on monitors set to an arbitrary refresh",
        "rate.",
        "",
        "Advanced detection uses linear interpolation",
        "where motion is detected, using a four-field",
        "buffer.  This is the Greedy: High Motion",
      "deinterlacer from DScaler."}
};

deinterlace_method_t *
dscaler_greedyh_get_method (void)
{
  greedyh_init ();
  return &greedyh_method;
}

void
greedyh_init (void)
{
  GreedyMaxComb = MAXCOMB_DEFAULT;
  GreedyMotionThreshold = MOTIONTHRESHOLD_DEFAULT;
  GreedyMotionSense = MOTIONSENSE_DEFAULT;
}

void
greedyh_filter_mmx (GstDeinterlace2 * object)
{
  greedyDScaler_MMX (object);
}

void
greedyh_filter_3dnow (GstDeinterlace2 * object)
{
  greedyDScaler_3DNOW (object);
}

void
greedyh_filter_sse (GstDeinterlace2 * object)
{
  greedyDScaler_SSE (object);
}
