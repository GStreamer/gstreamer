%{
#include <math.h>
#include <ctype.h>
#include <string.h>
#include "types.h"
#include "../gstinfo.h"
#include "grammar.tab.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef G_HAVE_ISO_VARARGS
#  ifdef GST_DEBUG_ENABLED
#    define PRINT(...) GST_DEBUG (GST_CAT_PIPELINE, "flex: "__VA_ARGS__)
#  endif
#elif defined(G_HAVE_GNUC_VARARGS)
#  ifdef GST_DEBUG_ENABLED
#    define PRINT(...) GST_DEBUG (GST_CAT_PIPELINE, "flex: "##args)
#  endif
#else
#  ifdef GST_DEBUG_ENABLED
#    define PRINT(a...) GST_DEBUG (GST_CAT_PIPELINE, "flex: "##a)
#  endif
#endif /* G_HAVE_ISO_VARARGS */

#define YY_DECL int _gst_parse_yylex (YYSTYPE *lvalp)
%}

_operators [(){}.:!,=]
_identifier [[:alpha:]][[:alnum:]\-_%]*

_char ("\\".)|([^[:space:]])
_string {_char}+|("\""([^\"]|"\\\"")*"\"")

_comma [[:space:]]*","[[:space:]]*
_assign [[:space:]]*"="[[:space:]]*

/* we must do this here, because nearly everything matches a {_string} */ 
_assignment {_identifier}{_assign}{_string}

/* get pad/element references and stuff with dots right */
_padref "."{_identifier}
_ref {_identifier}"."{_identifier}?
_binref {_identifier}[[:space:]]*"."[[:space:]]*"("

%x value
%option noyywrap
%option nounput
%%

{_assignment} {
    /* "=" */
    PRINT ("ASSIGNMENT: %s\n", yytext);
    lvalp->s = gst_parse_strdup (yytext);
    BEGIN (INITIAL);
    return ASSIGNMENT;
}

{_padref} {
    yytext++;
    PRINT ("PADREF: %s\n", yytext);
    lvalp->s = gst_parse_strdup (yytext);
    BEGIN (INITIAL);
    return PADREF;
}

{_ref} {
    PRINT ("REF: %s\n", yytext);
    lvalp->s = gst_parse_strdup (yytext);
    BEGIN (INITIAL);
    return REF;
}

{_binref} {
    gchar *pos = yytext;
    while (!g_ascii_isspace (*pos) && (*pos != '.')) pos++;
    *pos = '\0';
    PRINT ("BINREF: %s\n", yytext);
    lvalp->s = gst_parse_strdup (yytext);
    BEGIN (INITIAL);
    return BINREF;
}

{_identifier} {
    PRINT ("IDENTIFIER: %s\n", yytext);
    lvalp->s = gst_parse_strdup (yytext);
    BEGIN (INITIAL);
    return IDENTIFIER;
}

{_operators} { PRINT ("OPERATOR: [%s]\n", yytext); return *yytext; }

[[:space:]]+ { PRINT ("SPACE: [%s]\n", yytext); }

. {
    printf ("???: %s\n", yytext);
    return *yytext;
}

%%
