//
// dynamicsignal.c: new method of registering GObject signal
//   handlers that uses a default signal handler to read
//   callback argument stack and convert to an array of
//   GValues before running the "generic" signal handler
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// (C) 2006 Novell, Inc.
//

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gobject/gvaluecollector.h>

typedef void (* DynamicSignalHandler)(GObject *sender, guint argc, 
    GValue *argv, gpointer userdata);

//typedef gpointer (* DynamicSignalHandlerGPointer) (GObject *sender, guint argc,
//    GValue *argv, gpointer userdata);

//typedef gint64 (* DynamicSignalHandlerGint64) (GObject *sender, guint argc, GValue *argv, gpointer userdata);

typedef struct {
    GObject *object;
    gpointer userdata;

/*
    typedef union {
   	DynamicSignalHandler CBVoid;
	DynamicSignalHandlerGPointer CBGPointer;
	DynamicSignalHandlerGInt64 CBGInt64;  
    }
    CallBack;
*/
    DynamicSignalHandler callback;
    
    guint id;
    
    guint g_signal_id;
    gulong handler_id;
    GSignalQuery signal_details;
} DynamicSignalEntry;

static GList *dynamic_signal_table = NULL;
static guint dynamic_signal_last_id = 1;

static DynamicSignalEntry *
find_entry(GObject *object, guint signal_id)
{
    GList *current = dynamic_signal_table;
    
    while(current != NULL) {
        DynamicSignalEntry *entry = (DynamicSignalEntry *)current->data;
        if(entry->object == object && entry->g_signal_id == signal_id) {
            return entry;
        }

        current = current->next;
    }
    
    return NULL;
}

static DynamicSignalEntry *
find_entry_by_name(GObject *object, const gchar *signal_name, guint *signal_id_out)
{
    guint signal_id;
    
    signal_id = g_signal_lookup(signal_name, G_OBJECT_TYPE(object));
    if(signal_id_out != NULL) {
        *signal_id_out = signal_id;
    }

    if(signal_id <= 0) {
        return NULL;
    }
    
    return find_entry(object, signal_id);
}

static void
free_entry(DynamicSignalEntry *entry)
{
    memset(entry, 0, sizeof(DynamicSignalEntry));
    g_free(entry);
}

static void 
dynamic_signal_handler(DynamicSignalEntry *entry, ...)
{
    va_list argp;
    GValue *args = NULL;
    guint i, n;

    if(entry == NULL) {
        return;
    }

    n = entry->signal_details.n_params;
    
    if(n <= 0) {
        entry->callback(entry->object, 0, NULL, entry->userdata);
        return;
    }
    
    args = g_new0(GValue, n);
    va_start(argp, entry);
        
    for(i = 0; i < n; i++) {
        GType type = entry->signal_details.param_types[i];
        GValue *value = &args[i];
        gchar *collect_error = NULL;

        if(!G_TYPE_IS_CLASSED(type) && G_TYPE_IS_VALUE_TYPE(type) &&
            !G_TYPE_IS_FUNDAMENTAL(type) && G_TYPE_IS_DERIVED(type) &&
            G_TYPE_HAS_VALUE_TABLE(type)) {
            g_value_init(value, type);
            value->data[0].v_pointer = va_arg(argp, gpointer);
        } else {
            g_value_init(value, type);
            G_VALUE_COLLECT(value, argp, 0, &collect_error);        
        }
        
        if(collect_error != NULL) {
            g_warning("Could not collect value: %s", collect_error);
            g_free(collect_error);
            break;
        }
    }
    
    va_end(argp);
    entry->callback(entry->object, n, args, entry->userdata);
}

DynamicSignalEntry *
g_dynamic_signal_find_registration(GObject *object, const gchar *signal_name)
{
    return find_entry_by_name(object, signal_name, NULL);
}

guint 
g_dynamic_signal_connect(GObject *object, const gchar *signal_name,
    DynamicSignalHandler callback, gboolean after, gpointer userdata)
{
    DynamicSignalEntry *entry;
    guint signal_id;
    
    entry = find_entry_by_name(object, signal_name, &signal_id);
    
    if(entry != NULL) {
        return entry->id;
    }

    entry = g_new0(DynamicSignalEntry, 1);
    entry->id = dynamic_signal_last_id++;
    entry->object = object;
    entry->g_signal_id = signal_id;
    entry->callback = callback;
    entry->userdata = userdata;
    g_signal_query(signal_id, &(entry->signal_details));

    dynamic_signal_table = g_list_prepend(dynamic_signal_table, entry);
    
    entry->handler_id = g_signal_connect_data(object, signal_name, 
        G_CALLBACK(dynamic_signal_handler), entry, NULL, 
        G_CONNECT_SWAPPED | (after ? G_CONNECT_AFTER : 0));
    
    return entry->id;
}


void 
g_dynamic_signal_disconnect(GObject *object, const gchar *signal_name)
{
    DynamicSignalEntry *entry = find_entry_by_name(object, signal_name, NULL);
    
    if(entry == NULL) {
        return;
    }
    
    g_signal_handler_disconnect(object, entry->handler_id);
    
    dynamic_signal_table = g_list_remove(dynamic_signal_table, entry);
    free_entry(entry);
    
    entry = NULL;
}

void
g_dynamic_signal_update_entry_userdata(DynamicSignalEntry *entry, gpointer userdata)
{
    if(entry != NULL) {
       entry->userdata = userdata;
    }
}

GType g_value_type(GValue *value)
{
    return G_VALUE_TYPE(value);
}

