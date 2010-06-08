/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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

#include <ges/ges.h>
#include <gst/controller/gstcontroller.h>
#include "ges-internal.h"

GST_DEBUG_CATEGORY (ges_debug);

/**
 * SECTION:ges-common
 * @short_description: Initialization.
 */

/**
 * ges_init:
 *
 * Initialize the GStreamer Editing Service. Call this before any usage of
 * GES.
 */

void
ges_init (void)
{
  /* initialize debugging category */
  GST_DEBUG_CATEGORY_INIT (ges_debug, "ges", GST_DEBUG_FG_YELLOW,
      "GStreamer Editing Services");
  gst_controller_init (NULL, NULL);

  GST_DEBUG ("GStreamer Editing Services initialized");
}
