%{
#include <glib.h>
#include <stdio.h>
#define YYDEBUG 1
#define YYERROR_VERBOSE 1
%}

%union {
    double d;
    gboolean b;
    gint i;
    gchar *s;
}

%token <s> IDENTIFIER STRING
%token <d> FLOAT
%token <i> INTEGER
%token <b> BOOLEAN

%left '{' '}' '(' ')'
%left '!' '='
%left '+'
%left '.'

%start graph
%%

id:     IDENTIFIER {}
        ;

qid:            id
        |       id '.' id
        ;

value:          STRING {}
        |       FLOAT {}
        |       INTEGER {}
        |       BOOLEAN {}
        ;

property_value: qid '=' value
        ;

element:        id
        |       bin
        ;

graph:          /* empty */
        |       graph element
        |       graph connection
        |       graph property_value
        ;

bin:            '{' graph '}'
        |       id '.' '(' graph ')'
        ;

connection:     lconnection
        |       rconnection
        |       bconnection
        ;

lconnection:    qid '+' '!'
        ;

rconnection:    '!' '+' qid
        ;

bconnection:    '!'
        |       qid '+' bconnection '+' qid
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
    
    return yyparse();
}
