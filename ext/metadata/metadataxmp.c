/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/*
 * SECTION: metadataxmp
 * @short_description: This module provides functions to extract tags from
 * XMP metadata chunks and create XMP chunks from metadata tags.
 * @see_also: #metadatatags.[c/h]
 *
 * If lib exempi isn't available at compilation time, only the whole chunk
 * (#METADATA_TAG_MAP_WHOLECHUNK) tags is created. It means that individual
 * tags aren't mapped.
 *
 * <refsect2>
 * <para>
 * #metadata_xmp_init must be called before any other function in this
 * module and must be paired with a call to #metadata_xmp_dispose
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2008-01-24 (0.10.15)
 */

/*
 * includes
 */

#include "metadataxmp.h"
#include "metadataparseutil.h"
#include "metadatatags.h"

/*
 * defines
 */

GST_DEBUG_CATEGORY (gst_metadata_xmp_debug);
#define GST_CAT_DEFAULT gst_metadata_xmp_debug

/*
 * Implementation when lib exempi isn't available at compilation time
 */

#ifndef HAVE_XMP

/*
 * extern functions implementations
 */

gboolean
metadata_xmp_init (void)
{
  return TRUE;
}

void
metadata_xmp_dispose (void)
{
  return;
}

void
metadataparse_xmp_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{

  if (mapping & METADATA_TAG_MAP_WHOLECHUNK) {
    GST_LOG ("XMP not defined, sending just one tag as whole chunk");
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_XMP, adapter);
  }

}

void
metadatamux_xmp_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist)
{
  /* do nothing */
}

#else /* ifndef HAVE_XMP */

/*
 * Implementation when lib exempi isn't available at compilation time
 */

/*
 * includes
 */

#include <exempi/xmp.h>
#include <string.h>

/*
 * enum and types
 */

typedef struct _tag_SchemaTagMap
{
  const gchar *xmp_tag;
  const gchar *gst_tag;
} SchemaTagMap;

typedef struct _tag_SchemaMap
{
  const gchar *schema;
  const gchar *prefix;
  const guint8 prefix_len;
  const SchemaTagMap *tags_map;
} SchemaMap;

/*
 * defines and static global vars
 */

#define XMP_SCHEMA_NODE 0x80000000UL

/* *INDENT-OFF* */
/* When changing these tables, update 'metadata_mapping.htm' file too. */
static const SchemaTagMap schema_map_dublin_tags_map[] = {
  {"creator",     GST_TAG_ARTIST      },
  {"description", GST_TAG_DESCRIPTION },
  {"format",      GST_TAG_VIDEO_CODEC },
  {"rights",      GST_TAG_COPYRIGHT   },
  {"subject",     GST_TAG_KEYWORDS    },
  {"title",       GST_TAG_TITLE       },
  {"type",        GST_TAG_CODEC       },
  {NULL, NULL}
};

static const SchemaTagMap schema_map_photoshop_tags_map[] = {
  {"Country",     GST_TAG_GEO_LOCATION_COUNTRY },
  {"City",        GST_TAG_GEO_LOCATION_CITY   },
  {NULL, NULL}
};

static const SchemaTagMap schema_map_iptc4xmpcore_tags_map[] = {
  {"location",    GST_TAG_GEO_LOCATION_SUBLOCATION },
  {NULL, NULL}
};
/* *INDENT-ON* */

static const SchemaMap schema_map_dublin = {
  "http://purl.org/dc/elements/1.1/",
  "dc:",
  3,
  schema_map_dublin_tags_map
};

/* http://www.adobe.com/devnet/xmp/pdfs/xmp_specification.pdf */
static const SchemaMap schema_map_photoshop = {
  "http://ns.adobe.com/photoshop/1.0/",
  "photoshop:",
  10,
  schema_map_photoshop_tags_map
};

/* http://www.iptc.org/std/Iptc4xmpCore/1.0/specification/Iptc4xmpCore_1.0-spec-XMPSchema_8.pdf */
static const SchemaMap schema_map_iptc4xmpcore = {
  "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
  "Iptc4xmpCore:",
  13,
  schema_map_iptc4xmpcore_tags_map
};

static const SchemaMap *schemas_map[] = {
  &schema_map_dublin,
  &schema_map_photoshop,
  &schema_map_iptc4xmpcore,
  NULL
};

