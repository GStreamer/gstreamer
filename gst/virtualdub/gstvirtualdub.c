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
#include "gstvirtualdub.h"


struct _elements_entry
{
  gchar *name;
    GType (*type) (void);
  GstElementDetails *details;
    gboolean (*factoryinit) (GstElementFactory * factory);
};

static struct _elements_entry _elements[] = {
  {"xsharpen", gst_xsharpen_get_type, &gst_xsharpen_details, NULL},
  {NULL, 0},
};


GstPadTemplate *
gst_virtualdub_src_factory (void)
{
  static GstPadTemplate *templ = NULL;

  if (!templ) {
    templ = GST_PAD_TEMPLATE_NEW ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_CAPS_NEW ("virtualdub_src",
            "video/x-raw-rgb",
            "bpp", GST_PROPS_INT (32),
            "depth", GST_PROPS_INT (32),
            "endianness", GST_PROPS_INT (G_BYTE_ORDER),
            "red_mask", GST_PROPS_INT (0xff0000),
            "green_mask", GST_PROPS_INT (0xff00),
            "blue_mask", GST_PROPS_INT (0xff),
            "width", GST_PROPS_INT_RANGE (16, 4096),
            "height", GST_PROPS_INT_RANGE (16, 4096),
            "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT)
        )
        );
  }
  return templ;
}

GstPadTemplate *
gst_virtualdub_sink_factory (void)
{
  static GstPadTemplate *templ = NULL;

  if (!templ) {
    templ = GST_PAD_TEMPLATE_NEW ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_CAPS_NEW ("virtualdub_sink",
            "video/x-raw-rgb",
            "bpp", GST_PROPS_INT (32),
            "depth", GST_PROPS_INT (32),
            "endianness", GST_PROPS_INT (G_BYTE_ORDER),
            "red_mask", GST_PROPS_INT (0xff0000),
            "green_mask", GST_PROPS_INT (0xff00),
            "blue_mask", GST_PROPS_INT (0xff),
            "width", GST_PROPS_INT_RANGE (16, 4096),
            "height", GST_PROPS_INT_RANGE (16, 4096),
            "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT)
        )
        );
  }
  return templ;
}

static gboolean
plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *factory;
  gint i = 0;

  while (_elements[i].name) {
    factory = gst_element_factory_new (_elements[i].name,
        (_elements[i].type) (), _elements[i].details);

    if (!factory) {
      g_warning ("gst_virtualdub_new failed for `%s'", _elements[i].name);
      continue;
    }
    gst_element_factory_add_pad_template (factory,
        gst_virtualdub_src_factory ());
    gst_element_factory_add_pad_template (factory,
        gst_virtualdub_sink_factory ());

    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    if (_elements[i].factoryinit) {
      _elements[i].factoryinit (factory);
    }
    i++;
  }

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "virtualdub",
  plugin_init
};
