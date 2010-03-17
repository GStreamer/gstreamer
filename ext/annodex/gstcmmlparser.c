/*
 * gstcmmlparser.c - GStreamer CMML document parser
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#include <string.h>
#include <stdarg.h>
#include <gst/gst.h>

#include "gstcmmlparser.h"
#include "gstannodex.h"
#include "gstcmmlutils.h"

GST_DEBUG_CATEGORY_STATIC (cmmlparser);
#define GST_CAT_DEFAULT cmmlparser

static void gst_cmml_parser_generic_error (void *ctx, const char *msg, ...);
static xmlNodePtr gst_cmml_parser_new_node (GstCmmlParser * parser,
    const gchar * name, ...);
static void
gst_cmml_parser_parse_start_element_ns (xmlParserCtxt * ctxt,
    const xmlChar * name, const xmlChar * prefix, const xmlChar * URI,
    int nb_preferences, const xmlChar ** namespaces,
    int nb_attributes, int nb_defaulted, const xmlChar ** attributes);
static void gst_cmml_parser_parse_end_element_ns (xmlParserCtxt * ctxt,
    const xmlChar * name, const xmlChar * prefix, const xmlChar * URI);
static void gst_cmml_parser_parse_processing_instruction (xmlParserCtxtPtr ctxt,
    const xmlChar * target, const xmlChar * data);
static void gst_cmml_parser_meta_to_string (GstCmmlParser * parser,
    xmlNodePtr parent, GValueArray * meta);

/* initialize the parser */
void
gst_cmml_parser_init (void)
{
  GST_DEBUG_CATEGORY_INIT (cmmlparser, "cmmlparser", 0, "annodex CMML parser");

  xmlGenericError = gst_cmml_parser_generic_error;
}

/* create a new CMML parser
 */
GstCmmlParser *
gst_cmml_parser_new (GstCmmlParserMode mode)
{
  GstCmmlParser *parser = g_malloc (sizeof (GstCmmlParser));

  parser->mode = mode;
  parser->context = xmlCreatePushParserCtxt (NULL, NULL,
      NULL, 0, "cmml-bitstream");
  xmlCtxtUseOptions (parser->context, XML_PARSE_NONET | XML_PARSE_NOERROR);
  parser->context->_private = parser;
  parser->context->sax->startElementNs =
      (startElementNsSAX2Func) gst_cmml_parser_parse_start_element_ns;
  parser->context->sax->endElementNs =
      (endElementNsSAX2Func) gst_cmml_parser_parse_end_element_ns;
  parser->context->sax->processingInstruction = (processingInstructionSAXFunc)
      gst_cmml_parser_parse_processing_instruction;
  parser->preamble_callback = NULL;
  parser->cmml_end_callback = NULL;
  parser->stream_callback = NULL;
  parser->head_callback = NULL;
  parser->clip_callback = NULL;
  parser->user_data = NULL;

  return parser;
}

/* free a CMML parser instance
 */
void
gst_cmml_parser_free (GstCmmlParser * parser)
{
  if (parser) {
    xmlFreeDoc (parser->context->myDoc);
    xmlFreeParserCtxt (parser->context);
    g_free (parser);
  }
}

/* parse an xml chunk
 *
 * returns false if the xml is invalid
 */
gboolean
gst_cmml_parser_parse_chunk (GstCmmlParser * parser,
    const gchar * data, guint size, GError ** err)
{
  gint xmlres;

  xmlres = xmlParseChunk (parser->context, data, size, 0);
  if (xmlres != XML_ERR_OK) {
    xmlErrorPtr xml_error = xmlCtxtGetLastError (parser->context);

    GST_DEBUG ("Error occurred decoding chunk %s", data);
    g_set_error (err,
        GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED, "%s", xml_error->message);
    return FALSE;
  }

  return TRUE;
}

/* convert an xmlNodePtr to a string
 */
static guchar *
gst_cmml_parser_node_to_string (GstCmmlParser * parser, xmlNodePtr node)
{
  xmlBufferPtr xml_buffer;
  xmlDocPtr doc;
  guchar *str;

  if (parser)
    doc = parser->context->myDoc;
  else
    doc = NULL;

  xml_buffer = xmlBufferCreate ();
  xmlNodeDump (xml_buffer, doc, node, 0, 0);
  str = xmlStrndup (xml_buffer->content, xml_buffer->use);
  xmlBufferFree (xml_buffer);

  return str;
}