/*
 * static helper functions declaration
 */

static const SchemaTagMap *metadataparse_xmp_get_tagsmap_from_path (const
    SchemaMap * schema_map, const gchar * path, uint32_t opt);

static const SchemaTagMap *metadatamux_xmp_get_tagsmap_from_gsttag (const
    SchemaMap * schema_map, const gchar * tag);

static void
metadataparse_xmp_iter (GstTagList * taglist, GstTagMergeMode mode, XmpPtr xmp);

static void
metadataparse_xmp_iter_node_schema (GstTagList * taglist, GstTagMergeMode mode,
    XmpPtr xmp, const char *schema, const char *path);

static void
metadataparse_xmp_iter_array (GstTagList * taglist, GstTagMergeMode mode,
    XmpPtr xmp, const char *schema, const char *path,
    const SchemaMap * schema_map);

static void
metadataparse_xmp_iter_simple_qual (GstTagList * taglist, GstTagMergeMode mode,
    const char *path, const char *value, const SchemaMap * schema_map);

static void
metadataparse_xmp_iter_simple (GstTagList * taglist, GstTagMergeMode mode,
    const char *path, const char *value, const SchemaMap * schema_map);

static void
metadataparse_xmp_iter_add_to_tag_list (GstTagList * taglist,
    GstTagMergeMode mode, const char *path, const char *value,
    const SchemaMap * schema_map, const uint32_t opt);

static void
metadatamux_xmp_for_each_tag_in_list (const GstTagList * list,
    const gchar * tag, gpointer user_data);

/*
 * extern functions implementations
 */

/*
 * metadata_xmp_init:
 *
 * Init lib exempi (if present in compilation time)
 * This function must be called before any other function from this module.
 * This function must not be called twice without call
 * to #metadata_xmp_dispose beteween them.
 * @see_also: #metadata_xmp_dispose
 *
 * Returns: nothing
 */

gboolean
metadata_xmp_init (void)
{
  return xmp_init ();
}

/*
 * metadata_xmp_dispose:
 *
 * Call this function to free any resource allocated by #metadata_xmp_init
 * @see_also: #metadata_xmp_init
 *
 * Returns: nothing
 */

void
metadata_xmp_dispose (void)
{
  xmp_terminate ();
}

/*
 * metadataparse_xmp_tag_list_add:
 * @taglist: tag list in which extracted tags will be added
 * @mode: tag list merge mode
 * @adapter: contains the XMP metadata chunk
 * @mapping: if is to extract individual tags and/or the whole chunk.
 * 
 * This function gets a XMP chunk (@adapter) and extract tags from it
 * and then to add to @taglist.
 * Note: The XMP chunk (@adapetr) must NOT be wrapped by any bytes specific
 * to any file format
 * @see_also: #metadataparse_xmp_iter
 *
 * Returns: nothing
 */

void
metadataparse_xmp_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{
  const guint8 *buf;
  guint32 size;
  XmpPtr xmp = NULL;

  if (adapter == NULL || (size = gst_adapter_available (adapter)) == 0) {
    goto done;
  }

  /* add chunk tag */
  if (mapping & METADATA_TAG_MAP_WHOLECHUNK) {
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_XMP, adapter);
  }

  if (!(mapping & METADATA_TAG_MAP_INDIVIDUALS))
    goto done;

  buf = gst_adapter_peek (adapter, size);

  xmp = xmp_new ((gchar *) buf, size);
  if (!xmp)
    goto done;

  metadataparse_xmp_iter (taglist, mode, xmp);

done:

  if (xmp) {
    xmp_free (xmp);
  }

  return;

}

/*
 * metadatamux_xmp_create_chunk_from_tag_list:
 * @buf: buffer that will have the created XMP chunk
 * @size: size of the buffer that will be created
 * @taglist: list of tags to be added to XMP chunk
 *
 * Get tags from @taglist, create a XMP chunk based on it and save to @buf.
 * Note: The XMP chunk is NOT wrapped by any bytes specific to any file format
 *
 * Returns: nothing
 */

