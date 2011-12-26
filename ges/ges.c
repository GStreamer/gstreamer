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

#define GES_GNONLIN_VERSION_NEEDED_MAJOR 0
#define GES_GNONLIN_VERSION_NEEDED_MINOR 10
#define GES_GNONLIN_VERSION_NEEDED_MICRO 16

GST_DEBUG_CATEGORY (_ges_debug);

/**
 * SECTION:ges-common
 * @short_description: Initialization.
 */

static gboolean
ges_check_gnonlin_availability (void)
{
  gboolean ret = TRUE;
  if (!gst_default_registry_check_feature_version ("gnlcomposition",
          GES_GNONLIN_VERSION_NEEDED_MAJOR, GES_GNONLIN_VERSION_NEEDED_MINOR,
          GES_GNONLIN_VERSION_NEEDED_MICRO)) {
    GST_ERROR
        ("GNonLin plugins not found, or not at least version %u.%u.%u",
        GES_GNONLIN_VERSION_NEEDED_MAJOR,
        GES_GNONLIN_VERSION_NEEDED_MINOR, GES_GNONLIN_VERSION_NEEDED_MICRO);
    ret = FALSE;
  }
  return ret;
}

/**
 * ges_init:
 *
 * Initialize the GStreamer Editing Service. Call this before any usage of
 * GES. You should take care of initilizing GStreamer before calling this
 * function.
 */

gboolean
ges_init (void)
{
  /* initialize debugging category */
  GST_DEBUG_CATEGORY_INIT (_ges_debug, "ges", GST_DEBUG_FG_YELLOW,
      "GStreamer Editing Services");
  gst_controller_init (NULL, NULL);

  /* register timeline object classes with the system */

  GES_TYPE_TIMELINE_TEST_SOURCE;
  GES_TYPE_TIMELINE_FILE_SOURCE;
  GES_TYPE_TIMELINE_TITLE_SOURCE;
  GES_TYPE_TIMELINE_STANDARD_TRANSITION;
  GES_TYPE_TIMELINE_OVERLAY;

  /* check the gnonlin elements are available */
  if (!ges_check_gnonlin_availability ())
    return FALSE;

  /* TODO: user-defined types? */

  GST_DEBUG ("GStreamer Editing Services initialized");

  return TRUE;
}


/**
 * ges_version:
 * @major: (out): pointer to a guint to store the major version number
 * @minor: (out): pointer to a guint to store the minor version number
 * @micro: (out): pointer to a guint to store the micro version number
 * @nano:  (out): pointer to a guint to store the nano version number
 *
 * Gets the version number of the GStreamer Editing Services library.
 */
void
ges_version (guint * major, guint * minor, guint * micro, guint * nano)
{
  g_return_if_fail (major);
  g_return_if_fail (minor);
  g_return_if_fail (micro);
  g_return_if_fail (nano);

  *major = GES_VERSION_MAJOR;
  *minor = GES_VERSION_MINOR;
  *micro = GES_VERSION_MICRO;
  *nano = GES_VERSION_NANO;
}
