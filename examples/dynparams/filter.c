/*
 * filter.c
 *
 * demo application for filters using dparams
 * dparams get their sliders which you can play with
 *
 * you can also enter an input and output part of a pipeline
 */

#include <string.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/control/control.h>

/* filter UI data */
struct _filter_ui
{
  GtkWidget *window;            /* top-level interface window */

  GtkWidget *buttons;           /* all of the control buttons */
  GtkWidget *parse, *play, *stop;       /* control buttons */

  GtkWidget *feedback;          /* here's where we'll tell you stuff */
  GtkTextBuffer *fb_buffer;     /* feedback buffer */
  GtkWidget *selection;         /* the place to input element stuff */
  GtkWidget *input, *filter, *output;   /* the selection widgets */

  GtkWidget *control;           /* the dynamically generated control UI */
};

typedef struct _filter_ui _filter_ui_t;

/* back-end data */
struct _filter_data
{
  _filter_ui_t *ui;             /* the UI data */
  gchar *input_pipe, *output_pipe, *filter_element;
  gchar *pipe_string;
  GList *filter_choices;

  gboolean playing;
  GstElement *input, *output;   /* these are in and out bins */
  GstElement *pipeline;
  GstElement *filter;
};

typedef struct _filter_data _filter_data_t;

/* internal prototypes when they can't be avoided */
void cb_remove_and_destroy (GtkWidget * widget, gpointer user_data);

//void  cb_dynparm_value_changed (GtkWidget *widget, gpointer user_data);
void cb_dynparm_value_changed (GtkRange * range, GstDParam * dparam);

/* GStreamer helper functions go here */

/* go through a bin, finding the one pad that is unconnected in the given
 * direction, and return a ghost pad */
GstPad *
gst_bin_find_unconnected_pad (GstBin * bin, GstPadDirection direction,
    gchar * name)
{
  GstPad *pad = NULL;
  GList *elements = NULL;
  const GList *pads = NULL;
  GstElement *element = NULL;

  g_print ("DEBUG: find_unconnected start\n");
  elements = (GList *) gst_bin_get_list (bin);
  /* traverse all elements looking for unconnected pads */
  while (elements && pad == NULL) {
    element = GST_ELEMENT (elements->data);
    g_print ("DEBUG: looking in element %s\n", gst_element_get_name (element));
    pads = gst_element_get_pad_list (element);
    while (pads) {
      /* check if the direction matches */
      if (GST_PAD_DIRECTION (GST_PAD (pads->data)) == direction) {
        if (GST_PAD_PEER (GST_PAD (pads->data)) == NULL) {
          /* found it ! */
          g_print ("DEBUG: found an unconnected pad !\n");
          pad = GST_PAD (pads->data);
        }
      }
      if (pad)
        break;                  /* found one already */
      pads = g_list_next (pads);
    }
    elements = g_list_next (elements);
  }

  g_print ("DEBUG: find_unconnected stop\n");
  if (pad == NULL)              /* we didn't find it at all */
    return NULL;

  pad = gst_ghost_pad_new (name, pad);
  return pad;
}

void
ui_feedback_add_text (_filter_ui_t * ui, const gchar * text)
{
  GtkTextIter iter;

  gtk_text_buffer_get_end_iter (ui->fb_buffer, &iter);
  gtk_text_buffer_insert (ui->fb_buffer, &iter, text, -1);
}

void
ui_feedback_add (_filter_ui_t * ui, const gchar * format, ...)
{
  va_list args;
  gchar *buffer = NULL;

  va_start (args, format);
  buffer = g_strdup_vprintf (format, args);
  va_end (args);
  ui_feedback_add_text (ui, buffer);
  g_free (buffer);
}

void
ui_feedback_clear (_filter_ui_t * ui)
{
  gtk_text_buffer_set_text (ui->fb_buffer, "", 0);
}

/* create the control widget using the element's dynparams 
 * control is a vbox which we need to empty first */
