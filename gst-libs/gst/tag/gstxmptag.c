/* GStreamer
 * Copyright (C) 2010 Stefan Kost <stefan.kost@nokia.com>
 *
 * gstxmptag.c: library for reading / modifying xmp tags
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

/**
 * SECTION:gsttagxmp
 * @short_description: tag mappings and support functions for plugins
 *                     dealing with xmp packets
 * @see_also: #GstTagList
 *
 * Contains various utility functions for plugins to parse or create
 * xmp packets and map them to and from #GstTagList<!-- -->s.
 *
 * Please note that the xmp parser is very lightweight and not strict at all.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gsttagsetter.h>
#include "gsttageditingprivate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* look at this page for addtional schemas
 * http://www.sno.phy.queensu.ca/~phil/exiftool/TagNames/XMP.html
 */
static const GstTagEntryMatch tag_matches[] = {
  /* dublic code metadata
   * http://dublincore.org/documents/dces/
   */
  {GST_TAG_ARTIST, "dc:creator"},
  {GST_TAG_COPYRIGHT, "dc:rights"},
  {GST_TAG_DATE, "dc:date"},
  {GST_TAG_DATE, "exif:DateTimeOriginal"},
  {GST_TAG_DESCRIPTION, "dc:description"},
  {GST_TAG_KEYWORDS, "dc:subject"},
  {GST_TAG_TITLE, "dc:title"},
  /* FIXME: we probably want GST_TAG_{,AUDIO_,VIDEO_}MIME_TYPE */
  {GST_TAG_VIDEO_CODEC, "dc:format"},
  /* */
  {NULL, NULL}
};

typedef struct _GstXmpNamespaceMatch GstXmpNamespaceMatch;
struct _GstXmpNamespaceMatch
{
  const gchar *ns_prefix;
  const gchar *ns_uri;
};

static const GstXmpNamespaceMatch ns_match[] = {
  {"dc", "http://purl.org/dc/elements/1.1/"},
  {"exif", "http://ns.adobe.com/exif/1.0/"},
  {"tiff", "http://ns.adobe.com/tiff/1.0/"},
  {"xap", "http://ns.adobe.com/xap/1.0/"},
  {NULL, NULL}
};

typedef struct _GstXmpNamespaceMap GstXmpNamespaceMap;
struct _GstXmpNamespaceMap
{
  const gchar *original_ns;
  gchar *gstreamer_ns;
};
static GstXmpNamespaceMap ns_map[] = {
  {"dc", NULL},
  {"exif", NULL},
  {"tiff", NULL},
  {"xap", NULL},
  {NULL, NULL}
};

/* parsing */

static void
read_one_tag (GstTagList * list, const gchar * tag, const gchar * v)
{
  GType tag_type = gst_tag_get_type (tag);

  /* add gstreamer tag depending on type */
  switch (tag_type) {
    case G_TYPE_STRING:{
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, tag, v, NULL);
      break;
    }
    default:
      if (tag_type == GST_TYPE_DATE) {
        GDate *date;
        gint d, m, y;

        /* this is ISO 8601 Date and Time Format
         * %F     Equivalent to %Y-%m-%d (the ISO 8601 date format). (C99)
         * %T     The time in 24-hour notation (%H:%M:%S). (SU)
         * e.g. 2009-05-30T18:26:14+03:00 */

        /* FIXME: this would be the proper way, but needs
           #define _XOPEN_SOURCE before #include <time.h>

           date = g_date_new ();
           struct tm tm={0,};
           strptime (dts, "%FT%TZ", &tm);
           g_date_set_time_t (date, mktime(&tm));
         */
        /* FIXME: this cannot parse the date
           date = g_date_new ();
           g_date_set_parse (date, v);
           if (g_date_valid (date)) {
           gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, tag,
           date, NULL);
           } else {
           GST_WARNING ("unparsable date: '%s'", v);
           }
         */
        /* poor mans straw */
        sscanf (v, "%04d-%02d-%02dT", &y, &m, &d);
        date = g_date_new_dmy (d, m, y);
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, tag, date, NULL);
        g_date_free (date);
      } else {
        GST_WARNING ("unhandled type for %s from xmp", tag);
      }
      break;
  }
}

/**
 * gst_tag_list_from_xmp_buffer:
 * @list: buffer
 *
 * Parse a xmp packet into a taglist.
 *
 * Returns: new taglist or %NULL, free the list when done
 *
 * Since: 0.10.29
 */
