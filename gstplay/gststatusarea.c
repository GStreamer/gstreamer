#include <config.h>
#include "gststatusarea.h"

static void gst_status_area_class_init (GstStatusAreaClass *klass);
static void gst_status_area_init       (GstStatusArea *status_area);

static void gst_status_area_set_arg    (GtkObject *object, GtkArg *arg, guint id);
static void gst_status_area_get_arg    (GtkObject *object, GtkArg *arg, guint id);

static void gst_status_area_realize    (GtkWidget *status_area);
static gint gst_status_area_expose     (GtkWidget *widget, GdkEventExpose *event);

#define DEFAULT_HEIGHT 20
#define DEFAULT_EXPANDED_HEIGHT 100

/* signals and args */
enum {
	LAST_SIGNAL
};

enum {
	ARG_0,
};

static GtkDrawingArea *parent_class = NULL;
//static guint gst_status_area_signals[LAST_SIGNAL] = { 0 };

GtkType 
gst_status_area_get_type (void) 
{
	static GtkType status_area_type = 0;

	if (!status_area_type) {
		static const GtkTypeInfo status_area_info = {
			"GstStatusArea",
			sizeof(GstStatusArea),
			sizeof(GstStatusAreaClass),
			(GtkClassInitFunc)gst_status_area_class_init,
			(GtkObjectInitFunc)gst_status_area_init,
			NULL,
			NULL,
			(GtkClassInitFunc)NULL,
		};
		status_area_type = gtk_type_unique (gtk_widget_get_type (),&status_area_info);
	}
	return status_area_type;
}

static void 
gst_status_area_class_init (GstStatusAreaClass *klass) 
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;

	parent_class = gtk_type_class (gtk_widget_get_type ());

	object_class = (GtkObjectClass*)klass;
	widget_class = (GtkWidgetClass*)klass;

	object_class->set_arg = gst_status_area_set_arg;
	object_class->get_arg = gst_status_area_get_arg;

	widget_class->realize = gst_status_area_realize; 
	widget_class->expose_event = gst_status_area_expose; 
}

static void 
gst_status_area_init (GstStatusArea *status_area) 
{
	GTK_WIDGET(status_area)->requisition.height = DEFAULT_HEIGHT;

	status_area->state = GST_STATUS_AREA_STATE_INIT;
	status_area->expanded = FALSE;
}

GstStatusArea *
gst_status_area_new (void) 
{
	return GST_STATUS_AREA (gtk_type_new (GST_TYPE_STATUS_AREA));
}

static void 
gst_status_area_realize (GtkWidget *widget) 
{
	GdkWindowAttr attributes;
	gint attributes_mask;
	GstStatusArea *status_area;

	g_return_if_fail (widget != NULL);
	g_return_if_fail(GST_IS_STATUS_AREA(widget));

	GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);
	status_area = GST_STATUS_AREA(widget);

	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = widget->allocation.width;
	attributes.height = widget->allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.event_mask = gtk_widget_get_events (widget)
		| GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
		| GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK;

	attributes.visual = gtk_widget_get_visual (widget);
	attributes.colormap = gtk_widget_get_colormap (widget);

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
	widget->window = gdk_window_new (widget->parent->window, &attributes, attributes_mask);

	widget->style = gtk_style_attach (widget->style, widget->window);
}