guchar *
gst_cmml_parser_tag_stream_to_string (GstCmmlParser * parser,
    GstCmmlTagStream * stream)
{
  xmlNodePtr node;
  xmlNodePtr import;
  guchar *ret;

  node = gst_cmml_parser_new_node (parser, "stream", NULL);
  if (stream->timebase)
    xmlSetProp (node, (xmlChar *) "timebase", stream->timebase);

  if (stream->utc)
    xmlSetProp (node, (xmlChar *) "utc", stream->utc);

  if (stream->imports) {
    gint i;
    GValue *val;

    for (i = 0; i < stream->imports->n_values; ++i) {
      val = g_value_array_get_nth (stream->imports, i);
      import = gst_cmml_parser_new_node (parser, "import",
          "src", g_value_get_string (val), NULL);
      xmlAddChild (node, import);
    }
  }

  ret = gst_cmml_parser_node_to_string (parser, node);

  xmlUnlinkNode (node);
  xmlFreeNode (node);

  return ret;
}

/* convert a GstCmmlTagHead to its string representation
 */
guchar *
gst_cmml_parser_tag_head_to_string (GstCmmlParser * parser,
    GstCmmlTagHead * head)
{
  xmlNodePtr node;
  xmlNodePtr tmp;
  guchar *ret;

  node = gst_cmml_parser_new_node (parser, "head", NULL);
  if (head->title) {
    tmp = gst_cmml_parser_new_node (parser, "title", NULL);
    xmlNodeSetContent (tmp, head->title);
    xmlAddChild (node, tmp);
  }

  if (head->base) {
    tmp = gst_cmml_parser_new_node (parser, "base", "uri", head->base, NULL);
    xmlAddChild (node, tmp);
  }

  if (head->meta)
    gst_cmml_parser_meta_to_string (parser, node, head->meta);

  ret = gst_cmml_parser_node_to_string (parser, node);

  xmlUnlinkNode (node);
  xmlFreeNode (node);

  return ret;
}

/* convert a GstCmmlTagClip to its string representation
 */
guchar *
gst_cmml_parser_tag_clip_to_string (GstCmmlParser * parser,
    GstCmmlTagClip * clip)
{
  xmlNodePtr node;
  xmlNodePtr tmp;
  guchar *ret;

  node = gst_cmml_parser_new_node (parser, "clip",
      "id", clip->id, "track", clip->track, NULL);
  /* add the anchor element */
  if (clip->anchor_href) {
    tmp = gst_cmml_parser_new_node (parser, "a",
        "href", clip->anchor_href, NULL);
    if (clip->anchor_text)
      xmlNodeSetContent (tmp, clip->anchor_text);

    xmlAddChild (node, tmp);
  }
  /* add the img element */
  if (clip->img_src) {
    tmp = gst_cmml_parser_new_node (parser, "img",
        "src", clip->img_src, "alt", clip->img_alt, NULL);

    xmlAddChild (node, tmp);
  }
  /* add the desc element */
  if (clip->desc_text) {
    tmp = gst_cmml_parser_new_node (parser, "desc", NULL);
    xmlNodeSetContent (tmp, clip->desc_text);

    xmlAddChild (node, tmp);
  }
  /* add the meta elements */
  if (clip->meta)
    gst_cmml_parser_meta_to_string (parser, node, clip->meta);

  if (parser->mode == GST_CMML_PARSER_DECODE) {
    gchar *time_str;

    time_str = gst_cmml_clock_time_to_npt (clip->start_time);
    if (time_str == NULL)
      goto fail;

    xmlSetProp (node, (xmlChar *) "start", (xmlChar *) time_str);
    g_free (time_str);

    if (clip->end_time != GST_CLOCK_TIME_NONE) {
      time_str = gst_cmml_clock_time_to_npt (clip->end_time);
      if (time_str == NULL)
        goto fail;

      xmlSetProp (node, (xmlChar *) "end", (xmlChar *) time_str);
      g_free (time_str);
    }
  }

  ret = gst_cmml_parser_node_to_string (parser, node);

  xmlUnlinkNode (node);
  xmlFreeNode (node);

  return ret;
fail:
  xmlUnlinkNode (node);
  xmlFreeNode (node);
  return NULL;
}

