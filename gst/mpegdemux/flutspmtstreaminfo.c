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

#include "flutspmtstreaminfo.h"

enum
{
  PROP_0,
  PROP_PID,
  PROP_LANGUAGES,
  PROP_STREAM_TYPE,
  PROP_DESCRIPTORS,
};

GST_BOILERPLATE (MpegTsPmtStreamInfo, mpegts_pmt_stream_info, GObject,
    G_TYPE_OBJECT);

static void mpegts_pmt_stream_info_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * spec);
static void mpegts_pmt_stream_info_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * spec);
static void mpegts_pmt_stream_info_finalize (GObject * object);

static void
mpegts_pmt_stream_info_base_init (gpointer klass)
{
}

static void
mpegts_pmt_stream_info_class_init (MpegTsPmtStreamInfoClass * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) klass;

  gobject_klass->set_property = mpegts_pmt_stream_info_set_property;
  gobject_klass->get_property = mpegts_pmt_stream_info_get_property;
  gobject_klass->finalize = mpegts_pmt_stream_info_finalize;

  g_object_class_install_property (gobject_klass, PROP_PID,
      g_param_spec_uint ("pid", "PID carrying this stream",
          "PID which carries this stream", 1, G_MAXUINT16, 1,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_LANGUAGES,
      g_param_spec_value_array ("languages", "Languages of this stream",
          "Value array of the languages of this stream",
          g_param_spec_string ("language", "language", "language", "",
              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_STREAM_TYPE,
      g_param_spec_uint ("stream-type",
          "Stream type", "Stream type", 0, G_MAXUINT8, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_DESCRIPTORS,
      g_param_spec_value_array ("descriptors",
          "Descriptors",
          "Value array of strings containing stream descriptors",
          g_param_spec_boxed ("descriptor",
              "descriptor",
              "", G_TYPE_GSTRING, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
mpegts_pmt_stream_info_init (MpegTsPmtStreamInfo * pmt_stream_info,
    MpegTsPmtStreamInfoClass * klass)
{
  pmt_stream_info->languages = g_value_array_new (0);
  pmt_stream_info->descriptors = g_value_array_new (0);
}

static void
mpegts_pmt_stream_info_finalize (GObject * object)
{
  MpegTsPmtStreamInfo *info = MPEGTS_PMT_STREAM_INFO (object);

  g_value_array_free (info->languages);
  g_value_array_free (info->descriptors);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

MpegTsPmtStreamInfo *
mpegts_pmt_stream_info_new (guint16 pid, guint8 type)
{
  MpegTsPmtStreamInfo *info;
  info = g_object_new (MPEGTS_TYPE_PMT_STREAM_INFO, NULL);

  info->pid = pid;
  info->stream_type = type;
  return info;
}

void
mpegts_pmt_stream_info_add_language (MpegTsPmtStreamInfo * pmt_info,
    gchar * language)
{
  GValue v = { 0, };

  g_return_if_fail (MPEGTS_IS_PMT_STREAM_INFO (pmt_info));

  g_value_init (&v, G_TYPE_STRING);
  g_value_take_string (&v, language);
  g_value_array_append (pmt_info->languages, &v);
  g_value_unset (&v);
}

void
mpegts_pmt_stream_info_add_descriptor (MpegTsPmtStreamInfo * pmt_info,
    const gchar * descriptor, guint length)
{
  GValue value = { 0 };
  GString *string;

  g_return_if_fail (MPEGTS_IS_PMT_STREAM_INFO (pmt_info));

  string = g_string_new_len (descriptor, length);

  g_value_init (&value, G_TYPE_GSTRING);
  g_value_take_boxed (&value, string);
  g_value_array_append (pmt_info->descriptors, &value);
  g_value_unset (&value);
}

static void
mpegts_pmt_stream_info_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec)
{
  g_return_if_fail (MPEGTS_IS_PMT_STREAM_INFO (object));

  /* No settable properties */
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
}

static void
mpegts_pmt_stream_info_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec)
{
  MpegTsPmtStreamInfo *si;

  g_return_if_fail (MPEGTS_IS_PMT_STREAM_INFO (object));

  si = MPEGTS_PMT_STREAM_INFO (object);

  switch (prop_id) {
    case PROP_STREAM_TYPE:
      g_value_set_uint (value, si->stream_type);
      break;
    case PROP_PID:
      g_value_set_uint (value, si->pid);
      break;
    case PROP_LANGUAGES:
      g_value_set_boxed (value, si->languages);
      break;
    case PROP_DESCRIPTORS:
      g_value_set_boxed (value, si->descriptors);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
      break;
  }
}
