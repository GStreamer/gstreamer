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
 *
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

GST_BOILERPLATE (MpegTSPatInfo, mpegts_pat_info, GObject, G_TYPE_OBJECT);

MpegTSPatInfo *
mpegts_pat_info_new (guint16 program_no, guint16 pid)
{
  MpegTSPatInfo *info;

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
mpegts_pat_info_class_init (MpegTSPatInfoClass * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) klass;

  gobject_klass->set_property = mpegts_pat_info_set_property;
  gobject_klass->get_property = mpegts_pat_info_get_property;

  g_object_class_install_property (gobject_klass, PROP_PROGRAM_NO,
      g_param_spec_uint ("program-number", "Program Number",
          "Program Number for this program", 0, G_MAXUINT16, 1,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_klass, PROP_PID,
      g_param_spec_uint ("pid", "PID carrying PMT",
          "PID which carries the PMT for this program", 1, G_MAXUINT16, 1,
          G_PARAM_READABLE));
}

static void
mpegts_pat_info_init (MpegTSPatInfo * pat_info, MpegTSPatInfoClass * klass)
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
  MpegTSPatInfo *pat_info;

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