void
metadatamux_xmp_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist)
{
  GstBuffer *xmp_chunk = NULL;
  const GValue *val = NULL;
  XmpPtr xmp = NULL;
  XmpStringPtr xmp_str_buf = xmp_string_new ();

  if (!(buf && size))
    goto done;

  g_free (*buf);
  *buf = NULL;
  *size = 0;

  val = gst_tag_list_get_value_index (taglist, GST_TAG_XMP, 0);
  if (val) {
    xmp_chunk = gst_value_get_buffer (val);
    if (xmp_chunk)
      xmp =
          xmp_new ((gchar *) GST_BUFFER_DATA (xmp_chunk),
          GST_BUFFER_SIZE (xmp_chunk));
  }

  if (NULL == xmp)
    xmp = xmp_new_empty ();

  gst_tag_list_foreach (taglist, metadatamux_xmp_for_each_tag_in_list, xmp);

  if (!xmp_serialize (xmp, xmp_str_buf, 0, 2)) {
    GST_ERROR ("failed to serialize xmp into chunk\n");
  } else if (xmp_str_buf) {
    const gchar *text = xmp_string_cstr (xmp_str_buf);

    *buf = (guint8 *) g_strdup (text);
    *size = strlen (text);
  } else {
    GST_ERROR ("failed to serialize xmp into chunk\n");
  }

done:

  if (xmp_str_buf)
    xmp_string_free (xmp_str_buf);

  if (xmp)
    xmp_free (xmp);

  return;
}

/*
 * static helper functions implementation
 */

/*
 * metadataparse_xmp_get_tagsmap_from_path:
 * @schema_map: Structure containg a map beteween GST tags and tags into a XMP
 * schema
 * @path: string describing a XMP tag
 * @opt: indicates if the string (@path) has extras caracters like '[' and ']'
 *
 * This returns a structure that contains the GStreamer tag mapped to an XMP
 * tag.
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>Structure containing the GST tag mapped
 * to the XMP tag (@path)
 * </para></listitem>
 * <listitem><para>%NULL if there is no mapped GST tag for XMP tag (@path)
 * </para></listitem>
 * </itemizedlist>
 */

static const SchemaTagMap *
metadataparse_xmp_get_tagsmap_from_path (const SchemaMap * schema_map,
    const gchar * path, uint32_t opt)
{

  GString *string = NULL;
  gchar *ch;
  SchemaTagMap *tags_map = NULL;

  if (NULL == schema_map)
    goto done;

  tags_map = (SchemaTagMap *) schema_map->tags_map;

  if (XMP_HAS_PROP_QUALIFIERS (opt) || XMP_IS_ARRAY_ALTTEXT (opt)) {

    string = g_string_new (path);

    /* remove the language qualifier "[xxx]" */
    ch = string->str + string->len - 3;
    while (ch != string->str + schema_map->prefix_len) {
      if (*ch == '[') {
        *ch = '\0';
      }
      --ch;
    }

  } else {
    ch = (gchar *) path + schema_map->prefix_len;
  }

  while (tags_map->xmp_tag) {
    if (0 == strcmp (tags_map->xmp_tag, ch))
      break;
    tags_map++;
  }

done:

  if (string)
    g_string_free (string, TRUE);

  return tags_map;

}

/*
 * metadatamux_xmp_get_tagsmap_from_gsttag:
 * @schema_map: Structure containg a map beteween GST tags and tags into a XMP
 * schema
 * @tag: GStreaner tag to look for
 *
 * This returns a structure that contains the XMP tag mapped to a GStreamer
 * tag.
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>Structure containing the XMP tag mapped
 * to the GST tag (@path)
 * </para></listitem>
 * <listitem><para>%NULL if there is no mapped XMP tag for GST @tag
 * </para></listitem>
 * </itemizedlist>
 */

static const SchemaTagMap *
metadatamux_xmp_get_tagsmap_from_gsttag (const SchemaMap * schema_map,
    const gchar * tag)
{
  SchemaTagMap *tags_map = NULL;
  int i;

  if (NULL == schema_map)
    goto done;

  for (i = 0; schema_map->tags_map[i].gst_tag; i++) {
    if (0 == strcmp (schema_map->tags_map[i].gst_tag, tag)) {
      tags_map = (SchemaTagMap *) & schema_map->tags_map[i];
      break;
    }
  }

done:

  return tags_map;

}

