#ifndef __GST_PARSE_TYPES_H__
#define __GST_PARSE_TYPES_H__

#include <glib-object.h>
#include "../gstelement.h"

typedef struct {
  GstElement *src;
  GstElement *sink;
  gchar *src_name;
  gchar *sink_name;
  GSList *src_pads;
  GSList *sink_pads;
  GstCaps *caps;
} link_t;

typedef struct {
  GSList *elements;
  GstElement *first;
  GstElement *last;
  link_t *front;
  link_t *back;
} chain_t;

typedef struct _graph_t graph_t;
struct _graph_t {
  chain_t *chain; /* links are supposed to be done now */
  GSList *links;
  GError **error;
};


/*
 * Memory checking. Should probably be done with gsttrace stuff, but that
 * doesn't really work.
 * This is not safe from reentrance issues, but that doesn't matter as long as
 * we lock a mutex before parsing anyway.
 */
#ifdef GST_DEBUG_ENABLED
#  define __GST_PARSE_TRACE
#endif

#ifdef __GST_PARSE_TRACE
gchar  *__gst_parse_strdup (gchar *org);
void	__gst_parse_strfree (gchar *str);
link_t *__gst_parse_link_new ();
void	__gst_parse_link_free (link_t *data);
chain_t *__gst_parse_chain_new ();
void	__gst_parse_chain_free (chain_t *data);
#  define gst_parse_strdup __gst_parse_strdup
#  define gst_parse_strfree __gst_parse_strfree
#  define gst_parse_link_new __gst_parse_link_new
#  define gst_parse_link_free __gst_parse_link_free
#  define gst_parse_chain_new __gst_parse_chain_new
#  define gst_parse_chain_free __gst_parse_chain_free
#else /* __GST_PARSE_TRACE */
#  define gst_parse_strdup g_strdup
#  define gst_parse_strfree g_free
#  define gst_parse_link_new() g_new0 (link_t, 1)
#  define gst_parse_link_free g_free
#  define gst_parse_chain_new() g_new0 (chain_t, 1)
#  define gst_parse_chain_free g_free
#endif /* __GST_PARSE_TRACE */

static inline void
gst_parse_unescape (gchar *str)
{
  gchar *walk;

  g_return_if_fail (str != NULL);

  walk = str;

  while (*walk) {
    if (*walk == '\\')
      walk++;
    *str = *walk;
    str++;
    walk++;
  }
  *str = '\0';
}

#endif /* __GST_PARSE_TYPES_H__ */