GstTagList *
gst_tag_list_from_xmp_buffer (const GstBuffer * buffer)
{
  GstTagList *list = NULL;
  const gchar *xps, *xp1, *xp2, *xpe, *ns, *ne;
  guint len, max_ft_len;
  gboolean in_tag;
  gchar *part, *pp;
  guint i;
  const gchar *last_tag = NULL;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (GST_BUFFER_SIZE (buffer) > 0, NULL);

  xps = (const gchar *) GST_BUFFER_DATA (buffer);
  len = GST_BUFFER_SIZE (buffer);
  xpe = &xps[len + 1];

  /* check header and footer */
  xp1 = g_strstr_len (xps, len, "<?xpacket begin");
  if (!xp1)
    goto missing_header;
  xp1 = &xp1[strlen ("<?xpacket begin")];
  while (*xp1 != '>' && *xp1 != '<' && xp1 < xpe)
    xp1++;
  if (*xp1 != '>')
    goto missing_header;

  max_ft_len = 1 + strlen ("<?xpacket end=\".\"?>\n");
  if (len < max_ft_len)
    goto missing_footer;

  GST_DEBUG ("checking footer: [%s]", &xps[len - max_ft_len]);
  xp2 = g_strstr_len (&xps[len - max_ft_len], max_ft_len, "<?xpacket ");
  if (!xp2)
    goto missing_footer;

  GST_INFO ("xmp header okay");

  /* skip > and text until first xml-node */
  xp1++;
  while (*xp1 != '<' && xp1 < xpe)
    xp1++;

  /* no tag can be longer that the whole buffer */
  part = g_malloc (xp2 - xp1);
  list = gst_tag_list_new ();

  /* parse data into a list of nodes */
  /* data is between xp1..xp2 */
  in_tag = TRUE;
  ns = ne = xp1;
  pp = part;
  while (ne < xp2) {
    if (in_tag) {
      ne++;
      while (ne < xp2 && *ne != '>' && *ne != '<') {
        if (*ne == '\n' || *ne == '\t' || *ne == ' ') {
          while (ne < xp2 && (*ne == '\n' || *ne == '\t' || *ne == ' '))
            ne++;
          *pp++ = ' ';
        } else {
          *pp++ = *ne++;
        }
      }
      *pp = '\0';
      if (*ne != '>')
        goto broken_xml;
      /* create node */
      /* {XML, ns, ne-ns} */
      if (ns[0] != '/') {
        gchar *as = strchr (part, ' ');
        /* only log start nodes */
        GST_INFO ("xml: %s", part);

        if (as) {
          gchar *ae, *d;

          /* skip ' ' and scan the attributes */
          as++;
          d = ae = as;

          /* split attr=value pairs */
          while (*ae != '\0') {
            if (*ae == '=') {
              /* attr/value delimmiter */
              d = ae;
            } else if (*ae == '"') {
              /* scan values */
              gchar *v;

              ae++;
              while (*ae != '\0' && *ae != '"')
                ae++;

              *d = *ae = '\0';
              v = &d[2];
              GST_INFO ("   : [%s][%s]", as, v);
              if (!strncmp (as, "xmlns:", 6)) {
                i = 0;
                /* we need to rewrite known namespaces to what we use in
                 * tag_matches */
                while (ns_match[i].ns_prefix) {
                  if (!strcmp (ns_match[i].ns_uri, v))
                    break;
                  i++;
                }
                if (ns_match[i].ns_prefix) {
                  if (strcmp (ns_map[i].original_ns, &as[6])) {
                    ns_map[i].gstreamer_ns = g_strdup (&as[6]);
                  }
                }
              } else {
                /* FIXME: eventualy rewrite ns
                 * find ':'
                 * check if ns before ':' is in ns_map and ns_map[i].gstreamer_ns!=NULL
                 * do 2 stage filter in tag_matches
                 */
                i = 0;
                while (tag_matches[i].gstreamer_tag) {
                  if (!strcmp (tag_matches[i].original_tag, as))
                    break;
                  i++;
                }
                if (tag_matches[i].gstreamer_tag) {
                  read_one_tag (list, tag_matches[i].gstreamer_tag, v);
                }
              }
              /* restore chars overwritten by '\0' */
              *d = '=';
              *ae = '"';
            } else if (*ae == '\0' || *ae == ' ') {
              /* end of attr/value pair */
              as = &ae[1];
            }
            /* to next char if not eos */
            if (*ae != '\0')
              ae++;
          }
        } else {
          /*
             <dc:type><rdf:Bag><rdf:li>Image</rdf:li></rdf:Bag></dc:type>
             <dc:creator><rdf:Seq><rdf:li/></rdf:Seq></dc:creator>
           */
          /* FIXME: eventualy rewrite ns */

          /* skip rdf tags for now */
          if (strncmp (part, "rdf:", 4)) {
            i = 0;
            while (tag_matches[i].gstreamer_tag) {
              if (!strcmp (tag_matches[i].original_tag, part))
                break;
              i++;
            }
            if (tag_matches[i].gstreamer_tag) {
              last_tag = tag_matches[i].gstreamer_tag;
            }
          }
        }
      }
      /* next cycle */
      ne++;
      if (ne < xp2) {
        if (*ne != '<')
          in_tag = FALSE;
        ns = ne;
        pp = part;
      }
    } else {
      while (ne < xp2 && *ne != '<') {
        *pp++ = *ne;
        ne++;
      }
      *pp = '\0';
      /* create node */
      /* {TXT, ns, (ne-ns)-1} */
      if (ns[0] != '\n' && &ns[1] < ne) {
        /* only log non-newline nodes, we still have to parse them */
        GST_INFO ("txt: %s", part);
        if (last_tag) {
          read_one_tag (list, last_tag, part);
        }
      }
      /* next cycle */
      in_tag = TRUE;
      ns = ne;
      pp = part;
    }
  }
  GST_INFO ("xmp packet parsed, %d entries",
      gst_structure_n_fields ((GstStructure *) list));

  /* free resources */
  i = 0;
  while (ns_map[i].original_ns) {
    g_free (ns_map[i].gstreamer_ns);
    i++;
  }
  g_free (part);

  return list;

  /* Errors */
missing_header:
  GST_WARNING ("malformed xmp packet header");
  return NULL;
missing_footer:
  GST_WARNING ("malformed xmp packet footer");
  return NULL;
broken_xml:
  GST_WARNING ("malformed xml tag: %s", part);
  return NULL;
}


