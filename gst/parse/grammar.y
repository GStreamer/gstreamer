%{
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include "../gstparse.h"
#include "types.h"

#define YYDEBUG 1
#define YYERROR_VERBOSE 1
#define YYPARSE_PARAM pgraph

static int yylex (void *lvalp);
static int yyerror (const char *s);
%}

%union {
    gchar *s;
    GValue *v;
    graph_t *g;
    connection_t *c;
    property_t *p;
    element_t *e;
}

%token <s> IDENTIFIER
%token <c> CONNECTION BCONNECTION
%token <v> VALUE

%type <s> id
%type <g> graph bin
%type <e> element
%type <p> property_value value
%type <c> connection rconnection

%left '{' '}' '(' ')'
%left '!' '='
%left ','
%left '.'

%pure_parser

%start graph
%%

id:     IDENTIFIER
        ;

value:          VALUE                { $$ = g_new0 (property_t, 1); $$->value = $1; }
        ;

property_value: id '=' value         { $$ = $3; $$->name = $1; }
        ;

element:        id                   { static int i = 0; $$ = g_new0 (element_t, 1);
                                       $$->type = $1; $$->index = ++i; }
        ;

graph:          /* empty */          { $$ = g_new0 (graph_t, 1); *((graph_t**) pgraph) = $$; }
        |       graph element        { GList *l;
                                       $$ = $1; l = $$->connections_pending;
                                       $$->elements = g_list_append ($$->elements, $2);
                                       $$->current = $2;
                                       if (!$$->first)
                                           $$->first = $$->current;
                                       while (l) {
                                           ((connection_t*) l->data)->sink_index = $$->current->index;
                                           l = g_list_next (l);
                                       }
                                       if ($$->connections_pending) {
                                           g_list_free ($$->connections_pending);
                                           $$->connections_pending = NULL;
                                       }
                                     }
        |       graph bin            { GList *l; $$ = $1; l = $$->connections_pending;
                                       *((graph_t**) pgraph) = $$;
                                       $$->bins = g_list_append ($$->bins, $2);
                                       $2->parent = $$;
                                       $$->current = $2->first;
                                       if (!$$->first)
                                           $$->first = $$->current;
                                       while (l) {
                                           ((connection_t*) l->data)->sink_index = $$->current->index;
                                           l = g_list_next (l);
                                       }
                                       if ($$->connections_pending) {
                                           g_list_free ($$->connections_pending);
                                           $$->connections_pending = NULL;
                                       }
                                       $$->current = $2->current;
                                     }
        |       graph connection     { $$ = $1;
                                       $$->connections = g_list_append ($$->connections, $2);
                                       $2->src_index = $$->current->index;
                                       if (!$2->sink_name)
                                           $$->connections_pending = g_list_append ($$->connections_pending, $2);
                                     }
        |       graph property_value { $$ = $1;
                                       $$->current->property_values = g_list_append ($$->current->property_values,
                                                                                     $2);
                                     }
        ;

bin:            '{' graph '}'        { $$ = $2; $$->current_bin_type = "thread"; }
        |       id '.' '(' graph ')' { $$ = $4; $$->current_bin_type = $1; }
        ;

connection:     CONNECTION
        |       rconnection
        ;

rconnection:   '!'                   { $$ = g_new0 (connection_t, 1); }
        |       BCONNECTION          { $$ = $1; }
        |       id ',' rconnection ',' id 
                                     { $$ = $3;
                                       $$->src_pads = g_list_prepend ($$->src_pads, $1);
                                       $$->sink_pads = g_list_append ($$->sink_pads, $5);
                                     }
        ;

%%

extern FILE *yyin;
int _gst_parse_yylex (YYSTYPE *lvalp);

static int yylex (void *lvalp) {
    return _gst_parse_yylex ((YYSTYPE*) lvalp);
}

static int
yyerror (const char *s)
{
  printf ("error: %s\n", s);
  return -1;
}

int yy_scan_string (char*);

graph_t * _gst_parse_launch (const gchar *str, GError **error)
{
    graph_t *g = NULL;
    gchar *dstr;
    
    g_return_val_if_fail (str != NULL, NULL);

    dstr = g_strdup (str);
    yy_scan_string (dstr);

#ifdef DEBUG
    yydebug = 1;
#endif

    if (yyparse (&g) != 0) {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_SYNTAX,
                     "Invalid syntax");
        g_free (dstr);
        return NULL;
    }
    
    g_assert (g != NULL);

    g_free (dstr);

    return g;
}
