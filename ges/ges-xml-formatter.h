/* Gstreamer Editing Services
 *
 * Copyright (C) <2012> Thibault Saunier <thibault.saunier@collabora.com>
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
#pragma once

#include "ges-base-xml-formatter.h"

G_BEGIN_DECLS
#define GES_TYPE_XML_FORMATTER (ges_xml_formatter_get_type ())
GES_DECLARE_TYPE(XmlFormatter, xml_formatter, XML_FORMATTER);

struct _GESXmlFormatter
{
  GESBaseXmlFormatter parent;

  GESXmlFormatterPrivate *priv;

  gpointer _ges_reserved[GES_PADDING];
};

struct _GESXmlFormatterClass
{
  GESBaseXmlFormatterClass parent;

  gpointer _ges_reserved[GES_PADDING];
};

G_END_DECLS
