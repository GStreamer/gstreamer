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

#include "metadataxmp.h"
#include "metadataparseutil.h"
#include "metadatatags.h"

GST_DEBUG_CATEGORY (gst_metadata_xmp_debug);
#define GST_CAT_DEFAULT gst_metadata_xmp_debug

#ifndef HAVE_XMP

void
metadataparse_xmp_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{

  if (mapping & METADATA_TAG_MAP_WHOLECHUNK) {
    GST_LOG ("XMP not defined, here I should send just one tag as whole chunk");
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_XMP, adapter);
  }

}

gboolean
metadataparse_xmp_init (void)
{
  return TRUE;
}

void
metadataparse_xmp_dispose (void)
{
  return;
}

void
metadatamux_xmp_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist)
{
  /* do nothing */
}

#else /* ifndef HAVE_XMP */

#include <xmp.h>
#include <string.h>

#define XMP_SCHEMA_NODE 0x80000000UL

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

static const SchemaTagMap schema_map_dublin_tags_map[] = {
  {"description", GST_TAG_DESCRIPTION},
  {"title", GST_TAG_TITLE},
  {"rights", GST_TAG_COPYRIGHT},
  {NULL, NULL}
};

static const SchemaMap schema_map_dublin = { "http://purl.org/dc/elements/1.1/",
  "dc:",
  3,
  schema_map_dublin_tags_map
};

static const SchemaMap *schemas_map[] = {
  &schema_map_dublin,
  NULL
};

static void
metadataparse_xmp_iter_add_to_tag_list (GstTagList * taglist,
    GstTagMergeMode mode, const char *path, const char *value,
    const SchemaMap * schema_map, const uint32_t opt);

static void
metadataparse_xmp_iter_array (GstTagList * taglist, GstTagMergeMode mode,
    XmpPtr xmp, const char *schema, const char *path,
    const SchemaMap * schema_map);

static void
metadataparse_xmp_iter_node_schema (GstTagList * taglist, GstTagMergeMode mode,
    XmpPtr xmp, const char *schema, const char *path);

static void
metadataparse_xmp_iter_simple (GstTagList * taglist, GstTagMergeMode mode,
    const char *schema, const char *path, const char *value,
    const SchemaMap * schema_map);

static void
metadataparse_xmp_iter_simple_qual (GstTagList * taglist, GstTagMergeMode mode,
    const char *schema, const char *path, const char *value,
    const SchemaMap * schema_map);

static void
metadataparse_xmp_iter (GstTagList * taglist, GstTagMergeMode mode, XmpPtr xmp);

gboolean
metadataparse_xmp_init (void)
{
  return xmp_init ();
}

void
metadataparse_xmp_dispose (void)
{
  xmp_terminate ();
}

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
  if (mapping & METADATA_TAG_MAP_WHOLECHUNK)
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_XMP, adapter);

  if (!(mapping & METADATA_TAG_MAP_INDIVIDUALS))
    goto done;

  buf = gst_adapter_peek (adapter, size);

  xmp = xmp_new (buf, size);
  if (!xmp)
    goto done;

  metadataparse_xmp_iter (taglist, mode, xmp);

done:

  if (xmp) {
    xmp_free (xmp);
  }

  return;

}

static const SchemaTagMap *
metadataparse_get_tagsmap_from_gsttag (const SchemaMap * schema_map,
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

static const SchemaTagMap *
metadataparse_get_tagsmap_from_path (const SchemaMap * schema_map,
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

    /* remove the language qualifier */
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

static void
metadataparse_xmp_iter_add_to_tag_list (GstTagList * taglist,
    GstTagMergeMode mode, const char *path, const char *value,
    const SchemaMap * schema_map, const uint32_t opt)
{

  const SchemaTagMap *smaptag =
      metadataparse_get_tagsmap_from_path (schema_map, path, opt);

  if (NULL == smaptag)
    goto done;

  if (NULL == smaptag->gst_tag)
    goto done;

  GType type = gst_tag_get_type (smaptag->gst_tag);

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

void
metadataparse_xmp_iter_simple_qual (GstTagList * taglist, GstTagMergeMode mode,
    const char *schema, const char *path, const char *value,
    const SchemaMap * schema_map)
{
  GString *string = g_string_new (path);
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

  metadataparse_xmp_iter_add_to_tag_list (taglist, mode, path, value,
      schema_map, XMP_PROP_HAS_QUALIFIERS);

  g_string_free (string, TRUE);
}


void
metadataparse_xmp_iter_simple (GstTagList * taglist, GstTagMergeMode mode,
    const char *schema, const char *path, const char *value,
    const SchemaMap * schema_map)
{
  GST_LOG ("  %s = %s", path, value);

  metadataparse_xmp_iter_add_to_tag_list (taglist, mode, path, value,
      schema_map, 0);

}

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
          metadataparse_xmp_iter_simple_qual (taglist, mode, schema, path,
              value, schema_map);
        } else {
          metadataparse_xmp_iter_simple (taglist, mode, schema, path, value,
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

void
metadataparse_xmp_iter_node_schema (GstTagList * taglist, GstTagMergeMode mode,
    XmpPtr xmp, const char *schema, const char *path)
{
  SchemaMap *schema_map = NULL;

  if (0 == strcmp (schema, "http://purl.org/dc/elements/1.1/")) {
    schema_map = (SchemaMap *) & schema_map_dublin;
  }

  metadataparse_xmp_iter_array (taglist, mode, xmp, schema, path, schema_map);
}

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


static void
metadataxmp_for_each_tag_in_list (const GstTagList * list, const gchar * tag,
    gpointer user_data)
{
  XmpPtr xmp = (XmpPtr) user_data;
  int i;

  for (i = 0; schemas_map[i]; i++) {

    const SchemaMap *smap = schemas_map[i];
    const SchemaTagMap *stagmap =
        metadataparse_get_tagsmap_from_gsttag (smap, tag);

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

      if (value) {
        xmp_set_property (xmp, smap->schema, stagmap->xmp_tag, value);
        g_free (value);
      }

    }

  }

}

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
  if (*buf) {
    g_free (*buf);
    *buf = NULL;
  }
  *size = 0;

  val = gst_tag_list_get_value_index (taglist, GST_TAG_XMP, 0);
  if (val) {
    xmp_chunk = gst_value_get_buffer (val);
    if (xmp_chunk)
      xmp = xmp_new (GST_BUFFER_DATA (xmp_chunk), GST_BUFFER_SIZE (xmp_chunk));
  }

  if (NULL == xmp)
    xmp = xmp_new_empty ();

  gst_tag_list_foreach (taglist, metadataxmp_for_each_tag_in_list, xmp);

  if (!xmp_serialize (xmp, xmp_str_buf, 0, 2)) {
    GST_ERROR ("failed to serialize xmp into chunk\n");
  } else if (xmp_str_buf) {
    unsigned int len = strlen (xmp_string_cstr (xmp_str_buf));

    *size = len + 1;
    *buf = malloc (*size);
    memcpy (*buf, xmp_string_cstr (xmp_str_buf), *size);
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

#endif /* else (ifndef HAVE_XMP) */
