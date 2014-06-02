/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2013, Fluendo S.A.
 *   Author: Andoni Morales <amorales@fluendo.com>
 * Copyright (C) 2014, Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2014, Collabora Ltd.
 *   Author: Matthieu Bouron <matthieu.bouron@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */
#ifndef __GST_AMC_JNI_UTILS_H__
#define __GST_AMC_JNI_UTILS_H__

#include <jni.h>
#include <glib.h>
#include <gst/gst.h>

G_GNUC_PRINTF (5, 6)
void gst_amc_jni_set_error                   (JNIEnv * env,
                                              GQuark domain,
                                              gint code,
                                              GError ** error,
                                              const gchar * format, ...);

gboolean gst_amc_jni_initialize              (void);

gboolean gst_amc_jni_is_vm_started           (void);

JNIEnv *gst_amc_jni_get_env                  (void);

#endif
