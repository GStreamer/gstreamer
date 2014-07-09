/*
 * Copyright (c) 2013, Intel Corporation.
 * Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef __XML_UTILS_H__
#define __XML_UTILS_H__

#include <gst/gst.h>
#include <libxml/tree.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>

#define ANALYZER_XML_ELEMENT_START(writer,element) { \
  if (xmlTextWriterStartElement (writer, (xmlChar *)element) < 0) { \
    GST_ERROR ("Failed to start the element %s",element); \
    goto error; \
  } \
}

/* to create a new #element with #content */
#define ANALYZER_XML_ELEMENT_NEW(writer,element, content) { \
  if (xmlTextWriterWriteElement (writer, (xmlChar *)element, content) < 0) { \
    GST_ERROR ("Failed to write %s to the element %s",content, element); \
    goto error; \
  } \
}

#define ANALYZER_XML_ELEMENT_CONTENT_INTEGER_WRITE(writer,content) { \
  if (xmlTextWriterWriteFormatRaw (writer, "%d ", content) < 0) { \
    GST_ERROR ("Failed to write %d to the element",content); \
    goto error; \
  } \
}

#define ANALYZER_XML_ELEMENT_CONTENT_STRING_WRITE(writer,content) { \
  if (xmlTextWriterWriteFormatRaw (writer, "%s", content) < 0) { \
    GST_ERROR ("Failed to write %s to the element",content); \
    goto error; \
  } \
}

#define ANALYZER_XML_ELEMENT_ATTRIBUTE_INTEGER_WRITE(writer, attribute, value) { \
  if (xmlTextWriterWriteFormatAttribute (writer, (xmlChar *)attribute, "%d", value) < 0) { \
    GST_ERROR ("Failed to add attribute %s with value %d",attribute, value); \
    goto error; \
  } \
}

#define ANALYZER_XML_ELEMENT_WRITE_STRING (writer,element,content) { \
  if (xmlTextWriterWriteFormatElement (writer, (xmlChar *)element, "%s", content) < 0) { \
    GST_ERROR ("Failed to write %s to the element %s",content, element); \
    goto error; \
  } \
}

#define ANALYZER_XML_ELEMENT_END(writer) { \
  if (xmlTextWriterEndElement (writer) < 0) { \
    GST_ERROR ("Failed to end the element"); \
    goto error; \
  } \
}

#define ANALYZER_XML_ELEMENT_CREATE_INT(writer,element,content,attribute,value) { \
    ANALYZER_XML_ELEMENT_START (writer, element); \
    if (0 != value) \
      ANALYZER_XML_ELEMENT_ATTRIBUTE_INTEGER_WRITE (writer, attribute, value); \
    ANALYZER_XML_ELEMENT_CONTENT_INTEGER_WRITE (writer, content); \
    ANALYZER_XML_ELEMENT_END (writer); \
}

#define ANALYZER_XML_ELEMENT_CREATE_STRING(writer,element,content,attribute,value) { \
    ANALYZER_XML_ELEMENT_START (writer, element); \
    if (value != 0) \
      ANALYZER_XML_ELEMENT_ATTRIBUTE_INTEGER_WRITE (writer, attribute, value); \
    ANALYZER_XML_ELEMENT_CONTENT_STRING_WRITE (writer, content); \
    ANALYZER_XML_ELEMENT_END (writer); \
}

#define ANALYZER_XML_ELEMENT_CREATE_MATRIX(writer,element,content,rows,columns) { \
    int i, j; \
    int num_elements = rows * columns; \
    ANALYZER_XML_ELEMENT_START (writer, element); \
    ANALYZER_XML_ELEMENT_ATTRIBUTE_INTEGER_WRITE (writer, "is-matrix", 1); \
    ANALYZER_XML_ELEMENT_ATTRIBUTE_INTEGER_WRITE (writer, "rows", rows); \
    ANALYZER_XML_ELEMENT_ATTRIBUTE_INTEGER_WRITE (writer, "columns", columns); \
    for (i = 0; i < num_elements; i++) \
        ANALYZER_XML_ELEMENT_CONTENT_INTEGER_WRITE (writer, content[i]); \
    ANALYZER_XML_ELEMENT_END (writer); \
}

#endif /* __XML_UTILS_H__ */
