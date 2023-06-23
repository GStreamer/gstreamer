/*
 * GStreamer
 * Copyright (C) 2023 Arnaud Rebillout <elboulangero@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

// gcc gtkhttpsrc.c $(pkg-config --cflags --libs gstreamer-1.0 gtk+-3.0)

#include <gtk/gtk.h>
#include <gst/gst.h>

//#define DEBUG_GST_BUS_MESSAGE_ELEMENT

static GstElement *playbin;

static gboolean accept_bad_certificates = FALSE;        // whether we accept bad certs
static gchar *stream_uri;       // the stream we want to play
static gchar *redirection_uri;  // set if there was a redirection
static gchar *cert_errors;      // errors with the certificate

/*
 * Helpers
 */

static gchar *
tls_errors_to_string (GTlsCertificateFlags errors)
{
  GPtrArray *a;
  gchar *res;

  a = g_ptr_array_new_full (2, NULL);

  if (errors & G_TLS_CERTIFICATE_UNKNOWN_CA)
    g_ptr_array_add (a, (gchar *) "unknown-ca");
  if (errors & G_TLS_CERTIFICATE_BAD_IDENTITY)
    g_ptr_array_add (a, (gchar *) "bad-identity");
  if (errors & G_TLS_CERTIFICATE_NOT_ACTIVATED)
    g_ptr_array_add (a, (gchar *) "not-activated");
  if (errors & G_TLS_CERTIFICATE_EXPIRED)
    g_ptr_array_add (a, (gchar *) "expired");
  if (errors & G_TLS_CERTIFICATE_REVOKED)
    g_ptr_array_add (a, (gchar *) "revoked");
  if (errors & G_TLS_CERTIFICATE_INSECURE)
    g_ptr_array_add (a, (gchar *) "insecure");

  if (a->len > 0) {
    g_ptr_array_add (a, NULL);
    res = g_strjoinv (", ", (gchar **) a->pdata);
  } else
    res = g_strdup ("unknown error");

  g_ptr_array_free (a, TRUE);

  return res;
}

static void
start_playback (void)
{
  g_print ("Start playback: %s\n", stream_uri);
  g_object_set (playbin, "uri", stream_uri, NULL);
  gst_element_set_state (GST_ELEMENT (playbin), GST_STATE_PLAYING);
}

static void
stop_playback (void)
{
  g_print ("Stop playback\n");
  gst_element_set_state (GST_ELEMENT (playbin), GST_STATE_NULL);
}

/*
 * Main window - Enter the URL of the stream and play it
 */

static GtkWidget *main_window;

static void
button_cb (GtkWidget * widget, GtkEntry * entry)
{
  const gchar *uri;

  uri = gtk_entry_get_text (entry);

  // quick check - the purpose of this example is to play web radios
  if (g_str_has_prefix (uri, "http") == FALSE) {
    g_print ("Invalid entry, must start with 'http'\n");
    return;
  }
  // only one click allowed - this is just an example!
  gtk_widget_set_sensitive (widget, FALSE);

  // save in global variable
  stream_uri = g_strdup (uri);

  start_playback ();
}

static void
show_main_window (void)
{
  GtkWidget *window;
  GtkWidget *grid;
  GtkWidget *entry;
  GtkWidget *button;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (window), 640, -1);

  grid = gtk_grid_new ();
  gtk_container_add (GTK_CONTAINER (window), grid);

  entry = gtk_entry_new ();
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 0, 0, 1, 1);

  button = gtk_button_new_with_label ("> Play");
  gtk_grid_attach (GTK_GRID (grid), button, 0, 1, 1, 1);

  g_signal_connect (button, "clicked", G_CALLBACK (button_cb), entry);
  g_signal_connect (window, "delete-event", gtk_main_quit, NULL);

  gtk_widget_show_all (window);

  // save in global variable
  main_window = window;
}

/*
 * Dialog - Whether to play a stream when the certifiate is invalid
 */

static GtkWidget *dialog;

static void
button_yes_cb (GtkWidget * widget, gpointer data)
{
  accept_bad_certificates = TRUE;
  start_playback ();
  gtk_widget_destroy (dialog);
  dialog = NULL;
}

