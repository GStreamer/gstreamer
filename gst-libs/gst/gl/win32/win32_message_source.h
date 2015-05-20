/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
 * Copyright (C) 2015 Collabora ltd.
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

#ifndef __WIN32_MESSAGE_SOURCE_H__
#define __WIN32_MESSAGE_SOURCE_H__

#include <glib-object.h>
#include "gstglwindow_win32.h"

typedef void (*Win32MessageSourceFunc) (GstGLWindowWin32 *window_win32,
    MSG *msg, gpointer user_data);

GSource *
win32_message_source_new (GstGLWindowWin32 *window_win32);

#endif /* __WIN32_MESSAGE_SOURCE_H__ */
