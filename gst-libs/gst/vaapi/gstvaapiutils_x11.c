/*
 *  gstvaapiutils_x11.c - X11 utilties
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "config.h"
#include <glib.h>
#include <X11/Xutil.h>
#include "gstvaapiutils_x11.h"

// X error trap
static int x11_error_code = 0;
static int (*old_error_handler)(Display *, XErrorEvent *);

static int error_handler(Display *dpy, XErrorEvent *error)
{
    x11_error_code = error->error_code;
    return 0;
}

void x11_trap_errors(void)
{
    x11_error_code    = 0;
    old_error_handler = XSetErrorHandler(error_handler);
}

int x11_untrap_errors(void)
{
    XSetErrorHandler(old_error_handler);
    return x11_error_code;
}

// X window management
static const int x11_event_mask = (KeyPressMask |
                                   KeyReleaseMask |
                                   ButtonPressMask |
                                   ButtonReleaseMask |
                                   PointerMotionMask |
                                   EnterWindowMask |
                                   ExposureMask |
                                   StructureNotifyMask);

Window
x11_create_window(Display *display, guint width, guint height)
{
    Window root_window, window;
    int screen, depth;
    Visual *vis;
    XSetWindowAttributes xswa;
    unsigned long xswa_mask;
    XWindowAttributes wattr;
    unsigned long black_pixel, white_pixel;

    screen      = DefaultScreen(display);
    vis         = DefaultVisual(display, screen);
    root_window = RootWindow(display, screen);
    black_pixel = BlackPixel(display, screen);
    white_pixel = WhitePixel(display, screen);

    XGetWindowAttributes(display, root_window, &wattr);
    depth = wattr.depth;
    if (depth != 15 && depth != 16 && depth != 24 && depth != 32)
        depth = 24;

    xswa_mask             = CWBorderPixel | CWBackPixel;
    xswa.border_pixel     = black_pixel;
    xswa.background_pixel = white_pixel;

    window = XCreateWindow(
        display,
        root_window,
        0, 0, width, height,
        0,
        depth,
        InputOutput,
        vis,
        xswa_mask, &xswa
    );
    if (!window)
        return None;

    XSelectInput(display, window, x11_event_mask);
    return window;
}

gboolean
x11_get_geometry(
    Display    *dpy,
    Drawable    drawable,
    gint       *px,
    gint       *py,
    guint      *pwidth,
    guint      *pheight
)
{
    Window rootwin;
    int x, y;
    guint width, height, border_width, depth;

    x11_trap_errors();
    XGetGeometry(
        dpy,
        drawable,
        &rootwin,
        &x, &y, &width, &height,
        &border_width,
        &depth
    );
    if (x11_untrap_errors())
        return FALSE;

    if (px)      *px      = x;
    if (py)      *py      = y;
    if (pwidth)  *pwidth  = width;
    if (pheight) *pheight = height;
    return TRUE;
}

void x11_wait_event(Display *dpy, Window w, int type)
{
    XEvent e;
    while (!XCheckTypedWindowEvent(dpy, w, type, &e))
        g_usleep(10);
}