static void
show_dialog (void)
{
  GtkWidget *window;
  GtkWidget *grid;
  GtkWidget *box;
  GtkWidget *label;
  GtkWidget *button;
  gchar *str;

  // BEWARE! We can't gtk_dialog_run(), which would block the world, and
  // prevent us from receiving other signals from GStreamer. In particular,
  // if there was a redirection, we might not know it at this point.

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (main_window));

  grid = gtk_grid_new ();
  gtk_container_add (GTK_CONTAINER (window), grid);

  str = g_strdup_printf ("Bad certificate: %s", cert_errors);
  label = gtk_label_new (str);
  g_free (str);
  gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

  str = g_strdup_printf ("Stream URI: %s", stream_uri);
  label = gtk_label_new (str);
  g_free (str);
  gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);

  str = g_strdup_printf ("Redirection URI: %s", redirection_uri ?
      redirection_uri : "not redirected");
  label = gtk_label_new (str);
  g_free (str);
  gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);

  label = gtk_label_new ("Play the stream anyway?");
  gtk_grid_attach (GTK_GRID (grid), label, 0, 3, 2, 1);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_grid_attach (GTK_GRID (grid), box, 0, 4, 1, 1);

  button = gtk_button_new_with_label ("No");
  g_signal_connect (button, "clicked", G_CALLBACK (gtk_main_quit), NULL);
  gtk_box_pack_start (GTK_BOX (box), button, TRUE, TRUE, 0);

  button = gtk_button_new_with_label ("Yes");
  g_signal_connect (button, "clicked", G_CALLBACK (button_yes_cb), NULL);
  gtk_box_pack_start (GTK_BOX (box), button, TRUE, TRUE, 0);

  gtk_widget_show_all (window);

  // save in global variable
  dialog = window;
}

static void
dialog_update (void)
{
  GtkWidget *grid;
  GtkWidget *label;
  gchar *str;

  if (dialog == NULL)
    return;

  str = g_strdup_printf ("Redirection URI: %s", redirection_uri ?
      redirection_uri : "not redirected");
  grid = gtk_bin_get_child (GTK_BIN (dialog));
  label = gtk_grid_get_child_at (GTK_GRID (grid), 0, 2);
  gtk_label_set_text (GTK_LABEL (label), str);
  g_free (str);
}

/*
 * GStreamer things
 */

static gboolean
idle_cb (gpointer user_data)
{
  show_dialog ();
  return G_SOURCE_REMOVE;
}

static gboolean
accept_certificate_cb (GstElement * source, GTlsCertificate * tls_certificate,
    GTlsCertificateFlags tls_errors, gpointer user_data)
{
  gchar *errors;

  errors = tls_errors_to_string (tls_errors);

  // save in global variables
  if (cert_errors)
    g_free (cert_errors);
  cert_errors = errors;

  g_print ("Bad certificate: %s - %s\n", errors,
      accept_bad_certificates ? "accepting" : "rejecting");

  // Inform user that the certificate is invalid, and ask what to do.
  // BEWARE! We're in the GStreamer streaming thread, we can't touch
  // the GUI!
  if (accept_bad_certificates == FALSE) {
    //show_dialog(); // <- can't do that!
    g_idle_add (idle_cb, NULL);
  }

  return accept_bad_certificates;
}

static void
source_setup_cb (GstElement * playbin, GstElement * source, gpointer user_data)
{
  const gchar *name = G_OBJECT_TYPE_NAME (source);

  if (g_signal_lookup ("accept-certificate", G_OBJECT_TYPE (source)) != 0) {
    g_print ("Source %s has signal accept-certificate - connecting\n", name);
    g_signal_connect (source, "accept-certificate",
        G_CALLBACK (accept_certificate_cb), 0);
  } else {
    g_print ("Source %s does NOT have signal accept-certificate\n", name);
  }
}

static void
message_element_cb (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  const GstStructure *s;
  const gchar *name;
  const gchar *uri;

  s = gst_message_get_structure (msg);
  if (s == NULL)
    return;

  name = gst_structure_get_name (s);
  if (g_strcmp0 (name, "http-headers") != 0)
    return;

#ifdef DEBUG_GST_BUS_MESSAGE_ELEMENT
  gst_println ("%" GST_PTR_FORMAT, s);
#endif

  if (gst_structure_has_field (s, "redirection-uri") == FALSE)
    return;

  uri = gst_structure_get_string (s, "redirection-uri");
  g_print ("Redirected to: %s\n", uri);

  // save in global variable
  if (redirection_uri)
    g_free (redirection_uri);
  redirection_uri = g_strdup (uri);

  dialog_update ();
}

static void
message_error_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GError *err;
  gchar *debug;

  gst_message_parse_error (message, &err, &debug);

  g_print ("Got error! ---------\n");
  g_print ("  error code: %s: %d\n", g_quark_to_string (err->domain),
      err->code);
  g_print ("  message   : %s\n", err->message);
  g_print ("  debug     : %s\n", debug);
  g_print ("--------------------\n");

  stop_playback ();

  g_free (debug);
  g_error_free (err);
}

static void
setup_playback (void)
{
  GstBus *bus;

  playbin = gst_element_factory_make ("playbin", "playbin");
  g_signal_connect (playbin, "source-setup",
      G_CALLBACK (source_setup_cb), NULL);

  bus = gst_element_get_bus (playbin);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::element",
      G_CALLBACK (message_element_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (message_error_cb), NULL);
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  setup_playback ();
  show_main_window ();

  gtk_main ();

  stop_playback ();
  gst_deinit ();

  return 0;
}
