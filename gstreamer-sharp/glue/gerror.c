#include <glib.h>

gchar *
gstsharp_g_error_get_message (GError * error)
{
  if (error->message)
    return g_strdup (error->message);

  return NULL;
}

