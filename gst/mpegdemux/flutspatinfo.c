/*
 * This library is licensed under 2 different licenses and you
 * can choose to use it under the terms of either one of them. The
 * two licenses are the MPL 1.1 and the LGPL.
 *
 * MPL:
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * LGPL:
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
 *
 * The Original Code is Fluendo MPEG Demuxer plugin.
 *
 * The Initial Developer of the Original Code is Fluendo, S.L.
 * Portions created by Fluendo, S.L. are Copyright (C) 2005
 * Fluendo, S.L. All Rights Reserved.
 *
 * Contributor(s): Jan Schmidt <jan@fluendo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "flutspatinfo.h"

enum
{
  PROP_0,
  PROP_PROGRAM_NO,
  PROP_PID
};

static void mpegts_pat_info_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void mpegts_pat_info_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

GST_BOILERPLATE (MpegTsPatInfo, mpegts_pat_info, GObject, G_TYPE_OBJECT);

MpegTsPatInfo *
mpegts_pat_info_new (guint16 program_no, guint16 pid)
{
  MpegTsPatInfo *info;

  info = g_object_new (MPEGTS_TYPE_PAT_INFO, NULL);

  info->program_no = program_no;
  info->pid = pid;

  return info;
}

static void
mpegts_pat_info_base_init (gpointer klass)
{
}

static void
mpegts_pat_info_class_init (MpegTsPatInfoClass * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) klass;

  gobject_klass->set_property = mpegts_pat_info_set_property;
  gobject_klass->get_property = mpegts_pat_info_get_property;

  g_object_class_install_property (gobject_klass, PROP_PROGRAM_NO,
      g_param_spec_uint ("program-number", "Program Number",
          "Program Number for this program", 0, G_MAXUINT16, 1,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_PID,
      g_param_spec_uint ("pid", "PID carrying PMT",
          "PID which carries the PMT for this program", 1, G_MAXUINT16, 1,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
mpegts_pat_info_init (MpegTsPatInfo * pat_info, MpegTsPatInfoClass * klass)
{
}

static void
mpegts_pat_info_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec)
{
  g_return_if_fail (MPEGTS_IS_PAT_INFO (object));

  /* No settable properties */
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
}

static void
mpegts_pat_info_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec)
{
  MpegTsPatInfo *pat_info;

  g_return_if_fail (MPEGTS_IS_PAT_INFO (object));

  pat_info = MPEGTS_PAT_INFO (object);

  switch (prop_id) {
    case PROP_PROGRAM_NO:
      g_value_set_uint (value, pat_info->program_no);
      break;
    case PROP_PID:
      g_value_set_uint (value, pat_info->pid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
      break;
  }
}
