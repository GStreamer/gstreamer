/* gst-python
 * Copyright (C) 2002 David I. Lehn
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 * 
 * Author: David I. Lehn <dlehn@vt.edu>
 */

#include <glib-object.h>
#include <gst/gst.h>

#define GST_PAD_TEMPLATE GST_PADTEMPLATE
#define GST_TYPE_ELEMENT_FACTORY GST_TYPE_ELEMENTFACTORY
#define GST_ELEMENT_FACTORY GST_ELEMENTFACTORY
#define GST_AUTOPLUG_FACTORY GST_AUTOPLUGFACTORY
#define GST_TYPE_TIME_CACHE GST_TYPE_TIMECACHE
#define GST_SCHEDULER_FACTORY GST_SCHEDULERFACTORY
#define GST_TIME_CACHE GST_TIMECACHE
#define GST_TYPE_FACTORY GST_TYPEFACTORY
#define GST_TYPE_TYPE_FACTORY GST_TYPE_TYPEFACTORY
#define GST_TYPE_SCHEDULER_FACTORY GST_TYPE_SCHEDULERFACTORY
#define GST_TYPE_AUTOPLUG_FACTORY GST_TYPE_AUTOPLUGFACTORY
#define GST_TYPE_TYPE_FIND GST_TYPE_TYPEFIND
#define GST_TYPE_PAD_TEMPLATE GST_TYPE_PADTEMPLATE

#include <gst/gstqueue.h>
#include <gst/gsttypefind.h>
#include "tmp-enum-types.h"
