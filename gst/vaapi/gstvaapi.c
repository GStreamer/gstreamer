/*
 *  gstvaapi.c - VA-API element registration
 *
 *  Copyright (C) 2011-2012 Intel Corporation
 *  Copyright (C) 2011 Collabora Ltd.
 *    Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/gst.h>
#include "gstvaapidownload.h"
#include "gstvaapiupload.h"
#include "gstvaapidecode.h"
#include "gstvaapipostproc.h"
#include "gstvaapisink.h"

static gboolean
plugin_init (GstPlugin *plugin)
{
    gst_element_register(plugin, "vaapidownload",
                         GST_RANK_SECONDARY,
                         GST_TYPE_VAAPIDOWNLOAD);
    gst_element_register(plugin, "vaapiupload",
                         GST_RANK_PRIMARY,
                         GST_TYPE_VAAPIUPLOAD);
    gst_element_register(plugin, "vaapidecode",
                         GST_RANK_PRIMARY,
                         GST_TYPE_VAAPIDECODE);
    gst_element_register(plugin, "vaapipostproc",
                         GST_RANK_PRIMARY,
                         GST_TYPE_VAAPIPOSTPROC);
    gst_element_register(plugin, "vaapisink",
                         GST_RANK_PRIMARY,
                         GST_TYPE_VAAPISINK);
    return TRUE;
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "vaapi",
    "VA-API based elements",
    plugin_init,
    PACKAGE_VERSION,
    "LGPL",
    PACKAGE,
    PACKAGE_BUGREPORT)