void
ui_control_create (GstElement * element, GtkWidget * control, _filter_ui_t * ui)
{
  GtkWidget *hbox = NULL;
  GtkWidget *widget = NULL;
  GstDParamManager *dpman = NULL;
  GstDParam *dparam = NULL;
  GParamSpec **specs = NULL;
  int i = 0;

  g_assert (GTK_IS_VBOX (control));

  /* empty control vbox */
  g_print ("DEBUG: emptying control widget\n");
  gtk_container_foreach (GTK_CONTAINER (control), cb_remove_and_destroy,
      (gpointer) control);

  g_print ("DEBUG: adding label to control widget\n");
  widget = gtk_label_new ("Dynamic Parameters");
  gtk_container_add (GTK_CONTAINER (control), widget);
  gtk_widget_show (widget);

  if ((dpman = gst_dpman_get_manager (element))) {
    ui_feedback_add (ui, "Found Dynamic Parameters on filter element.\n");
    specs = gst_dpman_list_dparam_specs (dpman);
    for (i = 0; specs[i] != NULL; ++i) {
      hbox = gtk_hbox_new (FALSE, 0);
      widget = gtk_label_new (g_param_spec_get_name (specs[i]));
      gtk_container_add (GTK_CONTAINER (hbox), widget);
      gtk_widget_show (widget);
      switch (G_PARAM_SPEC_VALUE_TYPE (specs[i])) {
        case G_TYPE_INT64:
          widget = gtk_hscale_new_with_range (
              (gdouble) (((GParamSpecInt64 *) specs[i])->minimum),
              (gdouble) (((GParamSpecInt64 *) specs[i])->maximum), 1.0);
          gtk_range_set_value (GTK_RANGE (widget),
              (gdouble) ((GParamSpecInt64 *) specs[i])->default_value);
          break;

        case G_TYPE_INT:
          widget = gtk_hscale_new_with_range (
              (gdouble) (((GParamSpecInt *) specs[i])->minimum),
              (gdouble) (((GParamSpecInt *) specs[i])->maximum), 1.0);
          gtk_range_set_value (GTK_RANGE (widget),
              (gdouble) ((GParamSpecInt *) specs[i])->default_value);
          break;
        case G_TYPE_FLOAT:
          widget = gtk_hscale_new_with_range (
              (gdouble) (((GParamSpecFloat *) specs[i])->minimum),
              (gdouble) (((GParamSpecFloat *) specs[i])->maximum), 0.00001);
          gtk_range_set_value (GTK_RANGE (widget),
              (gdouble) ((GParamSpecFloat *) specs[i])->default_value);
          break;
      }
      /* create the dparam object */
      dparam = gst_dpsmooth_new (G_PARAM_SPEC_VALUE_TYPE (specs[i]));
      g_object_set (G_OBJECT (dparam), "update_period", 2000000LL, NULL);
      g_assert (gst_dpman_attach_dparam (dpman,
              (gchar *) g_param_spec_get_name (specs[i]), dparam));
      gst_dpman_set_mode (dpman, "asynchronous");
      g_signal_connect (widget, "value-changed",
          G_CALLBACK (cb_dynparm_value_changed), dparam);
      cb_dynparm_value_changed (GTK_RANGE (widget), dparam);

      gtk_container_add (GTK_CONTAINER (hbox), widget);
      gtk_widget_show (widget);
    }
    gtk_container_add (GTK_CONTAINER (control), hbox);
    gtk_widget_show (hbox);
  }
}

/* all the pretty callbacks gather here please */
void
cb_remove_and_destroy (GtkWidget * widget, gpointer user_data)
{
  GtkContainer *container = GTK_CONTAINER (user_data);

  g_print ("DEBUG: trying to remove widget from a container.\n");
  gtk_container_remove (container, widget);
  gtk_widget_destroy (widget);
}