guchar *
gst_cmml_parser_tag_object_to_string (GstCmmlParser * parser, GObject * tag)
{
  guchar *tag_string = NULL;
  GType tag_type = G_OBJECT_TYPE (tag);

  if (tag_type == GST_TYPE_CMML_TAG_STREAM)
    tag_string = gst_cmml_parser_tag_stream_to_string (parser,
        GST_CMML_TAG_STREAM (tag));
  else if (tag_type == GST_TYPE_CMML_TAG_HEAD)
    tag_string = gst_cmml_parser_tag_head_to_string (parser,
        GST_CMML_TAG_HEAD (tag));
  else if (tag_type == GST_TYPE_CMML_TAG_CLIP)
    tag_string = gst_cmml_parser_tag_clip_to_string (parser,
        GST_CMML_TAG_CLIP (tag));
  else
    g_warning ("could not convert object to cmml");

  return tag_string;
}

/*** private section ***/

/* create a new node
 *
 * helper to create a node and set its attributes
 */
static xmlNodePtr
gst_cmml_parser_new_node (GstCmmlParser * parser, const gchar * name, ...)
{
  va_list args;
  xmlNodePtr node;
  xmlChar *prop_name, *prop_value;

  node = xmlNewNode (NULL, (xmlChar *) name);

  va_start (args, name);

  prop_name = va_arg (args, xmlChar *);
  while (prop_name != NULL) {
    prop_value = va_arg (args, xmlChar *);
    if (prop_value != NULL)
      xmlSetProp (node, prop_name, prop_value);

    prop_name = va_arg (args, xmlChar *);
  }
  va_end (args);

  return node;
}

/* get the last node of the stream
 *
 * returns the last node at depth 1 (if any) or the root node
 */
static xmlNodePtr
gst_cmml_parser_get_last_element (GstCmmlParser * parser)
{
  xmlNodePtr node;

  node = xmlDocGetRootElement (parser->context->myDoc);
  if (!node) {
    g_warning ("no last cmml element");
    return NULL;
  }

  if (node->children)
    node = xmlGetLastChild (node);

  return node;
}

static void
gst_cmml_parser_parse_preamble (GstCmmlParser * parser,
    const guchar * attributes)
{
  gchar *preamble;
  gchar *element;
  const gchar *version;
  const gchar *encoding;
  const gchar *standalone;
  xmlDocPtr doc;

  doc = parser->context->myDoc;

  version = doc->version ? (gchar *) doc->version : "1.0";
  encoding = doc->encoding ? (gchar *) doc->encoding : "UTF-8";
  standalone = doc->standalone ? "yes" : "no";

  preamble = g_strdup_printf ("<?xml version=\"%s\""
      " encoding=\"%s\" standalone=\"%s\"?>\n"
      "<!DOCTYPE cmml SYSTEM \"cmml.dtd\">\n", version, encoding, standalone);

  if (attributes == NULL)
    attributes = (guchar *) "";

  if (parser->mode == GST_CMML_PARSER_ENCODE)
    element = g_strdup_printf ("<?cmml %s?>", attributes);
  else
    element = g_strdup_printf ("<cmml %s>", attributes);

  parser->preamble_callback (parser->user_data,
      (guchar *) preamble, (guchar *) element);

  g_free (preamble);
  g_free (element);
}

/* parse the cmml stream tag */
static void
gst_cmml_parser_parse_stream (GstCmmlParser * parser, xmlNodePtr stream)
{
  GstCmmlTagStream *stream_tag;
  GValue str_val = { 0 };
  xmlNodePtr walk;
  guchar *timebase;

  g_value_init (&str_val, G_TYPE_STRING);

  /* read the timebase and utc attributes */
  timebase = xmlGetProp (stream, (xmlChar *) "timebase");
  if (timebase == NULL)
    timebase = (guchar *) g_strdup ("0");

  stream_tag = g_object_new (GST_TYPE_CMML_TAG_STREAM,
      "timebase", timebase, NULL);
  g_free (timebase);

  stream_tag->utc = xmlGetProp (stream, (xmlChar *) "utc");

  /* walk the children nodes */
  for (walk = stream->children; walk; walk = walk->next) {
    /* for every import tag add its src attribute to stream_tag->imports */
    if (!xmlStrcmp (walk->name, (xmlChar *) "import")) {
      g_value_take_string (&str_val,
          (gchar *) xmlGetProp (walk, (xmlChar *) "src"));

      if (stream_tag->imports == NULL)
        stream_tag->imports = g_value_array_new (0);

      g_value_array_append (stream_tag->imports, &str_val);
    }
  }
  g_value_unset (&str_val);

  parser->stream_callback (parser->user_data, stream_tag);
  g_object_unref (stream_tag);
}

