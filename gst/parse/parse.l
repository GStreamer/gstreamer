%{
#include <math.h>
#include <ctype.h>
#include <string.h>
#include "types.h"
#include <grammar.tab.h>

#ifdef DEBUG
# define PRINT(a...) printf(##a)
#else
#define PRINT(a...)
#endif

#define CHAR(x) PRINT ("char: %c\n", *yytext); return *yytext;
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
        PRINT ("An integer: %s (%d)\n", yytext,
                atoi (yytext));
        BEGIN (INITIAL);
        return INTEGER;
    }
    
    {_float}	{
        PRINT ("A float: %s (%g)\n", yytext, atof (yytext));
        BEGIN (INITIAL);
        return FLOAT;
    }
    
    {_boolean} {
        PRINT ("A boolean: %s (%d)\n", yytext, tolower (*yytext) == 't' ? 1 : 0);
        BEGIN (INITIAL);
        return BOOLEAN;
    }
    
    {_string} {
        if (*yytext == '"') {
            yytext++;
            *(yytext + strlen (yytext) - 1) = '\0';
        }
        PRINT ("A string: %s\n", yytext);
        BEGIN (INITIAL);
        return STRING;
    }
    
    [[:space:]]+ { /* PRINT ("space: [%s]\n", yytext); */ }
    
    . {
        PRINT ("unknown: %s\n", yytext);
    }
}

{_identifier} {
    PRINT ("An identifier: %s\n", yytext);
    return IDENTIFIER;
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

[[:space:]]+ { PRINT ("space: [%s]\n", yytext); }

. {
    printf ("unknown: %s\n", yytext);
    return *yytext;
}

%%
