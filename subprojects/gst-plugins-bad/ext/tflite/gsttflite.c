
/*
 * GStreamer gstreamer-tflite
 * Copyright (C) 2024 Collabora Ltd
 *
 * gsttflite.c
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsttfliteinference.h"

#ifdef EDGETPU
#include "gsttfliteedgetpuinference.h"
#endif

#include "gsttfliteexternalinference.h"

#ifdef TFLITE_VSI
#include "gsttflitevsiinference.h"
#endif

#ifdef TFLITE_HAS_XNNPACK_DELEGATE
#include "gsttflitexnnpackinference.h"
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = GST_ELEMENT_REGISTER (tflite_inference, plugin);

#ifdef EDGETPU
  ret |= GST_ELEMENT_REGISTER (tflite_edgetpu_inference, plugin);
#endif

  ret |= GST_ELEMENT_REGISTER (tflite_external_inference, plugin);

#ifdef TFLITE_VSI
  ret |= GST_ELEMENT_REGISTER (tflite_vsi_inference, plugin);
#endif

#ifdef TFLITE_HAS_XNNPACK_DELEGATE
  ret |= GST_ELEMENT_REGISTER (tflite_xnnpack_inference, plugin);
#endif

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    tflite,
    "TFLITE neural network plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