/* when the scale associated with a dparam changes, respond */
void
cb_dynparm_value_changed (GtkRange * range, GstDParam * dparam)
{
  /*
     GstDParam *dparam = GST_DPARAM (user_data);
     GtkHScale *adj = GTK_HSCALE (widget);
   */
  gdouble value = 0.0;

  g_assert (GST_IS_DPARAM (dparam));
  g_assert (GTK_IS_RANGE (range));

  value = gtk_range_get_value (range);

  g_print ("DEBUG: setting value to %f\n", value);

  switch (G_PARAM_SPEC_VALUE_TYPE (GST_DPARAM_PARAM_SPEC (dparam))) {
    case G_TYPE_INT64:
      g_object_set (G_OBJECT (dparam), "value_int64", (gint64) value, NULL);
      break;

    case G_TYPE_INT:
      g_object_set (G_OBJECT (dparam), "value_int", (gint) value, NULL);
      break;

    case G_TYPE_FLOAT:
      g_object_set (G_OBJECT (dparam), "value_float", (gfloat) value, NULL);
      break;
  }
}


void
cb_entry_activate (GtkEntry * entry, gpointer user_data)
{
  g_print ("DEBUG: oi ! you activated an entry !\n");
  g_print ("DEBUG: pipeline: %s\n", gtk_entry_get_text (entry));
}

void
cb_play_clicked (GtkButton * button, gpointer * user_data)
{
  _filter_data_t *fd = (_filter_data_t *) user_data;

  g_return_if_fail (GST_IS_PIPELINE (fd->pipeline));
  if (GST_STATE (fd->pipeline) == GST_STATE_PLAYING) {
    ui_feedback_add (fd->ui, "Pipeline is already playing !\n");
    return;
  }
  gst_element_set_state (fd->pipeline, GST_STATE_PLAYING);
}

void
cb_stop_clicked (GtkButton * button, gpointer * user_data)
{
  _filter_data_t *fd = (_filter_data_t *) user_data;

  if (GST_STATE (fd->pipeline) != GST_STATE_PLAYING) {
    ui_feedback_add (fd->ui, "Pipeline is not playing !\n");
    return;
  }
  gst_element_set_state (fd->pipeline, GST_STATE_NULL);
}

