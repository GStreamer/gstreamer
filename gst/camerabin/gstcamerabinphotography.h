/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
 *
 * Photography interface implementation for camerabin
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

#ifndef __GST_CAMERABIN_PHOTOGRAPHY_H__
#define __GST_CAMERABIN_PHOTOGRAPHY_H__

#include <gst/interfaces/photography.h>

#include "gstcamerabin.h"

gboolean
gst_camerabin_photography_set_property (GstCameraBin * camerabin,
                                        guint prop_id,
                                        const GValue * value);

gboolean
gst_camerabin_photography_get_property (GstCameraBin * camerabin,
                                        guint prop_id,
                                        GValue * value);

void gst_camerabin_photography_init (GstPhotographyInterface * iface);

#endif /* #ifndef __GST_CAMERABIN_PHOTOGRAPHY_H__ */
