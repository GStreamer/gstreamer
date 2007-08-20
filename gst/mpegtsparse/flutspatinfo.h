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

#ifndef __MPEGTS_PAT_INFO_H__
#define __MPEGTS_PAT_INFO_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct MpegTSPatInfoClass {
  GObjectClass parent_class;
} MpegTSPatInfoClass;

typedef struct MpegTSPatInfo {
  GObject parent;

  guint16 pid;
  guint16 program_no;
} MpegTSPatInfo;

#define MPEGTS_TYPE_PAT_INFO (mpegts_pat_info_get_type ())
#define MPEGTS_IS_PAT_INFO(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), MPEGTS_TYPE_PAT_INFO))
#define MPEGTS_PAT_INFO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),MPEGTS_TYPE_PAT_INFO, MpegTSPatInfo))

GType mpegts_pat_info_get_type (void);

MpegTSPatInfo *mpegts_pat_info_new (guint16 program_no, guint16 pid);

G_END_DECLS

#endif
