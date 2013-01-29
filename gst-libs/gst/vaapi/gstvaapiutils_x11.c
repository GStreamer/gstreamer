/*
 *  gstvaapiutils_x11.c - X11 utilties
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "sysdeps.h"
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

/**
 * x11_create_window:
 * @dpy: an X11 #Display
 * @w: the requested width, in pixels
 * @h: the requested height, in pixels
 * @vis: the request visual
 * @cmap: the request colormap
 *
 * Creates a border-less window with the specified dimensions. If @vis
 * is %NULL, the default visual for @display will be used. If @cmap is
 * %None, no specific colormap will be bound to the window. Also note
 * the default background color is black.
 *
 * Return value: the newly created X #Window.
 */
Window
x11_create_window(Display *dpy, guint w, guint h, Visual *vis, Colormap cmap)
{
    Window rootwin, win;
    int screen, depth;
    XSetWindowAttributes xswa;
    unsigned long xswa_mask;
    XWindowAttributes wattr;
    unsigned long black_pixel;

    screen      = DefaultScreen(dpy);
    rootwin     = RootWindow(dpy, screen);
    black_pixel = BlackPixel(dpy, screen);

    if (!vis)
        vis = DefaultVisual(dpy, screen);

    XGetWindowAttributes(dpy, rootwin, &wattr);
    depth = wattr.depth;
    if (depth != 15 && depth != 16 && depth != 24 && depth != 32)
        depth = 24;

    xswa_mask             = CWBorderPixel | CWBackPixel;
    xswa.border_pixel     = black_pixel;
    xswa.background_pixel = black_pixel;

    if (cmap) {
        xswa_mask        |= CWColormap;
        xswa.colormap     = cmap;
    }

    win = XCreateWindow(
        dpy,
        rootwin,
        0, 0, w, h,
        0,
        depth,
        InputOutput,
        vis,
        xswa_mask, &xswa
    );
    if (!win)
        return None;

    XSelectInput(dpy, win, x11_event_mask);
    return win;
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
