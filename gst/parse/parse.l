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

#define YY_DECL int _gst_parse_yylex (YYSTYPE *lvalp)
#define YY_NO_UNPUT
%}

_integer [[:digit:]]+
_double [[:digit:]]+"."*[[:digit:]]*
_number {_integer}|{_double}
_boolean "true"|"false"|"TRUE"|"FALSE"
_identifier [[:alpha:]][[:alnum:]\-_%:]*
_lconnection ({_identifier}\.)?{_identifier}!
_rconnection !({_identifier}\.)?{_identifier}
_bconnection ({_identifier}\.)?{_identifier}!({_identifier}\.)?{_identifier}
_char ([^[:space:]])|("\\".)
_string {_char}+|("\""([^\"]|"\\\"")*"\"")

%x value
%option noyywrap
%%

<value>{
    {_integer} {
        PRINT ("An integer: %s (%d)\n", yytext,
               atoi (yytext));
        lvalp->v = g_new0 (GValue, 1);
        g_value_init (lvalp->v, G_TYPE_INT);
        g_value_set_int (lvalp->v, atoi (yytext));
        BEGIN (INITIAL);
        return VALUE;
    }
    
    {_double}	{
        PRINT ("A double: %s (%g)\n", yytext, atof (yytext));
        lvalp->v = g_new0 (GValue, 1);
        g_value_init (lvalp->v, G_TYPE_DOUBLE);
        g_value_set_double (lvalp->v, atof (yytext));
        BEGIN (INITIAL);
        return VALUE;
    }
    
    {_boolean} {
        PRINT ("A boolean: %s (%d)\n", yytext, tolower (*yytext) == 't' ? 1 : 0);
        lvalp->v = g_new0 (GValue, 1);
        g_value_init (lvalp->v, G_TYPE_BOOLEAN);
        g_value_set_boolean (lvalp->v, tolower (*yytext) == 't' ? TRUE : FALSE);
        BEGIN (INITIAL);
        return VALUE;
    }
    
    {_string} {
        if (*yytext == '"') {
            yytext++;
            *(yytext + strlen (yytext) - 1) = '\0';
        }
        _gst_parse_unescape (yytext);
        PRINT ("A string: \"%s\"\n", yytext);
        lvalp->v = g_new0 (GValue, 1);
        g_value_init (lvalp->v, G_TYPE_STRING);
        g_value_set_string (lvalp->v, yytext);
        BEGIN (INITIAL);
        return VALUE;
    }
    
    [[:space:]]+ { /* PRINT ("space: [%s]\n", yytext); */ }
}

{_lconnection} {
    gchar *d1, *q;
    lvalp->c = g_new0 (connection_t, 1);
    PRINT ("An connection: %s\n", yytext);
    q = strchr (yytext, '!');
    d1 = strchr (yytext, '.');
    if (d1) {
        lvalp->c->src_name = g_strndup (yytext, d1 - yytext);
        lvalp->c->src_pads = g_list_append (lvalp->c->src_pads, g_strndup (d1 + 1, q - d1 - 1));
    } else {
        lvalp->c->src_pads = g_list_append (lvalp->c->src_pads, g_strndup (yytext, q - yytext));
    }
    
    return CONNECTION;
}

{_rconnection} {
    gchar *d2;
    lvalp->c = g_new0 (connection_t, 1);
    PRINT ("An rconnection: %s\n", yytext);
    d2 = strchr (yytext, '.');
    if (d2) {
        lvalp->c->sink_name = g_strndup (yytext + 1, d2 - yytext - 1);
        lvalp->c->sink_pads = g_list_append (lvalp->c->sink_pads, g_strdup (d2 + 1));
    } else {
        lvalp->c->sink_pads = g_list_append (lvalp->c->sink_pads, g_strdup (yytext + 1));
    }

    return CONNECTION;
}

{_bconnection} {
    gchar *d1, *d2, *q;
    lvalp->c = g_new0 (connection_t, 1);
    PRINT ("A bconnection: %s\n", yytext);
    q = strchr (yytext, '!');
    d1 = strchr (yytext, '.');
    d2 = strchr (q, '.');
    if (d1 && d1 < q) {
        lvalp->c->src_name = g_strndup (yytext, d1 - yytext);
        lvalp->c->src_pads = g_list_append (lvalp->c->src_pads, g_strndup (d1 + 1, q - d1 - 1));
    } else {
        lvalp->c->src_pads = g_list_append (lvalp->c->src_pads, g_strndup (yytext, q - yytext));
    }
    
    if (d2) {
        lvalp->c->sink_name = g_strndup (q + 1, d2 - q - 1);
        lvalp->c->sink_pads = g_list_append (lvalp->c->sink_pads, g_strdup (d2 + 1));
    } else {
        lvalp->c->sink_pads = g_list_append (lvalp->c->sink_pads, g_strdup (q + 1));
    }

    return BCONNECTION;
}

{_identifier} {
    PRINT ("An identifier: %s\n", yytext);
    lvalp->s = g_strdup (yytext);
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
