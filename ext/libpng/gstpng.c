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

extern GstElementDetails gst_pngenc_details;

static GstCaps*
png_caps_factory (void)
{
  return gst_caps_new ( "png_png", "video/png", NULL);
}


static GstCaps*
raw_caps_factory (void)
{ 
  return gst_caps_new ( "png_raw", 
  			"video/raw",
			 gst_props_new (
			  "format",         GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")),
	                  "bpp",            GST_PROPS_INT (24),
			  "red_mask",       GST_PROPS_INT (0xff),
			  "green_mask",     GST_PROPS_INT (0xff00),
			  "blue_mask",      GST_PROPS_INT (0xff0000),
			  "width",          GST_PROPS_INT_RANGE (16, 4096),
			  "height",         GST_PROPS_INT_RANGE (16, 4096),
			  NULL )
	             );
}

static gboolean
plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *png_enc;
  GstCaps *raw_caps, *png_caps;

  /* create an elementfactory for the jpegdec element */
  png_enc = gst_element_factory_new("pngenc", GST_TYPE_PNGENC, &gst_pngenc_details);
  g_return_val_if_fail(png_enc != NULL, FALSE);

  raw_caps = raw_caps_factory ();
  png_caps = png_caps_factory ();

  /* register sink pads */
  pngenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
						       GST_PAD_ALWAYS,
						       raw_caps, NULL);
  gst_element_factory_add_pad_template (png_enc, pngenc_sink_template);
  

  /* register src pads */
  pngenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC,
					             GST_PAD_ALWAYS,
					             png_caps, NULL);
  gst_element_factory_add_pad_template (png_enc, pngenc_src_template);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (png_enc));
  
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "png",
  plugin_init
};
