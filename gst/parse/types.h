#include <glib-object.h>

typedef struct {
    gchar *name;
    GList *property_values;
} element_t;

typedef struct {
    gchar *name;
    GType value_type;
    union {
        gdouble d;
        gboolean b;
        gint i;
        gchar *s;
    } value;
} property_t;

typedef struct {
    char *src;
    char *sink;
    GList *src_pads;
    GList *sink_pads;
} connection_t;

typedef struct {
    element_t *current;
    gchar *current_bin_type;
    GList *elements;
    GList *connections;
    GList *connections_pending;
    GList *bins;
} graph_t;

typedef struct {
    gchar *id1;
    gchar *id2;
} hash_t;