void
cb_parse_clicked (GtkButton * button, gpointer * user_data)
{
  _filter_data_t *fd = (_filter_data_t *) user_data;
  GtkCombo *filter = GTK_COMBO (fd->ui->filter);
  GError *error = NULL;
  GstPad *src_pad, *sink_pad;

  g_print ("DEBUG: you pressed parse.\n");
  ui_feedback_clear (fd->ui);
  ui_feedback_add (fd->ui, "Parsing pipeline ...\n");
  if (fd->input_pipe)
    g_free (fd->input_pipe);
  if (fd->output_pipe)
    g_free (fd->output_pipe);
  if (fd->filter_element)
    g_free (fd->filter_element);
  fd->input_pipe = g_strdup_printf ("bin.( %s )",
      gtk_entry_get_text (GTK_ENTRY (fd->ui->input)));
  fd->output_pipe = g_strdup_printf ("bin.( %s )",
      gtk_entry_get_text (GTK_ENTRY (fd->ui->output)));
  /* gtkcombo.h says I can access the entry field directly */
  fd->filter_element =
      g_strdup (gtk_entry_get_text (GTK_ENTRY (filter->entry)));
  g_print ("Input pipeline :\t%s (%d)\n", fd->input_pipe,
      (int) strlen (fd->input_pipe));
  g_print ("Filter element :\t%s (%d)\n", fd->filter_element,
      (int) strlen (fd->filter_element));
  g_print ("Output pipeline :\t%s (%d)\n", fd->output_pipe,
      (int) strlen (fd->output_pipe));

  /* try to create in and out bins */
  if (strlen (fd->input_pipe) == 0) {
    ui_feedback_add (fd->ui, "Error : try setting an input pipe.\n");
    return;
  }
  if (fd->input)
    gst_object_unref (GST_OBJECT (fd->input));
  fd->input = GST_ELEMENT (gst_parse_launch (fd->input_pipe, &error));
  if (error) {
    ui_feedback_add (fd->ui, "Error : parsing input pipeline : %s\n",
        error->message);
    g_error_free (error);
    return;
  }

  if (strlen (fd->output_pipe) == 0) {
    ui_feedback_add (fd->ui, "Error : try setting an output pipe.\n");
    return;
  }
  if (fd->output)
    gst_object_unref (GST_OBJECT (fd->output));
  fd->output = GST_ELEMENT (gst_parse_launch (fd->output_pipe, &error));
  if (error) {
    ui_feedback_add (fd->ui, "Error : parsing output pipeline : %s\n",
        error->message);
    g_error_free (error);
    return;
  }

  /* try to create filter */
  if (fd->filter)
    gst_object_unref (GST_OBJECT (fd->filter));
  fd->filter = gst_element_factory_make (fd->filter_element, "filter");
  if (fd->filter == NULL) {
    ui_feedback_add (fd->ui, "Error : could not create element %s\n",
        fd->filter_element);
    return;
  }

  /* set up dynparam controls for filter */
  ui_control_create (fd->filter, fd->ui->control, fd->ui);

  /* create toplevel bin */
  fd->pipeline = gst_thread_new ("toplevel");

  /* add the players to it */
  gst_bin_add_many (GST_BIN (fd->pipeline),
      fd->input, fd->filter, fd->output, NULL);

  /* connect filter to input and output bin */
  src_pad = gst_bin_find_unconnected_pad (GST_BIN (fd->input), GST_PAD_SRC,
      "source");
  if (src_pad == NULL) {
    ui_feedback_add (fd->ui,
        "Error : could not find an unconnected source pad !\n");
    return;
  }
  sink_pad = gst_bin_find_unconnected_pad (GST_BIN (fd->output), GST_PAD_SINK,
      "sink");
  if (sink_pad == NULL) {
    ui_feedback_add (fd->ui,
        "Error : could not find an unconnected sink pad !\n");
    return;
  }
  gst_element_add_pad (fd->input, src_pad);
  gst_element_add_pad (fd->output, sink_pad);

  gst_element_link_many (fd->input, fd->filter, fd->output, NULL);

  if (fd->pipe_string)
    g_free (fd->pipe_string);
  fd->pipe_string = g_strdup_printf ("%s ! %s ! %s", fd->input_pipe,
      fd->filter_element, fd->output_pipe);
  g_print ("Pipeline : %s\n", fd->pipe_string);
  ui_feedback_add (fd->ui, "Complete parsed pipeline: %s\n", fd->pipe_string);


}

/* find out the list of choices for GStreamer filters */
GList *
get_filter_choices (void)
{
  GList *choices = NULL;

  choices = g_list_append (choices, "volume");
  choices = g_list_append (choices, "ladspa_lpf");
  choices = g_list_append (choices, "ladspa_hpf");

  return choices;
}

void
init_data (_filter_data_t * fd)
{
  fd->input_pipe = NULL;
  fd->output_pipe = NULL;
  fd->filter_element = NULL;
  fd->pipe_string = NULL;
  fd->filter_choices = get_filter_choices ();

  /* GStreamer stuff */
  fd->input = NULL;
  fd->output = NULL;
  fd->filter = NULL;
  fd->pipeline = NULL;
  fd->playing = FALSE;
}

