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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include "gstpngenc.h"
#include <gst/video/video.h>

#define MAX_HEIGHT		4096


GstElementDetails gst_pngenc_details = {
  "PNG encoder",
  "Codec/Encoder/Image",
  "Encode a video frame to a .png image",
  "Jeremy SIMON <jsimon13@yahoo.fr>",
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

static void     gst_pngenc_base_init    (gpointer g_class);
static void	gst_pngenc_class_init	(GstPngEncClass *klass);
static void	gst_pngenc_init		(GstPngEnc *pngenc);

static void	gst_pngenc_chain	(GstPad *pad, GstData *_data);

GstPadTemplate *pngenc_src_template, *pngenc_sink_template;

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
      sizeof (GstPngEncClass),
      gst_pngenc_base_init,
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

static GstCaps*
png_caps_factory (void)
{
  return gst_caps_new_simple ("video/x-png",
      "width",     GST_TYPE_INT_RANGE, 16, 4096,
      "height",    GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE,
      NULL);
}


static GstCaps*
raw_caps_factory (void)
{ 
  return gst_caps_from_string (GST_VIDEO_CAPS_RGB);
}

static void
gst_pngenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *png_caps;
  
  raw_caps = raw_caps_factory ();
  png_caps = png_caps_factory ();

  pngenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
					       GST_PAD_ALWAYS,
					       raw_caps);
  
  pngenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC,
					      GST_PAD_ALWAYS,
					      png_caps);
  
  gst_element_class_add_pad_template (element_class, pngenc_sink_template);
  gst_element_class_add_pad_template (element_class, pngenc_src_template);
  gst_element_class_set_details (element_class, &gst_pngenc_details);
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
gst_pngenc_sinklink (GstPad *pad, const GstCaps *caps)
{
  GstPngEnc *pngenc;
  gdouble fps;
  GstStructure *structure;

  pngenc = GST_PNGENC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &pngenc->width);
  gst_structure_get_int (structure, "height", &pngenc->height);
  gst_structure_get_double (structure, "framerate", &fps);
  gst_structure_get_int (structure, "bpp", &pngenc->bpp);

  caps = gst_caps_new_simple ("video/x-png",
      "framerate", G_TYPE_DOUBLE, fps,
      "width",     G_TYPE_INT, pngenc->width,
      "height",    G_TYPE_INT, pngenc->height, NULL);

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

  gst_pad_push (pngenc->srcpad, GST_DATA (gst_event_new (GST_EVENT_FLUSH)));
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
gst_pngenc_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
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
    GST_DEBUG ("returning from longjmp");
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

  gst_pad_push (pngenc->srcpad, GST_DATA (pngenc->buffer_out));

  /* send EOS event, since a frame has been pushed out */
  event = gst_event_new (GST_EVENT_EOS);
  gst_pad_push (pngenc->srcpad, GST_DATA (event));

  gst_buffer_unref (buf);
}