static gint 
gst_status_area_expose(GtkWidget *widget, GdkEventExpose *event) 
{
	GstStatusArea *status_area;
	guchar *statustext;

	g_return_val_if_fail (GST_IS_STATUS_AREA (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	status_area = GST_STATUS_AREA (widget);

	if (GTK_WIDGET_VISIBLE (widget) && GTK_WIDGET_MAPPED (widget)) {
		gdk_draw_rectangle (widget->window,
				    widget->style->black_gc,
				    TRUE, 
				    0, 0,
				    widget->allocation.width,
				    widget->allocation.height);

		if (status_area->expanded) {
			gint width;
      
			gdk_draw_line (widget->window,
				       widget->style->dark_gc[0],
				       0, widget->allocation.height - 20,
				       widget->allocation.width, widget->allocation.height - 20);

			width = gdk_string_width (widget->style->font, "Show:");

			gdk_draw_string (widget->window,
					 widget->style->font,
					 widget->style->white_gc, 
					 80-width, 15,
					 "Show:");

			width = gdk_string_width (widget->style->font, "Clip:");

			gdk_draw_string (widget->window,
					 widget->style->font,
					 widget->style->white_gc, 
					 80-width, 40,
					 "Clip:");

			width = gdk_string_width (widget->style->font, "Author:");

			gdk_draw_string (widget->window,
					 widget->style->font,
					 widget->style->white_gc, 
					 80-width, 55,
					 "Author:");

			width = gdk_string_width (widget->style->font, "Copyright:");

			gdk_draw_string (widget->window,
					 widget->style->font,
					 widget->style->white_gc, 
					 80-width, 70,
					 "Copyright:");

			gdk_draw_line (widget->window,
				       widget->style->dark_gc[0],
				       0, widget->allocation.height - 80,
				       widget->allocation.width, widget->allocation.height - 80);




		}

		switch (status_area->state) {
		case GST_STATUS_AREA_STATE_INIT:
			statustext = "Initializing";
			break;
		case GST_STATUS_AREA_STATE_PLAYING:
			statustext = "Playing";
			break;
		case GST_STATUS_AREA_STATE_PAUSED:
			statustext = "Paused";
			break;
		case GST_STATUS_AREA_STATE_STOPPED:
			statustext = "Stopped";
			break;
		default:
			statustext = "";
			break;
		}

		gdk_draw_string (widget->window,
				 widget->style->font,
				 widget->style->white_gc, 
				 8, widget->allocation.height-5,
				 statustext);

		if (status_area->playtime) {
			gint width = gdk_string_width (widget->style->font, status_area->playtime);

			gdk_draw_string (widget->window,
					 widget->style->font,
					 widget->style->white_gc, 
					 widget->allocation.width-width-20, 
					 widget->allocation.height-5, 
					 status_area->playtime);
		}
	}

	return FALSE;
}

void 
gst_status_area_set_state (GstStatusArea *area, GstStatusAreaState state) 
{
	g_return_if_fail(area != NULL);
	g_return_if_fail(GST_IS_STATUS_AREA(area));

	area->state = state;

	if (GTK_WIDGET_VISIBLE(area)) 
		gtk_widget_queue_draw(GTK_WIDGET(area));
}

void            
gst_status_area_set_playtime (GstStatusArea *area, const guchar *time)
{
	g_return_if_fail(area != NULL);
	g_return_if_fail(GST_IS_STATUS_AREA(area));
	g_return_if_fail(time != NULL);
 
	if (area->playtime) g_free (area->playtime);
  
	area->playtime = g_strdup (time);

	if (GTK_WIDGET_VISIBLE(area)) 
		gtk_widget_queue_draw(GTK_WIDGET(area));
}

void 
gst_status_area_set_streamtype (GstStatusArea *area, const guchar *type) 
{
}

void 
gst_status_area_show_extended (GstStatusArea *area, gboolean show)
{
	area->expanded = show;

	if (show) {
		GTK_WIDGET(area)->requisition.height = DEFAULT_EXPANDED_HEIGHT;
	}
	else {
		GTK_WIDGET(area)->requisition.height = DEFAULT_HEIGHT;
	}
	gtk_widget_queue_resize (GTK_WIDGET (area));
}

static void 
gst_status_area_set_arg(GtkObject *object, GtkArg *arg, guint id) 
{
	GstStatusArea *status_area;

	status_area = GST_STATUS_AREA(object);

	switch (id) {
	default:
		g_warning("GstStatusArea: unknown arg!");
		break;
	}
}

static void 
gst_status_area_get_arg(GtkObject *object, GtkArg *arg, guint id) 
{
	GstStatusArea *status_area;

	status_area = GST_STATUS_AREA(object);

	switch (id) {
	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}
