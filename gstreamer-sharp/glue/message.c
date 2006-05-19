
#include <glib/gerror.h>
#include <gst/gstmessage.h>

gchar* gstsharp_message_parse_error (GstMessage *message)
{
	GError *gerror;
	gchar *error;

	gst_message_parse_error (message, &gerror, NULL);

	error = g_strdup (gerror->message);
	g_error_free (gerror);
	return error;
}