/*
 * metadataparse_xmp_iter:
 * @taglist: tag list in which extracted tags will be added
 * @mode: tag list merge mode
 * @xmp: handle to XMP data from lib exempi
 *
 * This function looks all the shemas in a XMP data (@xmp) and then calls
 * #metadataparse_xmp_iter_node_schema for each schema. In the end, the idea is
 * to add all XMP mapped tags to @taglist by unsing a specified merge @mode
 * @see_also: #metadataparse_xmp_tag_list_add
 * #metadataparse_xmp_iter_node_schema
 *
 * Returns: nothing
 */

void
metadataparse_xmp_iter (GstTagList * taglist, GstTagMergeMode mode, XmpPtr xmp)
{
  XmpStringPtr xstr_schema = xmp_string_new ();
  XmpStringPtr xstr_path = xmp_string_new ();
  XmpStringPtr xstr_prop = xmp_string_new ();
  uint32_t opt = 0;
  XmpIteratorPtr xmp_iter = NULL;

  xmp_iter = xmp_iterator_new (xmp, NULL, NULL, XMP_ITER_JUSTCHILDREN);

  if (NULL == xmp_iter)
    goto done;

  while (xmp_iterator_next (xmp_iter, xstr_schema, xstr_path, xstr_prop, &opt)) {
    const char *schema = xmp_string_cstr (xstr_schema);
    const char *path = xmp_string_cstr (xstr_path);

    if (XMP_IS_NODE_SCHEMA (opt)) {
      GST_LOG ("%s", schema);
      metadataparse_xmp_iter_node_schema (taglist, mode, xmp, schema, path);
    } else {
      GST_LOG ("Unexpected iteraction");
    }
  }

done:

  if (xmp_iter)
    xmp_iterator_free (xmp_iter);

  if (xstr_prop)
    xmp_string_free (xstr_prop);

  if (xstr_path)
    xmp_string_free (xstr_path);

  if (xstr_schema)
    xmp_string_free (xstr_schema);
}

/*
 * metadataparse_xmp_iter_node_schema:
 * @taglist: tag list in which extracted tags will be added
 * @mode: tag list merge mode
 * @xmp: handle to XMP data from lib exempi
 * @schema: schema name string
 * @path: schema path
 *
 * This function gets a @schema, finds the #SchemaMap (structure 
 * containing @schema description and map with GST tags) to it. And then call
 * #metadataparse_xmp_iter_array. In the end, the idea is
 * to add all XMP Schema mapped tags to @taglist by unsing a specified
 * merge @mode
 * @see_also: #metadataparse_xmp_iter
 * #metadataparse_xmp_iter_array
 *
 * Returns: nothing
 */

void
metadataparse_xmp_iter_node_schema (GstTagList * taglist, GstTagMergeMode mode,
    XmpPtr xmp, const char *schema, const char *path)
{
  const SchemaMap *schema_map = NULL;
  gint i;

  for (i = 0; schemas_map[i]; i++) {
    if (0 == strcmp (schema, schemas_map[i]->schema)) {
      schema_map = schemas_map[i];
      break;
    }
  }

  metadataparse_xmp_iter_array (taglist, mode, xmp, schema, path, schema_map);
}

/*
 * metadataparse_xmp_iter_array:
 * @taglist: tag list in which extracted tags will be added
 * @mode: tag list merge mode
 * @xmp: handle to XMP data from lib exempi
 * @schema: schema name string
 * @path: schema path
 * @schema_map: structure containing @schema description and map with GST tags
 *
 * This function looks all the tags into a @schema and call other functions in
 * order to add the mapped ones to @taglist by using a specified merge @mode
 * @see_also: #metadataparse_xmp_iter_node_schema
 * #metadataparse_xmp_iter_simple_qual metadataparse_xmp_iter_simple
 *
 * Returns: nothing
 */

