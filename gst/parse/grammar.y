%{
#include <glib.h>
#include <stdio.h>
#include "types.h"
#define YYDEBUG 1
#define YYERROR_VERBOSE 1
%}

%union {
    double d;
    gboolean b;
    gint i;
    gchar *s;
    graph_t *g;
    connection_t *c;
    property_t *p;
    element_t *e;
    hash_t *h;
}

%token <s> IDENTIFIER STRING
%token <d> FLOAT
%token <i> INTEGER
%token <b> BOOLEAN

%type <s> id
%type <h> qid
%type <g> graph bin
%type <e> element
%type <p> property_value value
%type <c> connection lconnection rconnection qconnection iconnection

%left '{' '}' '(' ')'
%left '!' '='
%left '+'
%left '.'

%start graph
%%

id:     IDENTIFIER
        ;

qid:            id                   { $$ = g_new0 (hash_t, 1); $$->id2 = $1 }
        |       id '.' id            { $$ = g_new0 (hash_t, 1); $$->id1 = $1; $$->id2 = $3; }
        ;

value:          STRING               { $$ = g_new0 (property_t, 1); 
                                       $$->value_type = G_TYPE_STRING; $$->value.s = $1; }
        |       FLOAT                { $$ = g_new0 (property_t, 1);
                                       $$->value_type = G_TYPE_DOUBLE; $$->value.d = $1; }
        |       INTEGER              { $$ = g_new0 (property_t, 1);
                                       $$->value_type = G_TYPE_INT; $$->value.i = $1; }
        |       BOOLEAN              { $$ = g_new0 (property_t, 1);
                                       $$->value_type = G_TYPE_BOOLEAN; $$->value.b = $1; }
        ;

property_value: id '=' value         { $$ = $3; $$->name = $1; }
        ;

element:        id                   { $$ = g_new0 (element_t, 1); $$->name = $1; }
        ;

graph:          /* empty */          { $$ = g_new0 (graph_t, 1); }
        |       graph element        { GList *l = $$->connections_pending;
                                       $$ = $1;
                                       $$->elements = g_list_append ($$->elements, $2);
                                       $$->current = $2;
                                       while (l) {
                                           ((connection_t*) l->data)->sink = $$->current->name;
                                           l = g_list_next (l);
                                       }
                                       if ($$->connections_pending) {
                                           g_list_free ($$->connections_pending);
                                           $$->connections_pending = NULL;
                                       }
                                     }
        |       graph bin            { $$ = $1; $$->bins = g_list_append ($$->bins, $2); }
        |       graph connection     { $$ = $1; $$->connections = g_list_append ($$->connections, $2);
                                       if (!$2->src)
                                           $2->src = $$->current->name;
                                       if (!$2->sink)
                                           $$->connections_pending = g_list_append ($$->connections_pending, $2);
                                     }
        |       graph property_value { $$ = $1;
                                       $$->current->property_values = g_list_append ($$->current->property_values,
                                                                                     $2);
                                     }
        ;

bin:            '{' graph '}'        { $$ = $2; $$->current_bin_type = "gstthread"; }
        |       id '.' '(' graph ')' { $$ = $4; $$->current_bin_type = $1; }
        ;

connection:     lconnection
        |       rconnection
        |       qconnection
        |       iconnection
        ;

lconnection:    qid '+' '!'          { $$ = g_new0 (connection_t, 1);
                                       $$->src = $1->id1;
                                       $$->src_pads = g_list_append ($$->src_pads, $1->id2);
                                     }
        ;

rconnection:    '!' '+' qid          { $$ = g_new0 (connection_t, 1);
                                       $$->sink = $3->id1;
                                       $$->sink_pads = g_list_append ($$->src_pads, $3->id2);
                                     }
        ;

qconnection:    qid '+' '!' '+' qid  { $$ = g_new0 (connection_t, 1);
                                       $$->src = $1->id1;
                                       $$->src_pads = g_list_append ($$->src_pads, $1->id2);
                                       $$->sink = $5->id1;
                                       $$->sink_pads = g_list_append ($$->sink_pads, $5->id2);
                                     }
        ;

iconnection:   '!'                   { $$ = g_new0 (connection_t, 1); }
        |       id '+' iconnection '+' id 
                                     { $$ = $3;
                                       $$->src_pads = g_list_append ($$->src_pads, $1);
                                       $$->sink_pads = g_list_append ($$->sink_pads, $5);
                                     }
        ;

%%

extern FILE *yyin;

int
yyerror (const char *s)
{
  printf ("error: %s\n", s);
  return -1;
}

int main (int argc, char **argv)
{
    ++argv, --argc;  /* skip over program name */
    if ( argc > 0 )
        yyin = fopen (argv[0], "r");
    else
        yyin = stdin;

#ifdef DEBUG
    yydebug = 1;
#endif

    return yyparse();
}
