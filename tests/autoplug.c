#include <gst/gst.h>

static GList* 
autoplug_caps (gchar *mime1, gchar *mime2) 
{
  GstCaps *caps1, *caps2;

  caps1 = gst_caps_new (mime1);
  caps2 = gst_caps_new (mime2);

  return gst_autoplug_caps (caps1, caps2);
}

static void
dump_factories (GList *factories) 
{
  g_print ("dumping factories\n");

  while (factories) {
    GstElementFactory *factory = (GstElementFactory *)factories->data;

    g_print ("factory: \"%s\"\n", factory->name);

    factories = g_list_next (factories);
  }
}

int main(int argc,char *argv[]) 
{
  GList *factories;

  gst_init(&argc,&argv);

  factories = autoplug_caps ("audio/mp3", "audio/raw");
  dump_factories (factories);

  factories = autoplug_caps ("video/mpeg", "audio/raw");
  dump_factories (factories);

  factories = gst_autoplug_caps (
		  gst_caps_new_with_props(
			  "video/mpeg",
			  gst_props_new ( 
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (TRUE),
			      NULL)),
		  gst_caps_new("audio/raw"));
  dump_factories (factories);

  factories = gst_autoplug_caps (
		  gst_caps_new_with_props(
			  "video/mpeg",
			  gst_props_new ( 
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (FALSE),
			      NULL)),
		  gst_caps_new("video/raw"));
  dump_factories (factories);
}