/* formatting */

static void
write_one_tag (const GstTagList * list, const gchar * tag, gpointer user_data)
{
  guint i = 0, ct = gst_tag_list_get_tag_size (list, tag);
  GType tag_type = gst_tag_get_type (tag);
  GString *data = user_data;
  const gchar *xmp_tag = NULL, *fmt;

  /* map gst-tag to xmp tag */
  while (tag_matches[i].gstreamer_tag != NULL) {
    if (strcmp (tag, tag_matches[i].gstreamer_tag) == 0) {
      xmp_tag = tag_matches[i].original_tag;
      break;
    }
    i++;
  }

  if (!xmp_tag) {
    GST_WARNING ("no mapping for %s to xmp", tag);
    return;
  }

  /* write single or multi valued field */
  if (ct > 1) {
    g_string_append_printf (data, "<%s><rdf:Bag>", xmp_tag);
    fmt = "<rdf:li>%s</rdf:li>";
  } else {
    g_string_append_printf (data, "<%s>", xmp_tag);
    fmt = "%s";
  }
  for (i = 0; i < ct; i++) {
    GST_DEBUG ("mapping %s[%u/%u] to xmp", tag, i, ct);
    switch (tag_type) {
      case G_TYPE_STRING:{
        gchar *str;

        if (!gst_tag_list_get_string_index (list, tag, i, &str))
          g_return_if_reached ();
        g_string_append_printf (data, fmt, str);
        g_free (str);
        break;
      }
      default:
        if (tag_type == GST_TYPE_DATE) {
          GDate *date;
          gchar *str;

          if (!gst_tag_list_get_date_index (list, tag, i, &date))
            g_return_if_reached ();

          str = g_strdup_printf ("%04d-%02d-%02d",
              (gint) g_date_get_year (date), (gint) g_date_get_month (date),
              (gint) g_date_get_day (date));
          g_string_append_printf (data, fmt, str);
          g_free (str);
          g_date_free (date);
        } else {
          GST_WARNING ("unhandled type for %s to xmp", tag);
        }
        break;
    }
  }
  if (ct > 1) {
    g_string_append_printf (data, "</rdf:Bag></%s>\n", xmp_tag);
  } else {
    g_string_append_printf (data, "</%s>\n", xmp_tag);
  }
}

/**
 * gst_tag_list_to_xmp_buffer:
 * @list: tags
 * @read_only: does the container forbid inplace editing
 *
 * Formats a taglist as a xmp packet.
 *
 * Returns: new buffer or %NULL, unref the buffer when done
 *
 * Since: 0.10.29
 */
GstBuffer *
gst_tag_list_to_xmp_buffer (const GstTagList * list, gboolean read_only)
{
  GstBuffer *buffer = NULL;
  GString *data = g_string_sized_new (4096);
  guint i;

  g_return_val_if_fail (GST_IS_TAG_LIST (list), NULL);

  /* xmp header */
  g_string_append (data,
      "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n");
  g_string_append (data,
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"GStreamer\">\n");
  g_string_append (data,
      "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"");
  i = 0;
  while (ns_match[i].ns_prefix) {
    g_string_append_printf (data, " xmlns:%s=\"%s\"", ns_match[i].ns_prefix,
        ns_match[i].ns_uri);
    i++;
  }
  g_string_append (data, ">\n");
  g_string_append (data, "<rdf:Description rdf:about=\"\">\n");

  /* iterate the taglist */
  gst_tag_list_foreach (list, write_one_tag, data);

  /* xmp footer */
  g_string_append (data, "</rdf:Description>\n");
  g_string_append (data, "</rdf:RDF>\n");
  g_string_append (data, "</x:xmpmeta>\n");

  if (!read_only) {
    /* the xmp spec recommand to add 2-4KB padding for in-place editable xmp */
    guint i;

    for (i = 0; i < 32; i++) {
      g_string_append (data, "                " "                "
          "                " "                " "\n");
    }
  }
  g_string_append_printf (data, "<?xpacket end=\"%c\"?>\n",
      (read_only ? 'r' : 'w'));

  buffer = gst_buffer_new ();
  GST_BUFFER_SIZE (buffer) = data->len + 1;
  GST_BUFFER_DATA (buffer) = (guint8 *) g_string_free (data, FALSE);
  GST_BUFFER_MALLOCDATA (buffer) = GST_BUFFER_DATA (buffer);

  return buffer;
}
