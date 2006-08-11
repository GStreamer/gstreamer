#include <glib/gerror.h>
#include <gst/gstmessage.h>
#include <glib/gquark.h>

gchar *
gstsharp_message_parse_error(GstMessage *message)
{
    GError *gerror;
    gchar *error;

    gst_message_parse_error(message, &gerror, NULL);

    error = g_strdup(gerror->message);
    g_error_free(gerror);
    
    return error;
}

GError *
gstsharp_message_error_new()
{
	GQuark domain = g_quark_from_string ("test");
	return g_error_new (domain, 10, "test error");
}
