/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * validate.c - Validate generic functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#  include "config.h"
#endif

#include "validate.h"
#include "gst-validate-internal.h"

GST_DEBUG_CATEGORY (gstvalidate_debug);

void
gst_validate_init (void)
{
  GST_DEBUG_CATEGORY_INIT (gstvalidate_debug, "validate", 0,
      "Validation library");

  /* init the report system (can be called multiple times) */
  gst_validate_report_init ();

  /* Init the scenario system */
  init_scenarios ();

  /* Ensure we load overrides before any use of a monitor */
  gst_validate_override_registry_preload ();
}