void
create_ui (_filter_ui_t * fui, _filter_data_t * fd)
{
  GtkWidget *widget;            /* temporary widget */
  GtkWidget *vbox;              /* temporary vbox */

  g_print ("DEBUG: creating top-level window\n");
  fui->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  widget = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (fui->window), widget);
  gtk_window_set_title (GTK_WINDOW (fui->window), "Gee, I can set titles too");

  /* top level window division */
  g_print ("DEBUG: creating top-level window contents\n");
  fui->buttons = gtk_hbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (widget), fui->buttons);
  fui->feedback = gtk_text_view_new ();
  gtk_container_add (GTK_CONTAINER (widget), fui->feedback);
  fui->selection = gtk_hbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (widget), fui->selection);
  fui->control = gtk_vbox_new (TRUE, 5);
  gtk_container_add (GTK_CONTAINER (widget), fui->control);

  /* button widget */
  fui->parse = gtk_button_new_with_label ("Parse");
  gtk_container_add (GTK_CONTAINER (fui->buttons), fui->parse);
  g_signal_connect (G_OBJECT (fui->parse), "clicked",
      G_CALLBACK (cb_parse_clicked), fd);
  fui->play = gtk_button_new_with_label ("Play");
  gtk_container_add (GTK_CONTAINER (fui->buttons), fui->play);
  g_signal_connect (G_OBJECT (fui->play), "clicked",
      G_CALLBACK (cb_play_clicked), fd);
  fui->stop = gtk_button_new_with_label ("Stop");
  gtk_container_add (GTK_CONTAINER (fui->buttons), fui->stop);
  g_signal_connect (G_OBJECT (fui->stop), "clicked",
      G_CALLBACK (cb_stop_clicked), fd);

  /* feedback widget */
  fui->fb_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (fui->feedback));
  gtk_text_buffer_set_text (fui->fb_buffer,
      "Hello, and welcome to the GStreamer filter demo app !\n"
      "I'll be your feedback window for today.\n", -1);

  /* selection widget */
  vbox = gtk_vbox_new (FALSE, 0);
  widget = gtk_label_new ("Input Pipe");
  gtk_container_add (GTK_CONTAINER (vbox), widget);
  fui->input = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (fui->input), "sinesrc");
  gtk_container_add (GTK_CONTAINER (vbox), fui->input);
  gtk_container_add (GTK_CONTAINER (fui->selection), vbox);
  g_signal_connect (G_OBJECT (fui->input), "activate",
      G_CALLBACK (cb_entry_activate), NULL);

  vbox = gtk_vbox_new (FALSE, 0);
  widget = gtk_label_new ("Filter");
  gtk_container_add (GTK_CONTAINER (vbox), widget);
  fui->filter = gtk_combo_new ();
  gtk_combo_set_popdown_strings (GTK_COMBO (fui->filter), fd->filter_choices);
  gtk_container_add (GTK_CONTAINER (vbox), fui->filter);
  gtk_container_add (GTK_CONTAINER (fui->selection), vbox);

  vbox = gtk_vbox_new (FALSE, 0);
  widget = gtk_label_new ("Output Pipe");
  gtk_container_add (GTK_CONTAINER (vbox), widget);
  fui->output = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (fui->output), "osssink fragment=1572872");     /* fixme: gconf default ? */
  gtk_container_add (GTK_CONTAINER (vbox), fui->output);
  gtk_container_add (GTK_CONTAINER (fui->selection), vbox);
  g_signal_connect (G_OBJECT (fui->output), "activate",
      G_CALLBACK (cb_entry_activate), NULL);

  /* control widget is dynamically generated */
  /*
     g_print ("DEBUG: labeling control area.\n");
     widget = gtk_label_new ("This is the big control area.");
     gtk_container_add (GTK_CONTAINER (fui->control), widget);
   */
}


int
main (int argc, char *argv[])
{
  _filter_data_t filter_data;
  _filter_ui_t filter_ui;


  gtk_init (&argc, &argv);
  gst_init (&argc, &argv);
  gst_control_init (&argc, &argv);

  init_data (&filter_data);
  filter_data.ui = &filter_ui;

  create_ui (&filter_ui, &filter_data);
  gtk_widget_show_all (filter_ui.window);

  gtk_main ();

  return 0;
}
