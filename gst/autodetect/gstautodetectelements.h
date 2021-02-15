/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 Huawei Technologies Co., Ltd.
 *   @Author: Stéphane Cerveau <stephane.cerveau@collabora.com>
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

#ifndef __GST_AUTO_DETECT_ELEMENTS_H__
#define __GST_AUTO_DETECT_ELEMENTS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

void autodetect_element_init (GstPlugin * plugin);

GST_ELEMENT_REGISTER_DECLARE (autovideosink);
GST_ELEMENT_REGISTER_DECLARE (autovideosrc);
GST_ELEMENT_REGISTER_DECLARE (autoaudiosink);
GST_ELEMENT_REGISTER_DECLARE (autoaudiosrc);

GST_DEBUG_CATEGORY_EXTERN (autodetect_debug);
#define GST_CAT_DEFAULT autodetect_debug

G_END_DECLS

#endif /* __GST_AUTO_DETECT_ELEMENTS_H__ */