/* parse the cmml head tag */
static void
gst_cmml_parser_parse_head (GstCmmlParser * parser, xmlNodePtr head)
{
  GstCmmlTagHead *head_tag;
  xmlNodePtr walk;
  GValue str_val = { 0 };

  head_tag = g_object_new (GST_TYPE_CMML_TAG_HEAD, NULL);

  g_value_init (&str_val, G_TYPE_STRING);

  /* Parse the content of the node and setup the GST_TAG_CMML_HEAD tag.
   * Create a GST_TAG_TITLE when we find the title element.
   */
  for (walk = head->children; walk; walk = walk->next) {
    if (!xmlStrcmp (walk->name, (xmlChar *) "title")) {
      head_tag->title = xmlNodeGetContent (walk);
    } else if (!xmlStrcmp (walk->name, (xmlChar *) "base")) {
      head_tag->base = xmlGetProp (walk, (xmlChar *) "uri");
    } else if (!xmlStrcmp (walk->name, (xmlChar *) "meta")) {
      if (head_tag->meta == NULL)
        head_tag->meta = g_value_array_new (0);
      /* add a pair name, content to the meta value array */
      g_value_take_string (&str_val,
          (gchar *) xmlGetProp (walk, (xmlChar *) "name"));
      g_value_array_append (head_tag->meta, &str_val);
      g_value_take_string (&str_val,
          (gchar *) xmlGetProp (walk, (xmlChar *) "content"));
      g_value_array_append (head_tag->meta, &str_val);
    }
  }
  g_value_unset (&str_val);

  parser->head_callback (parser->user_data, head_tag);
  g_object_unref (head_tag);
}

/* parse a cmml clip tag */
static void
gst_cmml_parser_parse_clip (GstCmmlParser * parser, xmlNodePtr clip)
{
  GstCmmlTagClip *clip_tag;
  GValue str_val = { 0 };
  guchar *id, *track, *start, *end;
  xmlNodePtr walk;
  GstClockTime start_time = GST_CLOCK_TIME_NONE;
  GstClockTime end_time = GST_CLOCK_TIME_NONE;

  start = xmlGetProp (clip, (xmlChar *) "start");
  if (parser->mode == GST_CMML_PARSER_ENCODE && start == NULL)
    /* XXX: validate the document */
    return;

  id = xmlGetProp (clip, (xmlChar *) "id");
  track = xmlGetProp (clip, (xmlChar *) "track");
  end = xmlGetProp (clip, (xmlChar *) "end");

  if (track == NULL)
    track = (guchar *) g_strdup ("default");

  if (start) {
    if (!strncmp ((gchar *) start, "smpte", 5))
      start_time = gst_cmml_clock_time_from_smpte ((gchar *) start);
    else
      start_time = gst_cmml_clock_time_from_npt ((gchar *) start);
  }

  if (end) {
    if (!strncmp ((gchar *) end, "smpte", 5))
      start_time = gst_cmml_clock_time_from_smpte ((gchar *) end);
    else
      end_time = gst_cmml_clock_time_from_npt ((gchar *) end);
  }

  clip_tag = g_object_new (GST_TYPE_CMML_TAG_CLIP, "id", id,
      "track", track, "start-time", start_time, "end-time", end_time, NULL);

  g_free (id);
  g_free (track);
  g_free (start);
  g_free (end);

  g_value_init (&str_val, G_TYPE_STRING);

  /* parse the children */
  for (walk = clip->children; walk; walk = walk->next) {
    /* the clip is not empty */
    clip_tag->empty = FALSE;

    if (!xmlStrcmp (walk->name, (xmlChar *) "a")) {
      clip_tag->anchor_href = xmlGetProp (walk, (xmlChar *) "href");
      clip_tag->anchor_text = xmlNodeGetContent (walk);
    } else if (!xmlStrcmp (walk->name, (xmlChar *) "img")) {
      clip_tag->img_src = xmlGetProp (walk, (xmlChar *) "src");
      clip_tag->img_alt = xmlGetProp (walk, (xmlChar *) "alt");
    } else if (!xmlStrcmp (walk->name, (xmlChar *) "desc")) {
      clip_tag->desc_text = xmlNodeGetContent (walk);
    } else if (!xmlStrcmp (walk->name, (xmlChar *) "meta")) {
      if (clip_tag->meta == NULL)
        clip_tag->meta = g_value_array_new (0);
      /* add a pair name, content to the meta value array */
      g_value_take_string (&str_val,
          (char *) xmlGetProp (walk, (xmlChar *) "name"));
      g_value_array_append (clip_tag->meta, &str_val);
      g_value_take_string (&str_val,
          (char *) xmlGetProp (walk, (xmlChar *) "content"));
      g_value_array_append (clip_tag->meta, &str_val);
    }
  }
  g_value_unset (&str_val);

  parser->clip_callback (parser->user_data, clip_tag);
  g_object_unref (clip_tag);
}

