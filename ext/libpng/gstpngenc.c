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

#define MAX_HEIGHT     		4096 

gint frame=0;

GstElementDetails gst_pngenc_details = {
  "",
  "Filter/Video/Effect",
  "LGPL",
  "Apply a pngenc effect on video"
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

static void 	gst_pngenc_class_init 	        (GstPngEncClass * klass);
static void 	gst_pngenc_init 		(GstPngEnc * pngenc);

static void 	gst_pngenc_set_property 	(GObject * object, guint prop_id,
					  	 const GValue * value, GParamSpec * pspec);
static void 	gst_pngenc_get_property 	(GObject * object, guint prop_id,
					  	 GValue * value, GParamSpec * pspec);

static void 	gst_pngenc_chain 		(GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;


static void user_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
  g_warning("%s", error_msg);
}

static void user_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
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

    pngenc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstPngEnc", &pngenc_info, 0);
  }
  return pngenc_type;
}

static void
gst_pngenc_class_init (GstPngEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_pngenc_set_property;
  gobject_class->get_property = gst_pngenc_get_property;
}


static GstPadConnectReturn
gst_pngenc_sinkconnect (GstPad * pad, GstCaps * caps)
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
  
  pngenc->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (pngenc), pngenc->srcpad);

  gst_pad_set_chain_function (pngenc->sinkpad, gst_pngenc_chain);
  gst_pad_set_link_function (pngenc->sinkpad, gst_pngenc_sinkconnect);

  pngenc->png_struct_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, (png_voidp)NULL, user_error_fn, user_warning_fn);
  if ( pngenc->png_struct_ptr == NULL )
     g_warning ("Failed to initialize png structure");

  pngenc->png_info_ptr = png_create_info_struct (pngenc->png_struct_ptr);

  if (setjmp(pngenc->png_struct_ptr->jmpbuf))
      png_destroy_write_struct (&pngenc->png_struct_ptr, &pngenc->png_info_ptr);

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
    gst_buffer_unref( buffer );
  }
  else
    pngenc->buffer_out = buffer;
}

static void
gst_pngenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstPngEnc *pngenc;
  gint row_indice;
  png_byte *row_pointers[ MAX_HEIGHT ];

  if (frame != 300)
  {
    frame++;
    gst_buffer_unref (buf);
    return ;
  }

  pngenc = GST_PNGENC (gst_pad_get_parent (pad));

  pngenc->buffer_out = NULL;

  png_set_filter (pngenc->png_struct_ptr, 0, PNG_FILTER_NONE | PNG_FILTER_VALUE_NONE);
  png_set_compression_level (pngenc->png_struct_ptr, 9);

  png_set_IHDR(
    pngenc->png_struct_ptr,
    pngenc->png_info_ptr,
    pngenc->width,
    pngenc->height,
    pngenc->bpp/3,
    PNG_COLOR_TYPE_RGB,
    PNG_INTERLACE_NONE,
    PNG_COMPRESSION_TYPE_DEFAULT,
    PNG_FILTER_TYPE_DEFAULT
  );

  png_set_write_fn (pngenc->png_struct_ptr, pngenc, (png_rw_ptr)user_write_data, user_flush_data);

  for (row_indice = 0; row_indice < pngenc->height; row_indice++)
    row_pointers[row_indice] = GST_BUFFER_DATA (buf) + (pngenc->width * row_indice * pngenc->bpp/8);
  
  png_write_info (pngenc->png_struct_ptr, pngenc->png_info_ptr);
  png_write_image (pngenc->png_struct_ptr, row_pointers);
  png_write_end (pngenc->png_struct_ptr, NULL);

  gst_pad_push (pngenc->srcpad, pngenc->buffer_out );
  user_flush_data (pngenc->png_struct_ptr);
  
  png_destroy_info_struct (pngenc->png_struct_ptr, &pngenc->png_info_ptr);
  png_destroy_write_struct (&pngenc->png_struct_ptr, (png_infopp)NULL);

  g_print ("Frame %d dumped\n", frame);
  frame++;

  gst_buffer_unref (buf);
}

static void
gst_pngenc_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPngEnc *pngenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_PNGENC (object));

  pngenc = GST_PNGENC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_pngenc_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstPngEnc *pngenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_PNGENC (object));

  pngenc = GST_PNGENC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
