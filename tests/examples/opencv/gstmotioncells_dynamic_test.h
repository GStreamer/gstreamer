/* GStreamer
 * Copyright (C) 2011 Robert Jobbagy <jobbagy.robert@gmail.com>
 *
 *
 *  gst_motioncells_dynamic_test(): a test tool what can to do dynamic change properties
 *
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
 *
 */

#ifndef GST_MOTIONCELLS_DYNAMIC_TEST_H
#define GST_MOTIONCELLS_DYNAMIC_TEST_H

extern void setProperty (GstElement * mcells, char *property, char *prop_value,
    GType type, GValue * value);


#endif
