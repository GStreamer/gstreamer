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
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU Lesser General Public License Version 2 or later (the "LGPL"),
 * in which case the provisions of the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of the MPL or the LGPL.
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

GST_BOILERPLATE (MpegTSPmtInfo, mpegts_pmt_info, GObject, G_TYPE_OBJECT);

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
mpegts_pmt_info_class_init (MpegTSPmtInfoClass * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) klass;

  gobject_klass->finalize = mpegts_pmt_info_finalize;
  gobject_klass->set_property = mpegts_pmt_info_set_property;
  gobject_klass->get_property = mpegts_pmt_info_get_property;

  g_object_class_install_property (gobject_klass, PROP_PROGRAM_NO,
      g_param_spec_uint ("program-number", "Program Number",
          "Program Number for this program", 0, G_MAXUINT16, 1,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_klass, PROP_PCR_PID,
      g_param_spec_uint ("pcr-pid", "PID carrying the PCR for this program",
          "PID which carries the PCR for this program", 1, G_MAXUINT16, 1,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_klass, PROP_STREAMINFO,
      g_param_spec_value_array ("stream-info",
          "GValueArray containing GObjects with properties",
          "Array of GObjects containing information about the program streams",
          g_param_spec_object ("flu-pmt-streaminfo", "FluPMTStreamInfo",
              "Fluendo TS Demuxer PMT Stream info object",
              MPEGTS_TYPE_PMT_STREAM_INFO, G_PARAM_READABLE),
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_klass, PROP_VERSION_NO,
      g_param_spec_uint ("version-number", "Version Number",
          "Version number of this program information", 0, G_MAXUINT8, 1,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_klass, PROP_DESCRIPTORS,
      g_param_spec_value_array ("descriptors",
          "Descriptors",
          "Value array of strings containing program descriptors",
          g_param_spec_boxed ("descriptor",
              "descriptor",
              "", G_TYPE_GSTRING, G_PARAM_READABLE), G_PARAM_READABLE));
}

static void
mpegts_pmt_info_init (MpegTSPmtInfo * pmt_info, MpegTSPmtInfoClass * klass)
{
  pmt_info->streams = g_value_array_new (0);
  pmt_info->descriptors = g_value_array_new (0);
}

MpegTSPmtInfo *
mpegts_pmt_info_new (guint16 program_no, guint16 pcr_pid, guint8 version_no)
{
  MpegTSPmtInfo *info;

  info = g_object_new (MPEGTS_TYPE_PMT_INFO, NULL);

  info->program_no = program_no;
  info->pcr_pid = pcr_pid;
  info->version_no = version_no;

  return info;
}

static void
mpegts_pmt_info_finalize (GObject * object)
{
  MpegTSPmtInfo *info = MPEGTS_PMT_INFO (object);

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
  MpegTSPmtInfo *pmt_info;

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
mpegts_pmt_info_add_descriptor (MpegTSPmtInfo * pmt_info,
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
mpegts_pmt_info_add_stream (MpegTSPmtInfo * pmt_info,
    MpegTSPmtStreamInfo * stream)
{
  GValue v = { 0, };

  g_return_if_fail (MPEGTS_IS_PMT_INFO (pmt_info));
  g_return_if_fail (MPEGTS_IS_PMT_STREAM_INFO (stream));

  g_value_init (&v, G_TYPE_OBJECT);
  g_value_take_object (&v, stream);
  g_value_array_append (pmt_info->streams, &v);
}
