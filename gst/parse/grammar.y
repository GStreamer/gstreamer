%{
#include <glib.h>
#include <stdio.h>
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

%%

graph:          connection                { printf ("primary graph: connection\n"); }
        |       property_value            { printf ("primary graph: prop_value\n"); }
        |       element                   { printf ("primary graph: element\n"); }
        |       graph connection          { printf ("adding a connection to the graph\n"); }
        |       graph property_value      { printf ("adding a property=value pair to the graph\n"); }
        |       graph element             { printf ("adding on another element...\n"); }
        ;

property_value: property '=' value        { printf ("got property=value\n"); }
        ;

property:       identifier                { printf ("got unadorned property name\n"); }
        |       identifier '.' identifier { printf ("got qualified property name\n"); }
        ;

value:          STRING                    { printf ("got string\n"); }
        |       FLOAT                     { printf ("got float\n"); }
        |       INTEGER                   { printf ("got integer\n"); }
        |       BOOLEAN                   { printf ("got boolean\n"); }
        ;

element:        identifier                { printf ("got element\n"); }
        |       bin                       { printf ("new element, it's a bin\n"); }
        ;

bin:            '{' graph '}'             { printf ("new thread\n"); }
        |       identifier '.' '(' graph ')' { printf ("new named bin\n"); }
        ;

connection:     lconnection
        |       rconnection
        |       bconnection
        ;

lconnection:    pad_name '+' '!'          { printf ("got lconnection\n"); }
        ;

rconnection:    '!' '+' pad_name          { printf ("got rconnection\n"); }
        ;

bconnection:    '!'                       { printf ("got base bconnection\n"); }
        |       pad_name '+' '!' '+' pad_name { printf ("got bconnection with pads\n"); }
        |       pad_name ',' bconnection ',' pad_name { printf ("got multiple-pad bconnection\n"); }
        ;

pad_name:       identifier                { printf ("got pad\n"); }
        |       identifier '.' identifier { printf ("got named pad\n"); }
        ;

identifier:     IDENTIFIER                { printf ("matching on identifier\n");}
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