void
metadataparse_xmp_iter_array (GstTagList * taglist, GstTagMergeMode mode,
    XmpPtr xmp, const char *schema, const char *path,
    const SchemaMap * schema_map)
{
  XmpStringPtr xstr_schema = xmp_string_new ();
  XmpStringPtr xstr_path = xmp_string_new ();
  XmpStringPtr xstr_prop = xmp_string_new ();
  uint32_t opt = 0;
  XmpIteratorPtr xmp_iter = NULL;

  xmp_iter = xmp_iterator_new (xmp, schema, path, XMP_ITER_JUSTCHILDREN);

  if (NULL == xmp_iter)
    goto done;

  while (xmp_iterator_next (xmp_iter, xstr_schema, xstr_path, xstr_prop, &opt)) {
    const char *schema = xmp_string_cstr (xstr_schema);
    const char *path = xmp_string_cstr (xstr_path);
    const char *value = xmp_string_cstr (xstr_prop);

    if (XMP_IS_NODE_SCHEMA (opt)) {
      GST_LOG ("Unexpected iteraction");
    } else if (XMP_IS_PROP_SIMPLE (opt)) {
      if (strcmp (path, "") != 0) {
        if (XMP_HAS_PROP_QUALIFIERS (opt)) {
          /* ignore language qualifier, just get the first */
          metadataparse_xmp_iter_simple_qual (taglist, mode, path, value,
              schema_map);
        } else {
          metadataparse_xmp_iter_simple (taglist, mode, path, value,
              schema_map);
        }
      }
    } else if (XMP_IS_PROP_ARRAY (opt)) {
      /* FIXME: array with merge mode */
      GstTagMergeMode new_mode = mode;

#if 0
      //const gchar *tag = ;
      if (mode == GST_TAG_MERGE_REPLACE) {
        //gst_tag_list_remove_tag(taglist, );
      }
#endif
      if (XMP_IS_ARRAY_ALTTEXT (opt)) {
        metadataparse_xmp_iter_array (taglist, new_mode, xmp, schema, path,
            schema_map);
        xmp_iterator_skip (xmp_iter, XMP_ITER_SKIPSUBTREE);
      } else {
        metadataparse_xmp_iter_array (taglist, new_mode, xmp, schema, path,
            schema_map);
        xmp_iterator_skip (xmp_iter, XMP_ITER_SKIPSUBTREE);
      }
    }

  }

done:

  if (xmp_iter)
    xmp_iterator_free (xmp_iter);

  if (xstr_prop)
    xmp_string_free (xstr_prop);

  if (xstr_path)
    xmp_string_free (xstr_path);

  if (xstr_schema)
    xmp_string_free (xstr_schema);

}

/*
 * metadataparse_xmp_iter_simple_qual:
 * @taglist: tag list in which extracted tags will be added
 * @mode: tag list merge mode
 * @path: schema path
 * @value: value of the (@path) tag
 * @schema_map: structure containing @schema description and map with GST tags
 *
 * This function gets a XMP tag (@path) with quilifiers and try to add it
 * to @taglist by calling  #metadataparse_xmp_iter_add_to_tag_list
 * @see_also: #metadataparse_xmp_iter_array
 * #metadataparse_xmp_iter_simple #metadataparse_xmp_iter_add_to_tag_list
 *
 * Returns: nothing
 */

void
metadataparse_xmp_iter_simple_qual (GstTagList * taglist, GstTagMergeMode mode,
    const char *path, const char *value, const SchemaMap * schema_map)
{
  GString *string = g_string_new (path);

#ifndef GST_DISABLE_GST_DEBUG
  gchar *ch;

  /* remove the language qualifier */
  ch = string->str + string->len - 3;
  while (ch != string->str + schema_map->prefix_len) {
    if (*ch == '[') {
      *ch = '\0';
    }
    --ch;
  }
  GST_LOG ("  %s = %s", string->str, value);
#endif /* #ifndef GST_DISABLE_GST_DEBUG */

  metadataparse_xmp_iter_add_to_tag_list (taglist, mode, path, value,
      schema_map, XMP_PROP_HAS_QUALIFIERS);

  g_string_free (string, TRUE);
}

/*
 * metadataparse_xmp_iter_simple:
 * @taglist: tag list in which extracted tags will be added
 * @mode: tag list merge mode
 * @path: schema path
 * @value: value of the (@path) tag
 * @schema_map: structure containing @schema description and map with GST tags
 *
 * This function gets a simple XMP tag (@path) and try to add it to @taglist by
 * calling # metadataparse_xmp_iter_add_to_tag_list
 * @see_also: #metadataparse_xmp_iter_array
 * #metadataparse_xmp_iter_simple_qual #metadataparse_xmp_iter_add_to_tag_list
 *
 * Returns: nothing
 */

void
metadataparse_xmp_iter_simple (GstTagList * taglist, GstTagMergeMode mode,
    const char *path, const char *value, const SchemaMap * schema_map)
{
  GST_LOG ("  %s = %s", path, value);

  metadataparse_xmp_iter_add_to_tag_list (taglist, mode, path, value,
      schema_map, 0);

}

