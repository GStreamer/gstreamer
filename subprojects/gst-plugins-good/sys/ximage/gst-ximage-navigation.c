/* GStreamer
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

#include "gst-ximage-navigation.h"
#include <X11/extensions/XTest.h>

// Based on xtestlib: https://www.x.org/releases/X11R7.5/doc/Xext/xtestlib.html

void
gst_ximage_navigation_mouse_move_pointer (Display * display, int x, int y)
{
  // If screen_number is -1, the current screen (that the pointer is on) is used
  XTestFakeMotionEvent (display, -1, x, y, CurrentTime);
  XSync (display, FALSE);
  return;
}

void
gst_ximage_navigation_mouse_push_button (Display * display,
    unsigned int button, Bool is_press)
{
  /*
     button values:
     1 = left button
     2 = middle button (pressing the scroll wheel)
     3 = right button
     4 = turn scroll wheel up
     5 = turn scroll wheel down
     6 = push scroll wheel left
     7 = push scroll wheel right
     8 = 4th button (aka browser backward button)
     9 = 5th button (aka browser forward button)
   */
  XTestFakeButtonEvent (display, button, is_press, CurrentTime);
  XSync (display, FALSE);
  return;
}

void
gst_ximage_navigation_key (Display * display, const char *keysym_name,
    Bool is_press)
{
  // keysym_name: one of X11 keysym names defined in https://www.cl.cam.ac.uk/~mgk25/ucs/keysyms.txt
  unsigned int keysym, keycode;
  keysym = (unsigned int) XStringToKeysym (keysym_name);
  keycode = XKeysymToKeycode (display, keysym);
  if (keycode == 0)             // undefined KeySym
    return;
  XTestFakeKeyEvent (display, keycode, is_press, CurrentTime);
  XSync (display, FALSE);
  return;
}
