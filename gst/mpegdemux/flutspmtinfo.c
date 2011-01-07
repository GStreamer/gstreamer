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

#include "flutspmtinfo.h"

enum
{
  PROP_0,
  PROP_PROGRAM_NO,
  PROP_VERSION_NO,
  PROP_PCR_PID,
  PROP_DESCRIPTORS,
  PROP_STREAMINFO
};

GST_BOILERPLATE (MpegTsPmtInfo, mpegts_pmt_info, GObject, G_TYPE_OBJECT);

static void mpegts_pmt_info_finalize (GObject * object);
static void mpegts_pmt_info_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void mpegts_pmt_info_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

static void
mpegts_pmt_info_base_init (gpointer klass)
{
}

static void
mpegts_pmt_info_class_init (MpegTsPmtInfoClass * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) klass;

  gobject_klass->finalize = mpegts_pmt_info_finalize;
  gobject_klass->set_property = mpegts_pmt_info_set_property;
  gobject_klass->get_property = mpegts_pmt_info_get_property;

  g_object_class_install_property (gobject_klass, PROP_PROGRAM_NO,
      g_param_spec_uint ("program-number", "Program Number",
          "Program Number for this program", 0, G_MAXUINT16, 1,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_PCR_PID,
      g_param_spec_uint ("pcr-pid", "PID carrying the PCR for this program",
          "PID which carries the PCR for this program", 1, G_MAXUINT16, 1,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_STREAMINFO,
      g_param_spec_value_array ("stream-info",
          "GValueArray containing GObjects with properties",
          "Array of GObjects containing information about the program streams",
          g_param_spec_object ("flu-pmt-streaminfo", "FluPMTStreamInfo",
              "Fluendo TS Demuxer PMT Stream info object",
              MPEGTS_TYPE_PMT_STREAM_INFO,
              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_VERSION_NO,
      g_param_spec_uint ("version-number", "Version Number",
          "Version number of this program information", 0, G_MAXUINT8, 1,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_DESCRIPTORS,
      g_param_spec_value_array ("descriptors",
          "Descriptors",
          "Value array of strings containing program descriptors",
          g_param_spec_boxed ("descriptor",
              "descriptor",
              "", G_TYPE_GSTRING, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
mpegts_pmt_info_init (MpegTsPmtInfo * pmt_info, MpegTsPmtInfoClass * klass)
{
  pmt_info->streams = g_value_array_new (0);
  pmt_info->descriptors = g_value_array_new (0);
}

MpegTsPmtInfo *
mpegts_pmt_info_new (guint16 program_no, guint16 pcr_pid, guint8 version_no)
{
  MpegTsPmtInfo *info;

  info = g_object_new (MPEGTS_TYPE_PMT_INFO, NULL);

  info->program_no = program_no;
  info->pcr_pid = pcr_pid;
  info->version_no = version_no;

  return info;
}

static void
mpegts_pmt_info_finalize (GObject * object)
{
  MpegTsPmtInfo *info = MPEGTS_PMT_INFO (object);

  g_value_array_free (info->streams);
  g_value_array_free (info->descriptors);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
mpegts_pmt_info_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec)
{
  g_return_if_fail (MPEGTS_IS_PMT_INFO (object));

  /* No settable properties */
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
}

static void
mpegts_pmt_info_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec)
{
  MpegTsPmtInfo *pmt_info;

  g_return_if_fail (MPEGTS_IS_PMT_INFO (object));

  pmt_info = MPEGTS_PMT_INFO (object);

  switch (prop_id) {
    case PROP_PROGRAM_NO:
      g_value_set_uint (value, pmt_info->program_no);
      break;
    case PROP_PCR_PID:
      g_value_set_uint (value, pmt_info->pcr_pid);
      break;
    case PROP_STREAMINFO:
      g_value_set_boxed (value, pmt_info->streams);
      break;
    case PROP_VERSION_NO:
      g_value_set_uint (value, pmt_info->version_no);
      break;
    case PROP_DESCRIPTORS:
      g_value_set_boxed (value, pmt_info->descriptors);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
      break;
  }
}

void
mpegts_pmt_info_add_descriptor (MpegTsPmtInfo * pmt_info,
    const gchar * descriptor, guint length)
{
  GValue value = { 0 };
  GString *string;

  g_return_if_fail (MPEGTS_IS_PMT_INFO (pmt_info));

  string = g_string_new_len (descriptor, length);

  g_value_init (&value, G_TYPE_GSTRING);
  g_value_take_boxed (&value, string);
  g_value_array_append (pmt_info->descriptors, &value);
  g_value_unset (&value);
}

void
mpegts_pmt_info_add_stream (MpegTsPmtInfo * pmt_info,
    MpegTsPmtStreamInfo * stream)
{
  GValue v = { 0, };

  g_return_if_fail (MPEGTS_IS_PMT_INFO (pmt_info));
  g_return_if_fail (MPEGTS_IS_PMT_STREAM_INFO (stream));

  g_value_init (&v, G_TYPE_OBJECT);
  g_value_take_object (&v, stream);
  g_value_array_append (pmt_info->streams, &v);
  g_value_unset (&v);
}
