#include <gst/gst.h>

static GList* 
autoplug_factories (gchar *factory1, gchar *factory2) 
{
  GstElementFactory *mp3parse, *audiosink;
  mp3parse = gst_elementfactory_find ("mpeg1parse");
  g_assert (mp3parse != NULL);

  audiosink = gst_elementfactory_find ("videosink");
  g_assert (audiosink != NULL);

  return gst_autoplug_factories (mp3parse, audiosink);
}

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

  factories = autoplug_factories ("mpeg1parse", "videosink");
  dump_factories (factories);

  factories = autoplug_caps ("audio/mp3", "audio/raw");
  dump_factories (factories);

  factories = autoplug_caps ("video/mpeg", "audio/raw");
  dump_factories (factories);
}