/*
 * metadataparse_xmp_iter_add_to_tag_list:
 * @taglist: tag list in which extracted tags will be added
 * @mode: tag list merge mode
 * @path: schema path
 * @value: value of the (@path) tag
 * @schema_map: structure containing @schema description and map with GST tags
 * @opt: indicates if the string (@path) has extras caracters like '[' and ']'
 *
 * This function gets a XMP tag (@path) and see if it is mapped to a GST tag by
 * calling #metadataparse_xmp_get_tagsmap_from_path, if so, add it to @taglist
 * by using a specified merge @mode
 * @see_also: #metadataparse_xmp_iter_simple_qual
 * #metadataparse_xmp_iter_simple #metadataparse_xmp_get_tagsmap_from_path
 *
 * Returns: nothing
 */

static void
metadataparse_xmp_iter_add_to_tag_list (GstTagList * taglist,
    GstTagMergeMode mode, const char *path, const char *value,
    const SchemaMap * schema_map, const uint32_t opt)
{
  GType type;
  const SchemaTagMap *smaptag =
      metadataparse_xmp_get_tagsmap_from_path (schema_map, path, opt);

  if (NULL == smaptag)
    goto done;

  if (NULL == smaptag->gst_tag)
    goto done;

  type = gst_tag_get_type (smaptag->gst_tag);

  switch (type) {
    case G_TYPE_STRING:
      gst_tag_list_add (taglist, mode, smaptag->gst_tag, value, NULL);
      break;
    default:
      break;
  }

done:

  return;

}

/*
 * metadatamux_xmp_for_each_tag_in_list:
 * @list: GStreamer tag list from which @tag belongs to
 * @tag: GStreamer tag to be added to the XMP chunk
 * @user_data: pointer to #XmpPtr in which the tag will be added
 *
 * This function designed to be called for each tag in GST tag list. This
 * function adds get the tag value from tag @list and then add it to the XMP
 * chunk by using #XmpPtr and related functions from lib exempi
 * @see_also: #metadatamux_xmp_create_chunk_from_tag_list
 *
 * Returns: nothing
 */

static void
metadatamux_xmp_for_each_tag_in_list (const GstTagList * list,
    const gchar * tag, gpointer user_data)
{
  XmpPtr xmp = (XmpPtr) user_data;
  int i;

  GST_DEBUG ("trying to map tag '%s' to xmp", tag);

  for (i = 0; schemas_map[i]; i++) {

    /* FIXME: should try to get all of values (index) for the tag */

    const SchemaMap *smap = schemas_map[i];
    const SchemaTagMap *stagmap =
        metadatamux_xmp_get_tagsmap_from_gsttag (smap, tag);

    if (stagmap) {
      gchar *value = NULL;
      GType type = gst_tag_get_type (tag);

      switch (type) {
        case G_TYPE_STRING:
          gst_tag_list_get_string (list, tag, &value);
          break;
        default:
          break;
      }

      GST_DEBUG ("found mapping for tag '%s' in schema %s", tag,
          schemas_map[i]->prefix);

      if (value) {
        uint32_t options = 0;

#ifdef XMP_1_99_5
        if (xmp_get_property (xmp, smap->schema, stagmap->xmp_tag,
                NULL, &options)) {
#else
        if (xmp_get_property_and_bits (xmp, smap->schema, stagmap->xmp_tag,
                NULL, &options)) {
#endif
          if (XMP_IS_PROP_SIMPLE (options)) {
#ifdef XMP_1_99_5
            xmp_set_property (xmp, smap->schema, stagmap->xmp_tag, value, 0);
#else
            xmp_set_property (xmp, smap->schema, stagmap->xmp_tag, value);
#endif
          } else {
            xmp_set_array_item (xmp, smap->schema, stagmap->xmp_tag, 1,
                value, 0);
          }
        } else {
#ifdef XMP_1_99_5
          xmp_set_property (xmp, smap->schema, stagmap->xmp_tag, value, 0);
#else
          xmp_set_property (xmp, smap->schema, stagmap->xmp_tag, value);
#endif
        }

        g_free (value);
      }
    } else {
      GST_DEBUG ("no xmp mapping for tag '%s' in schema %s found", tag,
          schemas_map[i]->prefix);
    }
  }
}

#endif /* else (ifndef HAVE_XMP) */
