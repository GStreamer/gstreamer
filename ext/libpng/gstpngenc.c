/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Filter:
 * Copyright (C) 2000 Donald A. Graft
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
 */

#include <string.h>
#include <gst/gst.h>
#include "gstpngenc.h"

#define MAX_HEIGHT		4096


GstElementDetails gst_pngenc_details = {
  "",
  "Filter/Video/Effect",
  "LGPL",
  "Encode a video frame to a .png image"
  VERSION,
  "Jeremy SIMON <jsimon13@yahoo.fr>",
  "(C) 2000 Donald Graft",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static void	gst_pngenc_class_init	(GstPngEncClass *klass);
static void	gst_pngenc_init		(GstPngEnc *pngenc);

static void	gst_pngenc_chain	(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;


static void user_error_fn (png_structp png_ptr, png_const_charp error_msg)
{
  g_warning("%s", error_msg);
}

static void user_warning_fn (png_structp png_ptr, png_const_charp warning_msg)
{
  g_warning("%s", warning_msg);
}


GType gst_pngenc_get_type (void)
{
  static GType pngenc_type = 0;

  if (!pngenc_type) {
    static const GTypeInfo pngenc_info = {
      sizeof (GstPngEncClass), NULL,
      NULL,
      (GClassInitFunc) gst_pngenc_class_init,
      NULL,
      NULL,
      sizeof (GstPngEnc),
      0,
      (GInstanceInitFunc) gst_pngenc_init,
    };

    pngenc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstPngEnc",
		                          &pngenc_info, 0);
  }
  return pngenc_type;
}

static void
gst_pngenc_class_init (GstPngEncClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}


static GstPadLinkReturn
gst_pngenc_sinklink (GstPad *pad, GstCaps *caps)
{
  GstPngEnc *pngenc;

  pngenc = GST_PNGENC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_caps_get_int (caps, "width", &pngenc->width);
  gst_caps_get_int (caps, "height", &pngenc->height);
  gst_caps_get_int (caps, "bpp", &pngenc->bpp);

  return gst_pad_try_set_caps (pngenc->srcpad, caps);
}

static void
gst_pngenc_init (GstPngEnc * pngenc)
{
  pngenc->sinkpad = gst_pad_new_from_template (pngenc_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (pngenc), pngenc->sinkpad);

  pngenc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (pngenc), pngenc->srcpad);

  gst_pad_set_chain_function (pngenc->sinkpad, gst_pngenc_chain);
  gst_pad_set_link_function (pngenc->sinkpad, gst_pngenc_sinklink);

  pngenc->png_struct_ptr = NULL;
  pngenc->png_info_ptr = NULL;

}

void user_flush_data (png_structp png_ptr)
{
GstPngEnc *pngenc;

  pngenc = (GstPngEnc *) png_get_io_ptr (png_ptr);

  gst_pad_push (pngenc->srcpad, GST_BUFFER (gst_event_new (GST_EVENT_FLUSH)));
}


void user_write_data (png_structp png_ptr, png_bytep data, png_uint_32 length)
{
  GstBuffer *buffer;
  GstPngEnc *pngenc;

  pngenc = (GstPngEnc *) png_get_io_ptr (png_ptr);

  buffer = gst_buffer_new();
  GST_BUFFER_DATA (buffer) = g_memdup (data, length);
  GST_BUFFER_SIZE (buffer) = length;

  if (pngenc->buffer_out)
  {
    pngenc->buffer_out = gst_buffer_merge (pngenc->buffer_out, buffer);
    gst_buffer_unref (buffer);
  }
  else
    pngenc->buffer_out = buffer;
}

static void
gst_pngenc_chain (GstPad *pad, GstBuffer *buf)
{
  GstPngEnc *pngenc;
  gint row_index;
  png_byte *row_pointers[MAX_HEIGHT];
  GstEvent *event;

  pngenc = GST_PNGENC (gst_pad_get_parent (pad));

  pngenc->buffer_out = NULL;
  if (!GST_PAD_IS_USABLE (pngenc->srcpad))
  {
    gst_buffer_unref (buf);
    return;
  }

  /* initialize png struct stuff */
  pngenc->png_struct_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
		           (png_voidp) NULL, user_error_fn, user_warning_fn);
  /* FIXME: better error handling */
  if (pngenc->png_struct_ptr == NULL)
     g_warning ("Failed to initialize png structure");

  pngenc->png_info_ptr = png_create_info_struct (pngenc->png_struct_ptr);
  if (!pngenc->png_info_ptr)
  {
    png_destroy_read_struct (&(pngenc->png_struct_ptr), (png_infopp) NULL,
		             (png_infopp) NULL);
  }

  /* non-0 return is from a longjmp inside of libpng */
  if (setjmp (pngenc->png_struct_ptr->jmpbuf) != 0)
  {
    GST_DEBUG (GST_CAT_PLUGIN_INFO, "returning from longjmp");
    png_destroy_write_struct (&pngenc->png_struct_ptr, &pngenc->png_info_ptr);
    return;
  }

  png_set_filter (pngenc->png_struct_ptr, 0,
		  PNG_FILTER_NONE | PNG_FILTER_VALUE_NONE);
  png_set_compression_level (pngenc->png_struct_ptr, 9);

  png_set_IHDR(
    pngenc->png_struct_ptr,
    pngenc->png_info_ptr,
    pngenc->width,
    pngenc->height,
    pngenc->bpp / 3,
    PNG_COLOR_TYPE_RGB,
    PNG_INTERLACE_NONE,
    PNG_COMPRESSION_TYPE_DEFAULT,
    PNG_FILTER_TYPE_DEFAULT
  );

  png_set_write_fn (pngenc->png_struct_ptr, pngenc,
		    (png_rw_ptr) user_write_data, user_flush_data);

  for (row_index = 0; row_index < pngenc->height; row_index++)
    row_pointers[row_index] = GST_BUFFER_DATA (buf) +
	                      (pngenc->width * row_index * pngenc->bpp / 8);

  png_write_info (pngenc->png_struct_ptr, pngenc->png_info_ptr);
  png_write_image (pngenc->png_struct_ptr, row_pointers);
  png_write_end (pngenc->png_struct_ptr, NULL);

  user_flush_data (pngenc->png_struct_ptr);

  png_destroy_info_struct (pngenc->png_struct_ptr, &pngenc->png_info_ptr);
  png_destroy_write_struct (&pngenc->png_struct_ptr, (png_infopp) NULL);

  gst_pad_push (pngenc->srcpad, pngenc->buffer_out);

  /* send EOS event, since a frame has been pushed out */
  event = gst_event_new (GST_EVENT_EOS);
  gst_pad_push (pngenc->srcpad, GST_BUFFER (event));

  gst_buffer_unref (buf);
}
