
#include <gmodule.h>


int
main (int argc, char *argv[])
{
  GModule *module;
  void (*symbol) (void);
  gboolean ret;

  module = g_module_open (".libs/libloadgst.so",
      G_MODULE_BIND_LOCAL | G_MODULE_BIND_LAZY);
  g_assert (module != NULL);

  ret = g_module_symbol (module, "gst_init", (gpointer *) & symbol);
  g_print ("'gst_init' is %s\n", ret ? "visible" : "not visible");

  ret = g_module_symbol (module, "do_test", (gpointer *) & symbol);
  g_assert (ret);

  symbol ();

  exit (0);
}
