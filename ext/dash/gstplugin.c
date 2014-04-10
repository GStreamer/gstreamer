#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstdashdemux.h"

#define XML_BUFFER_SIZE 16
#define XML_INC_BUFFER {                                                \
  pos++;                                                                \
  if (pos == XML_BUFFER_SIZE) {                                         \
    pos = 0;                                                            \
    offset += XML_BUFFER_SIZE;                                          \
    data = gst_type_find_peek (tf, offset, XML_BUFFER_SIZE);            \
    if (data == NULL) return FALSE;                                     \
  } else {                                                              \
    data++;                                                             \
  }                                                                     \
}

static gboolean
xml_check_first_element (GstTypeFind * tf, const gchar * element, guint elen,
    gboolean strict)
{
  gboolean got_xmldec;
  const guint8 *data;
  guint offset = 0;
  guint pos = 0;

  data = gst_type_find_peek (tf, 0, XML_BUFFER_SIZE);
  if (!data)
    return FALSE;

  /* look for the XMLDec
   * see XML spec 2.8, Prolog and Document Type Declaration
   * http://www.w3.org/TR/2004/REC-xml-20040204/#sec-prolog-dtd */
  got_xmldec = (memcmp (data, "<?xml", 5) == 0);

  if (strict && !got_xmldec)
    return FALSE;

  /* skip XMLDec in any case if we've got one */
  if (got_xmldec) {
    pos += 5;
    data += 5;
  }

  /* look for the first element, it has to be the requested element. Bail
   * out if it is not within the first 4kB. */
  while (data && (offset + pos) < 4096) {
    while (*data != '<' && (offset + pos) < 4096) {
      XML_INC_BUFFER;
    }

    XML_INC_BUFFER;
    if (!g_ascii_isalpha (*data)) {
      /* if not alphabetic, it's a PI or an element / attribute declaration
       * like <?xxx or <!xxx */
      XML_INC_BUFFER;
      continue;
    }

    /* the first normal element, check if it's the one asked for */
    data = gst_type_find_peek (tf, offset + pos, elen + 1);
    return (data && element && strncmp ((char *) data, element, elen) == 0);
  }

  return FALSE;
}

/*** application/dash+xml typefind helper ***/

static GstStaticCaps dash_caps = GST_STATIC_CAPS ("application/dash+xml");

#define DASH_CAPS gst_static_caps_get (&dash_caps)

static void
dash_type_find (GstTypeFind * tf, gpointer unused)
{
  if (xml_check_first_element (tf, "MPD", 3, FALSE) ||
      xml_check_first_element (tf, "mpd", 3, FALSE)) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, DASH_CAPS);
  }
}

static gboolean
fragmented_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "dashdemux", GST_RANK_PRIMARY,
          GST_TYPE_DASH_DEMUX) || FALSE)
    return FALSE;
  if (!gst_type_find_register (plugin, "application/dash+xml",
          GST_RANK_SECONDARY, dash_type_find, NULL, DASH_CAPS, NULL, NULL))
    return FALSE;
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dashdemux,
    "DASH demuxer plugin",
    fragmented_init, VERSION, "LGPL", PACKAGE_NAME, "http://www.gstreamer.org/")
