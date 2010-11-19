/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oravnas@cisco.com>
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
 */

#ifndef __GST_DYN_API_INTERNAL_H__
#define __GST_DYN_API_INTERNAL_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GstDynSymSpec       GstDynSymSpec;

struct _GstDynSymSpec
{
  const gchar * name;
  guint offset;
  gboolean is_required;
};

gpointer _gst_dyn_api_new (GType derived_type, const gchar * filename,
    const GstDynSymSpec * symbols, GError ** error);

G_END_DECLS

#endif

