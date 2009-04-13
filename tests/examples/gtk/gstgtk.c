
#include "gstgtk.h"

#if defined(GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#elif defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#elif defined(GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartz.h>
#else
#error unimplemented GTK backend
#endif


void
gst_x_overlay_set_gtk_window (GstXOverlay *xoverlay, GtkWidget *window)
{

#if defined(GDK_WINDOWING_WIN32)
    gst_x_overlay_set_xwindow_id (xoverlay, (gulong)GDK_WINDOW_HWND(window->window));
#elif defined(GDK_WINDOWING_QUARTZ)
    gst_x_overlay_set_xwindow_id (xoverlay,
        (gulong)gdk_quartz_window_get_nswindow (window->window));
#elif defined(GDK_WINDOWING_X11)
    gst_x_overlay_set_xwindow_id (xoverlay, GDK_WINDOW_XWINDOW(window->window));
#else
#error unimplemented GTK backend
#endif

}


