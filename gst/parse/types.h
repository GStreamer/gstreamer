#include <glib-object.h>
#include "../gstelement.h"

typedef struct {
    gchar *type;
    gint index;
    GList *property_values;
    GstElement *element;
} element_t;

typedef struct {
    gchar *name;
    GValue *value;
} property_t;

typedef struct {
    /* if the names are present, upon connection we'll search out the pads of the
       proper name and use those. otherwise, we'll search for elements of src_index
       and sink_index. */
    char *src_name;
    char *sink_name;
    int src_index;
    int sink_index;
    GList *src_pads;
    GList *sink_pads;
} connection_t;

typedef struct _graph_t graph_t;

struct _graph_t {
    element_t *first;
    element_t *current;
    graph_t *parent;
    gchar *current_bin_type;
    GList *elements;
    GList *connections;
    GList *connections_pending;
    GList *bins;
    GstElement *bin;
};

graph_t * _gst_parse_launch (const gchar *str, GError **error);

gchar *_gst_parse_escape (const gchar *str);
void _gst_parse_unescape (gchar *str);
