%{
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
    link_t *c;
    property_t *p;
    element_t *e;
}

%token <s> IDENTIFIER
%token <c> LINK BLINK FLINK
%token <v> VALUE

%type <s> id
%type <g> graph bin
%type <e> element
%type <p> property_value value
%type <c> link rlink

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
                                       $$ = $1; l = $$->links_pending;
                                       $$->elements = g_list_append ($$->elements, $2);
                                       $$->current = $2;
                                       if (!$$->first)
                                           $$->first = $$->current;
                                       while (l) {
                                           ((link_t*) l->data)->sink_index = $$->current->index;
                                           l = g_list_next (l);
                                       }
                                       if ($$->links_pending) {
                                           g_list_free ($$->links_pending);
                                           $$->links_pending = NULL;
                                       }
                                     }
        |       graph bin            { GList *l; $$ = $1; l = $$->links_pending;
                                       *((graph_t**) pgraph) = $$;
                                       $$->bins = g_list_append ($$->bins, $2);
                                       $2->parent = $$;
                                       $$->current = $2->first;
                                       if (!$$->first)
                                           $$->first = $$->current;
                                       while (l) {
                                           ((link_t*) l->data)->sink_index = $$->current->index;
                                           l = g_list_next (l);
                                       }
                                       if ($$->links_pending) {
                                           g_list_free ($$->links_pending);
                                           $$->links_pending = NULL;
                                       }
                                       $$->current = $2->current;
                                     }
        |       graph link     { $$ = $1;
                                       $$->links = g_list_append ($$->links, $2);
				       if ($$->current)
                                         $2->src_index = $$->current->index;
                                       if (!$2->sink_name)
                                           $$->links_pending = g_list_append ($$->links_pending, $2);
                                     }
        |       graph property_value { $$ = $1;
                                       if (!$$->current) {
                                           fprintf (stderr, "error: property value assignments must be preceded by an element definition\n");
                                           YYABORT;
                                       }
                                       $$->current->property_values = g_list_append ($$->current->property_values,
                                                                                     $2);
                                     }
        ;

bin:            '{' graph '}'        { $$ = $2; $$->current_bin_type = "thread"; }
        |       id '.' '(' graph ')' { $$ = $4; $$->current_bin_type = $1; }
        ;

link:     LINK
        |       rlink
        ;

rlink:   '!'                   { $$ = g_new0 (link_t, 1); }
        |       BLINK          { $$ = $1; }
        |       FLINK          { $$ = $1; }
        |       id ',' rlink ',' id 
                                     { $$ = $3;
                                       $$->src_pads = g_list_prepend ($$->src_pads, $1);
                                       $$->sink_pads = g_list_append ($$->sink_pads, $5);
                                     }
        ;

%%

extern FILE *_gst_parse_yyin;
int _gst_parse_yylex (YYSTYPE *lvalp);

static int yylex (void *lvalp) {
    return _gst_parse_yylex ((YYSTYPE*) lvalp);
}

static int
yyerror (const char *s)
{
  fprintf (stderr, "error: %s\n", s);
  return -1;
}

int _gst_parse_yy_scan_string (char*);

graph_t * _gst_parse_launch (const gchar *str, GError **error)
{
    graph_t *g = NULL;
    gchar *dstr;
    
    g_return_val_if_fail (str != NULL, NULL);

    dstr = g_strdup (str);
    _gst_parse_yy_scan_string (dstr);

#ifdef DEBUG
    _gst_parse_yydebug = 1;
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

    /* if the toplevel only contains one bin, make that bin top-level */
    if (g->elements == NULL && g->bins && g->bins->next == NULL) {
        g = (graph_t*)g->bins->data;
        g_free (g->parent);
        g->parent = NULL;
    }

    return g;
}
