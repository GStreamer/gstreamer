%{
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <glib.h>
#include <grammar.tab.h>

#define CHAR(x) printf ("char: %c\n", *yytext); return *yytext;
%}

_integer [[:digit:]]+
_float [[:digit:]]+"."*[[:digit:]]*
_number {_integer}|{_float}
_boolean "true"|"false"|"TRUE"|"FALSE"
_identifier [[:alpha:]][[:alnum:]\-_]*
_string ([^[:space:]\"]|"\\\"")+|("\""([^\"]|"\\\"")*"\"")

%x value
%option noyywrap
%%

<value>{
    {_integer} {
        printf ("An integer: %s (%d)\n", yytext,
                atoi (yytext));
        BEGIN (INITIAL);
        return INTEGER;
    }
    
    {_float}	{
        printf ("A float: %s (%g)\n", yytext, atof (yytext));
        BEGIN (INITIAL);
        return FLOAT;
    }
    
    {_boolean} {
        printf ("A boolean: %s (%d)\n", yytext, tolower (*yytext) == 't' ? 1 : 0);
        BEGIN (INITIAL);
        return BOOLEAN;
    }
    
    {_string} {
        if (*yytext == '"') {
            yytext++;
            *(yytext + strlen (yytext) - 1) = '\0';
        }
        printf ("A string: %s\n", yytext);
        BEGIN (INITIAL);
        return STRING;
    }
    
    [[:space:]]+ { /* printf ("space: [%s]\n", yytext); */ }
    
    . {
        printf ("unknown: %s\n", yytext);
    }
}

{_identifier} {
    printf ("An identifier: %s\n", yytext);
}

"=" { BEGIN (value); CHAR ('='); }
"@" { CHAR ('@'); }
"." { CHAR ('.'); }
"," { CHAR (','); }
"{" { CHAR ('{'); }
"}" { CHAR ('}'); }
"[" { CHAR ('['); }
"]" { CHAR (']'); }
"(" { CHAR ('('); }
")" { CHAR (')'); }
"!" { CHAR ('!'); }
"+" { CHAR ('+'); }

[[:space:]]+ { /* printf ("space: [%s]\n", yytext); */ }

. {
    printf ("unknown: %s\n", yytext);
}

%%
