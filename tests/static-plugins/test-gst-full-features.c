#include <gst/gst.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


void
assert_feature_names (gchar * names, GType feature_type, gboolean spook)
{
  GstPluginFeature *feature = NULL;
  gchar **split = NULL;
  int i;

  if (names)
    split = g_strsplit (names, ",", 0);
  if (split) {
    for (i = 0; split[i]; i++) {
      feature = gst_registry_find_feature (gst_registry_get (),
          split[i], feature_type);
      if (spook) {
        g_assert_null (feature);
      } else {
        g_assert_nonnull (feature);
        g_assert_cmpstr (gst_plugin_feature_get_plugin_name (feature), ==,
            GST_PLUGIN_FULL_FEATURES_NAME);
      }

      if (feature)
        gst_object_unref (feature);
    }
    g_strfreev (split);
  }
}

int
main (int argc, char *argv[])
{
  GOptionContext *ctx;
  GError *err = NULL;
  gchar *elements, *typefinds, *deviceproviders, *dynamictypes;
  gchar *spook_elements, *spook_typefinds, *spook_deviceproviders,
      *spook_dynamictypes;

  elements = typefinds = deviceproviders = dynamictypes = NULL;
  spook_elements = spook_typefinds = spook_deviceproviders =
      spook_dynamictypes = NULL;

  GOptionEntry options[] = {
    {"elements", 'e', 0, G_OPTION_ARG_STRING, &elements,
          "Element(s) which should be available. Specify multiple ones using ',' as separator",
        NULL},
    {"spook-elements", 'E', 0, G_OPTION_ARG_STRING, &spook_elements,
          "Element(s) which should NOT be available. Specify multiple ones using ',' as separator",
        NULL},
    {"typefinds", 't', 0, G_OPTION_ARG_STRING, &typefinds,
          "Typefind(s) which should be available. Specify multiple ones using ',' as separator",
        NULL},
    {"spook-typefinds", 'T', 0, G_OPTION_ARG_STRING, &spook_typefinds,
          "Typefind(s) which should NOT be available. Specify multiple ones using ',' as separator",
        NULL},
    {"deviceproviders", 'd', 0, G_OPTION_ARG_STRING, &deviceproviders,
          "Deviceprovider(s) which should be available. Specify multiple ones using ',' as separator",
        NULL},
    {"spook-deviceproviders", 'D', 0, G_OPTION_ARG_STRING,
          &spook_deviceproviders,
          "Deviceprovider(s) which should NOT be available. Specify multiple ones using ',' as separator",
        NULL},
    {"dynamictypes", 'l', 0, G_OPTION_ARG_STRING, &dynamictypes,
          "Dynamictype(s) which should be available. Specify multiple ones using ',' as separator",
        NULL},
    {"spook-dynamictypes", 'L', 0, G_OPTION_ARG_STRING, &spook_dynamictypes,
          "Dynamictype(s) which should NOT be available. Specify multiple ones using ',' as separator",
        NULL},
    {NULL}
  };
  ctx = g_option_context_new ("elements ...");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    g_clear_error (&err);
    g_option_context_free (ctx);
    return 1;
  }
  g_option_context_free (ctx);

  gst_init (&argc, &argv);

  /* Test that elements are instanciable. */
  assert_feature_names (elements, GST_TYPE_ELEMENT_FACTORY, FALSE);
  /* Test that elements are NOT instanciable. */
  assert_feature_names (spook_elements, GST_TYPE_ELEMENT_FACTORY, TRUE);

  /* Test that typefinds are instanciable. */
  assert_feature_names (typefinds, GST_TYPE_TYPE_FIND_FACTORY, FALSE);
  /* Test that typefinds are NOT instanciable. */
  assert_feature_names (spook_typefinds, GST_TYPE_TYPE_FIND_FACTORY, TRUE);

  /* Test that device providers are instanciable. */
  assert_feature_names (deviceproviders, GST_TYPE_DEVICE_PROVIDER_FACTORY,
      FALSE);
  /* Test that device providers are NOT instanciable. */
  assert_feature_names (spook_deviceproviders, GST_TYPE_DEVICE_PROVIDER_FACTORY,
      TRUE);

  /* Test that dynamic types are instanciable. */
  assert_feature_names (dynamictypes, GST_TYPE_DYNAMIC_TYPE_FACTORY, FALSE);
  /* Test that dynamic types are NOT instanciable. */
  assert_feature_names (spook_dynamictypes, GST_TYPE_DYNAMIC_TYPE_FACTORY,
      TRUE);

  gst_deinit ();

  return 0;
}
