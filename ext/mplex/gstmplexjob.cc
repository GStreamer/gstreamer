/* GStreamer mplex (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmplexjob.hh: gstreamer/mplex multiplex-job wrapper
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmplexjob.hh"


enum {
  ARG_0,
  ARG_FORMAT,
  ARG_MUX_BITRATE,
  ARG_VBR,
  ARG_SYSTEM_HEADERS,
  ARG_SPLIT_SEQUENCE,
  ARG_SEGMENT_SIZE,
  ARG_PACKETS_PER_PACK,
  ARG_SECTOR_SIZE,
  ARG_WORKAROUND_MPLAYER_HDR
  /* FILL ME */
};

/*
 * Property enumeration types.
 */

#define GST_TYPE_MPLEX_FORMAT \
  (gst_mplex_format_get_type ())

static GType
gst_mplex_format_get_type (void)
{
  static GType mplex_format_type = 0;

  if (!mplex_format_type) {
    static const GEnumValue mplex_formats[] = {
      { 0, "0", "Generic MPEG-1" },
      { 1, "1", "Standard VCD" },
      { 2, "2", "User VCD" },
      { 3, "3", "Generic MPEG-2" },
      { 4, "4", "Standard SVCD" },
      { 5, "5", "User SVCD" },
      { 6, "6", "VCD Stills sequences" },
      { 7, "7", "SVCD Stills sequences" },
      { 8, "8", "DVD MPEG-2 for dvdauthor" },
      { 9, "9", "DVD MPEG-2" },
      { 0, NULL, NULL },
    };

    mplex_format_type =
	g_enum_register_static ("GstMplexFormat",
				mplex_formats);
  }

  return mplex_format_type;
}

/*
 * Class init functions.
 */

GstMplexJob::GstMplexJob (void) :
  MultiplexJob ()
{
  /* blabla */
}

/*
 * GObject properties.
 */

void
GstMplexJob::initProperties (GObjectClass *klass)
{
  /* encoding profile */
  g_object_class_install_property (klass, ARG_FORMAT,
    g_param_spec_enum ("format", "Format", "Encoding profile format",
                       GST_TYPE_MPLEX_FORMAT, 0,
		       (GParamFlags) G_PARAM_READWRITE));

  /* total stream datarate. Normally, this shouldn't be needed, but
   * some DVD/VCD/SVCD players really need strict values to handle
   * the created files correctly. */
  g_object_class_install_property (klass, ARG_MUX_BITRATE,
    g_param_spec_int ("mux-bitrate", "Mux. bitrate",
		      "Bitrate of output stream in kbps (0 = autodetect)",
                       0, 15 * 1024, 0, (GParamFlags) G_PARAM_READWRITE));

#if 0
	{ "video-buffer",      1, 0, 'b' },
#endif

  /* some boolean stuff for headers */
  g_object_class_install_property (klass, ARG_VBR,
    g_param_spec_boolean ("vbr", "VBR",
			  "Whether the input video stream is variable bitrate",
                	  FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_SYSTEM_HEADERS,
    g_param_spec_boolean ("system-headers", "System headers",
			  "Create system header in every pack for generic formats",
                	  FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_SPLIT_SEQUENCE,
    g_param_spec_boolean ("split-sequence", "Split sequence",
			  "Simply split a sequence across files "
			  "(rather than building run-out/run-in)",
                	  FALSE, (GParamFlags) G_PARAM_READWRITE));

  /* size of a segment (followed by EOS) */
  g_object_class_install_property (klass, ARG_SEGMENT_SIZE,
    g_param_spec_int ("max-segment-size", "Max. segment size",
		      "Max. size per segment/file in MB (0 = unlimited)",
                       0, 10 * 1024, 0, (GParamFlags) G_PARAM_READWRITE));

  /* packets per pack (generic formats) */
  g_object_class_install_property (klass, ARG_PACKETS_PER_PACK,
    g_param_spec_int ("packets-per-pack", "Packets per pack",
		      "Number of packets per pack for generic formats",
                       1, 100, 1, (GParamFlags) G_PARAM_READWRITE));

  /* size of one sector */
  g_object_class_install_property (klass, ARG_SECTOR_SIZE,
    g_param_spec_int ("sector-size", "Sector size",
		      "Specify sector size in bytes for generic formats",
                       256, 16384, 2048, (GParamFlags) G_PARAM_READWRITE));

  /* workarounds */
  g_object_class_install_property (klass, ARG_WORKAROUND_MPLAYER_HDR,
    g_param_spec_boolean ("mplayer-hdr-workaround", "MPlayer header workaround",
			  "Enable a workaround for bugs in MPlayer LCPM header parsing",
                	  FALSE, (GParamFlags) G_PARAM_READWRITE));
}

/*
 * set/get gobject properties.
 */

void
GstMplexJob::getProperty (guint   prop_id,
			  GValue *value)
{
  switch (prop_id) {
    case ARG_FORMAT:
      g_value_set_enum (value, mux_format);
      break;
    case ARG_MUX_BITRATE:
      g_value_set_int (value, data_rate / 1000);
      break;
    case ARG_VBR:
      g_value_set_boolean (value, VBR);
      break;
    case ARG_SYSTEM_HEADERS:
      g_value_set_boolean (value, always_system_headers);
      break;
    case ARG_SPLIT_SEQUENCE:
      g_value_set_boolean (value, multifile_segment);
      break;
    case ARG_SEGMENT_SIZE:
      g_value_set_int (value, max_segment_size);
      break;
    case ARG_PACKETS_PER_PACK:
      g_value_set_int (value, packets_per_pack);
      break;
    case ARG_SECTOR_SIZE:
      g_value_set_int (value, sector_size);
      break;
    case ARG_WORKAROUND_MPLAYER_HDR:
      g_value_set_boolean (value, workarounds.mplayer_pes_headers);
      break;
    default:
      break;
  }
}

void
GstMplexJob::setProperty (guint         prop_id,
			  const GValue *value)
{
  switch (prop_id) {
    case ARG_FORMAT:
      mux_format = g_value_get_enum (value);
      break;
    case ARG_MUX_BITRATE:
      /* data_rate expects bytes (don't ask me why the property itself is
       * in bits, I'm just staying compatible to mjpegtools options), and
       * rounded up to 50-bytes. */
      data_rate = ((g_value_get_int (value) * 1000 / 8 + 49) / 50 ) * 50;
      break;
    case ARG_VBR:
      VBR = g_value_get_boolean (value);
      break;
    case ARG_SYSTEM_HEADERS:
      always_system_headers = g_value_get_boolean (value);
      break;
    case ARG_SPLIT_SEQUENCE:
      multifile_segment = g_value_get_boolean (value);
      break;
    case ARG_SEGMENT_SIZE:
      max_segment_size = g_value_get_int (value);
      break;
    case ARG_PACKETS_PER_PACK:
      packets_per_pack = g_value_get_int (value);
      break;
    case ARG_SECTOR_SIZE:
      sector_size = g_value_get_int (value);
      break;
    case ARG_WORKAROUND_MPLAYER_HDR:
      workarounds.mplayer_pes_headers = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}
