/* 
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

GST_BOILERPLATE (MpegTSPmtStreamInfo, mpegts_pmt_stream_info, GObject,
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
mpegts_pmt_stream_info_class_init (MpegTSPmtStreamInfoClass * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) klass;

  gobject_klass->set_property = mpegts_pmt_stream_info_set_property;
  gobject_klass->get_property = mpegts_pmt_stream_info_get_property;
  gobject_klass->finalize = mpegts_pmt_stream_info_finalize;

  g_object_class_install_property (gobject_klass, PROP_PID,
      g_param_spec_uint ("pid", "PID carrying this stream",
          "PID which carries this stream", 1, G_MAXUINT16, 1,
          G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, PROP_LANGUAGES,
      g_param_spec_value_array ("languages", "Languages of this stream",
          "Value array of the languages of this stream",
          g_param_spec_string ("language", "language", "language", "",
              G_PARAM_READABLE), G_PARAM_READABLE));

  g_object_class_install_property (gobject_klass, PROP_STREAM_TYPE,
      g_param_spec_uint ("stream-type",
          "Stream type", "Stream type", 0, G_MAXUINT8, 0, G_PARAM_READABLE));

  g_object_class_install_property (gobject_klass, PROP_DESCRIPTORS,
      g_param_spec_value_array ("descriptors",
          "Descriptors",
          "Value array of strings containing stream descriptors",
          g_param_spec_boxed ("descriptor",
              "descriptor",
              "", G_TYPE_GSTRING, G_PARAM_READABLE), G_PARAM_READABLE));
}

static void
mpegts_pmt_stream_info_init (MpegTSPmtStreamInfo * pmt_stream_info,
    MpegTSPmtStreamInfoClass * klass)
{
  pmt_stream_info->languages = g_value_array_new (0);
  pmt_stream_info->descriptors = g_value_array_new (0);
}

static void
mpegts_pmt_stream_info_finalize (GObject * object)
{
  MpegTSPmtStreamInfo *info = MPEGTS_PMT_STREAM_INFO (object);

  g_value_array_free (info->languages);
  g_value_array_free (info->descriptors);
}

MpegTSPmtStreamInfo *
mpegts_pmt_stream_info_new (guint16 pid, guint8 type)
{
  MpegTSPmtStreamInfo *info;

  info = g_object_new (MPEGTS_TYPE_PMT_STREAM_INFO, NULL);

  info->pid = pid;
  info->stream_type = type;
  return info;
}

void
mpegts_pmt_stream_info_add_language (MpegTSPmtStreamInfo * pmt_info,
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
mpegts_pmt_stream_info_add_descriptor (MpegTSPmtStreamInfo * pmt_info,
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
  MpegTSPmtStreamInfo *si;

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
