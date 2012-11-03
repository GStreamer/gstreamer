/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gst.h>

#include "asfheaders.h"

const ASFGuidHash asf_payload_ext_guids[] = {
  {ASF_PAYLOAD_EXTENSION_DURATION, "ASF_PAYLOAD_EXTENSION_DURATION",
        {0xC6BD9450, 0x4907867F, 0x79C7A383, 0xAD33B721}
      },
  {ASF_PAYLOAD_EXTENSION_SYSTEM_CONTENT, "ASF_PAYLOAD_EXTENSION_SYSTEM_CONTENT",
      {0xD590DC20, 0x436C07BC, 0xBBF3f79C, 0xDCA4F1FB}},
  {ASF_PAYLOAD_EXTENSION_SYSTEM_PIXEL_ASPECT_RATIO,
        "ASF_PAYLOAD_EXTENSION_SYSTEM_PIXEL_ASPECT_RATIO",
      {0x1b1ee554, 0x4bc8f9ea, 0x6b371a82, 0xb8c4e474}},
  {ASF_PAYLOAD_EXTENSION_UNDEFINED, "ASF_PAYLOAD_EXTENSION_UNDEFINED",
        {0, 0, 0, 0}
      }
};

const ASFGuidHash asf_correction_guids[] = {
  {ASF_CORRECTION_ON, "ASF_CORRECTION_ON",
        {0xBFC3CD50, 0x11CF618F, 0xAA00B28B, 0x20E2B400}
      },
  {ASF_CORRECTION_OFF, "ASF_CORRECTION_OFF",
        {0x20FB5700, 0x11CF5B55, 0x8000FDA8, 0x2B445C5F}
      },
  /* CHECKME: where does this 49F1A440... GUID come from? (tpm) */
  {ASF_CORRECTION_OFF, "ASF_CORRECTION_OFF",
        {0x49F1A440, 0x11D04ECE, 0xA000ACA3, 0xF64803C9}
      },
  {ASF_CORRECTION_UNDEFINED, "ASF_CORRECTION_UNDEFINED",
        {0, 0, 0, 0}
      }
};

const ASFGuidHash asf_stream_guids[] = {
  {ASF_STREAM_VIDEO, "ASF_STREAM_VIDEO",
        {0xBC19EFC0, 0x11CF5B4D, 0x8000FDA8, 0x2B445C5F}
      },
  {ASF_STREAM_AUDIO, "ASF_STREAM_AUDIO",
        {0xF8699E40, 0x11CF5B4D, 0x8000FDA8, 0x2B445C5F}
      },
  {ASF_STREAM_UNDEFINED, "ASF_STREAM_UNDEFINED",
        {0, 0, 0, 0}
      }
};

