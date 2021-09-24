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

#include "ges-formatter.h"

#pragma once

G_BEGIN_DECLS
typedef struct _GESBaseXmlFormatter GESBaseXmlFormatter;
typedef struct _GESBaseXmlFormatterClass GESBaseXmlFormatterClass;

#define GES_TYPE_BASE_XML_FORMATTER (ges_base_xml_formatter_get_type ())
GES_DECLARE_TYPE(BaseXmlFormatter, base_xml_formatter, BASE_XML_FORMATTER);

/**
 * GESBaseXmlFormatter:
 */
struct _GESBaseXmlFormatter
{
  GESFormatter parent;

  /*< public > */
  /* <private> */
  GESBaseXmlFormatterPrivate *priv;
  gchar *xmlcontent;

  gpointer _ges_reserved[GES_PADDING - 1];
};

/**
 * GESBaseXmlFormatterClass:
 */
struct _GESBaseXmlFormatterClass
{
  GESFormatterClass parent;

  /* Should be overriden by subclasses */
  GMarkupParser content_parser;

  GString * (*save) (GESFormatter *formatter, GESTimeline *timeline, GError **error);

  gpointer _ges_reserved[GES_PADDING];
};

G_END_DECLS