void
gst_cmml_parser_meta_to_string (GstCmmlParser * parser,
    xmlNodePtr parent, GValueArray * array)
{
  gint i;
  xmlNodePtr node;
  GValue *name, *content;

  for (i = 0; i < array->n_values - 1; i += 2) {
    name = g_value_array_get_nth (array, i);
    content = g_value_array_get_nth (array, i + 1);
    node = gst_cmml_parser_new_node (parser, "meta",
        "name", g_value_get_string (name),
        "content", g_value_get_string (content), NULL);
    xmlAddChild (parent, node);
  }
}

static void
gst_cmml_parser_generic_error (void *ctx, const char *msg, ...)
{
#ifndef GST_DISABLE_GST_DEBUG
  va_list varargs;

  va_start (varargs, msg);
  gst_debug_log_valist (GST_CAT_DEFAULT, GST_LEVEL_WARNING,
      "", "", 0, NULL, msg, varargs);
  va_end (varargs);
#endif /* GST_DISABLE_GST_DEBUG */
}

/* sax handler called when an element start tag is found
 * this is used to parse the cmml start tag
 */
static void
gst_cmml_parser_parse_start_element_ns (xmlParserCtxt * ctxt,
    const xmlChar * name, const xmlChar * prefix, const xmlChar * URI,
    int nb_preferences, const xmlChar ** namespaces,
    int nb_attributes, int nb_defaulted, const xmlChar ** attributes)
{
  GstCmmlParser *parser = (GstCmmlParser *) ctxt->_private;

  xmlSAX2StartElementNs (ctxt, name, prefix, URI, nb_preferences, namespaces,
      nb_attributes, nb_defaulted, attributes);

  if (parser->mode == GST_CMML_PARSER_ENCODE)
    if (!xmlStrcmp (name, (xmlChar *) "cmml"))
      if (parser->preamble_callback)
        /* FIXME: parse attributes */
        gst_cmml_parser_parse_preamble (parser, NULL);
}

/* sax processing instruction handler
 * used to parse the cmml processing instruction
 */
static void
gst_cmml_parser_parse_processing_instruction (xmlParserCtxtPtr ctxt,
    const xmlChar * target, const xmlChar * data)
{
  GstCmmlParser *parser = (GstCmmlParser *) ctxt->_private;

  xmlSAX2ProcessingInstruction (ctxt, target, data);

  if (parser->mode == GST_CMML_PARSER_DECODE)
    if (!xmlStrcmp (target, (xmlChar *) "cmml"))
      if (parser->preamble_callback)
        gst_cmml_parser_parse_preamble (parser, data);
}

/* sax handler called when an xml end tag is found
 * used to parse the stream, head and clip nodes
 */
static void
gst_cmml_parser_parse_end_element_ns (xmlParserCtxt * ctxt,
    const xmlChar * name, const xmlChar * prefix, const xmlChar * URI)
{
  xmlNodePtr node;
  GstCmmlParser *parser = (GstCmmlParser *) ctxt->_private;

  xmlSAX2EndElementNs (ctxt, name, prefix, URI);

  if (!xmlStrcmp (name, (xmlChar *) "clip")) {
    if (parser->clip_callback) {
      node = gst_cmml_parser_get_last_element (parser);
      gst_cmml_parser_parse_clip (parser, node);
    }
  } else if (!xmlStrcmp (name, (xmlChar *) "cmml")) {
    if (parser->cmml_end_callback)
      parser->cmml_end_callback (parser->user_data);
  } else if (!xmlStrcmp (name, (xmlChar *) "stream")) {
    if (parser->stream_callback) {
      node = gst_cmml_parser_get_last_element (parser);
      gst_cmml_parser_parse_stream (parser, node);
    }
  } else if (!xmlStrcmp (name, (xmlChar *) "head")) {
    if (parser->head_callback) {
      node = gst_cmml_parser_get_last_element (parser);
      gst_cmml_parser_parse_head (parser, node);
    }
  }
}