const ASFGuidHash asf_object_guids[] = {
  {ASF_OBJ_STREAM, "ASF_OBJ_STREAM",
        {0xB7DC0791, 0x11CFA9B7, 0xC000E68E, 0x6553200C}
      },
  {ASF_OBJ_DATA, "ASF_OBJ_DATA",
        {0x75b22636, 0x11cf668e, 0xAA00D9a6, 0x6Cce6200}
      },
  {ASF_OBJ_FILE, "ASF_OBJ_FILE",
        {0x8CABDCA1, 0x11CFA947, 0xC000E48E, 0x6553200C}
      },
  {ASF_OBJ_HEADER, "ASF_OBJ_HEADER",
        {0x75B22630, 0x11CF668E, 0xAA00D9A6, 0x6CCE6200}
      },
  {ASF_OBJ_CONCEAL_NONE, "ASF_OBJ_CONCEAL_NONE",
        {0x20fb5700, 0x11cf5b55, 0x8000FDa8, 0x2B445C5f}
      },
  {ASF_OBJ_COMMENT, "ASF_OBJ_COMMENT",
        {0x75b22633, 0x11cf668e, 0xAA00D9a6, 0x6Cce6200}
      },
  {ASF_OBJ_CODEC_COMMENT, "ASF_OBJ_CODEC_COMMENT",
        {0x86D15240, 0x11D0311D, 0xA000A4A3, 0xF64803C9}
      },
  {ASF_OBJ_CODEC_COMMENT1, "ASF_OBJ_CODEC_COMMENT1",
        {0x86d15241, 0x11d0311d, 0xA000A4a3, 0xF64803c9}
      },
  {ASF_OBJ_SIMPLE_INDEX, "ASF_OBJ_SIMPLE_INDEX",
        {0x33000890, 0x11cfe5b1, 0xA000F489, 0xCB4903c9}
      },
  {ASF_OBJ_INDEX, "ASF_OBJ_INDEX",
        {0xd6e229d3, 0x11d135da, 0xa0003490, 0xbe4903c9}
      },
  {ASF_OBJ_HEAD1, "ASF_OBJ_HEAD1",
        {0x5fbf03b5, 0x11cfa92e, 0xC000E38e, 0x6553200c}
      },
  {ASF_OBJ_HEAD2, "ASF_OBJ_HEAD2",
        {0xabd3d211, 0x11cfa9ba, 0xC000E68e, 0x6553200c}
      },
  {ASF_OBJ_PADDING, "ASF_OBJ_PADDING",
        {0x1806D474, 0x4509CADF, 0xAB9ABAA4, 0xE8AA96CB}
      },
  {ASF_OBJ_BITRATE_PROPS, "ASF_OBJ_BITRATE_PROPS",
        {0x7bf875ce, 0x11d1468d, 0x6000828d, 0xb2a2c997}
      },
  {ASF_OBJ_EXT_CONTENT_DESC, "ASF_OBJ_EXT_CONTENT_DESC",
        {0xd2d0a440, 0x11d2e307, 0xa000f097, 0x50a85ec9}
      },
  {ASF_OBJ_BITRATE_MUTEX, "ASF_OBJ_BITRATE_MUTEX",
        {0xd6e229dc, 0x11d135da, 0xa0003490, 0xbe4903c9}
      },
  {ASF_OBJ_LANGUAGE_LIST, "ASF_OBJ_LANGUAGE_LIST",
        {0x7c4346a9, 0x4bfcefe0, 0x3e3929b2, 0x855c41de}
      },
  {ASF_OBJ_METADATA_OBJECT, "ASF_OBJ_METADATA_OBJECT",
        {0xc5f8cbea, 0x48775baf, 0x8caa6784, 0xca4cfa44}
      },
  {ASF_OBJ_EXTENDED_STREAM_PROPS, "ASF_OBJ_EXTENDED_STREAM_PROPS",
        {0x14e6a5cb, 0x4332c672, 0x69a99983, 0x5a5b0652}
      },
  {ASF_OBJ_COMPATIBILITY, "ASF_OBJ_COMPATIBILITY",
        {0x26f18b5d, 0x47ec4584, 0x650e5f9f, 0xc952041f}
      },
  {ASF_OBJ_INDEX_PLACEHOLDER, "ASF_OBJ_INDEX_PLACEHOLDER",
        {0xd9aade20, 0x4f9c7c17, 0x558528bc, 0xa2e298dd}
      },
  {ASF_OBJ_INDEX_PARAMETERS, "ASF_OBJ_INDEX_PARAMETERS",
        {0xd6e229df, 0x11d135da, 0xa0003490, 0xbe4903c9}
      },
  {ASF_OBJ_ADVANCED_MUTUAL_EXCLUSION, "ASF_OBJ_ADVANCED_MUTUAL_EXCLUSION",
        {0xa08649cf, 0x46704775, 0x356e168a, 0xcd667535}
      },
  {ASF_OBJ_STREAM_PRIORITIZATION, "ASF_OBJ_STREAM_PRIORITIZATION",
        {0xd4fed15b, 0x454f88d3, 0x5cedf081, 0x249e9945}
      },
  {ASF_OBJ_CONTENT_ENCRYPTION, "ASF_OBJ_CONTENT_ENCRYPTION",
        {0x2211b3fb, 0x11d2bd23, 0xa000b7b4, 0x6efc55c9}
      },
  {ASF_OBJ_EXT_CONTENT_ENCRYPTION, "ASF_OBJ_EXT_CONTENT_ENCRYPTION",
        {0x298ae614, 0x4c172622, 0xe0da35b9, 0x9c28e97e}
      },
  {ASF_OBJ_DIGITAL_SIGNATURE_OBJECT, "ASF_OBJ_DIGITAL_SIGNATURE_OBJECT",
        {0x2211b3fc, 0x11d2bd23, 0xa000b7b4, 0x6efc55c9}
      },
  {ASF_OBJ_SCRIPT_COMMAND, "ASF_OBJ_SCRIPT_COMMAND",
        {0x1efb1a30, 0x11d00b62, 0xa0009ba3, 0xf64803c9}
      },
  {ASF_OBJ_MARKER, "ASF_OBJ_MARKER",
        {0xf487cd01, 0x11cfa951, 0xc000e68e, 0x6553200c}
      },
  /* This guid is definitely used for encryption (mentioned in MS smooth
   * streaming docs) in new PlayReady (c) (tm) (wtf) system, but I haven't
   * found a proper name for it.
   * (Edward Jan 11 2011).*/
  {ASF_OBJ_UNKNOWN_ENCRYPTION_OBJECT, "ASF_OBJ_UNKNOWN_ENCRYPTION_OBJECT",
        {0x9a04f079, 0x42869840, 0x5be692ab, 0x955f88e0}
      },
  {ASF_OBJ_UNDEFINED, "ASF_OBJ_UNDEFINED",
        {0, 0, 0, 0}
      }
};

guint32
gst_asf_identify_guid (const ASFGuidHash * guids, ASFGuid * guid)
{
  gint i;

  for (i = 0; guids[i].obj_id != ASF_OBJ_UNDEFINED; ++i) {
    if (guids[i].guid.v1 == guid->v1 &&
        guids[i].guid.v2 == guid->v2 &&
        guids[i].guid.v3 == guid->v3 && guids[i].guid.v4 == guid->v4) {
      return guids[i].obj_id;
    }
  }

  /* The base case if none is found */
  return ASF_OBJ_UNDEFINED;
}

const gchar *
gst_asf_get_guid_nick (const ASFGuidHash * guids, guint32 obj_id)
{
  gint i;

  for (i = 0; guids[i].obj_id != ASF_OBJ_UNDEFINED; ++i) {
    if (guids[i].obj_id == obj_id) {
      return guids[i].obj_id_str;
    }
  }

  /* The base case if none is found */
  return "ASF_OBJ_UNDEFINED";
}
