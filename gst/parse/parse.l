%{
#include <math.h>
#include <string.h>

#include "../gst_private.h"

#include "types.h"
#include "../gstinfo.h"
#include "../gsturi.h"
#include "grammar.tab.h"

#ifdef G_HAVE_ISO_VARARGS
#define PRINT(...) GST_CAT_DEBUG (GST_CAT_PIPELINE, "flex: " __VA_ARGS__)
#elif defined(G_HAVE_GNUC_VARARGS)
#define PRINT(args...) GST_CAT_DEBUG (GST_CAT_PIPELINE, "flex: " args)
#else
static inline void
PRINT (const char *format, ...)
{
  va_list varargs;

  va_start (varargs, format);
  GST_CAT_LEVEL_LOG_valist (GST_CAT_PIPELINE, GST_LEVEL_DEBUG, NULL,
    format, varargs);
  va_end (varargs);
}
#endif

#define YY_DECL int _gst_parse_yylex (YYSTYPE *lvalp)
%}

_operator [(){}.:!,;=]
_identifier [[:alpha:]][[:alnum:]\-_%]*

_char ("\\".)|([^[:space:]])
_string {_char}+|("\""([^\"]|"\\\"")*"\"")|("'"([^']|"\\\"")*"'")

_comma [[:space:]]*","[[:space:]]*
_assign [[:space:]]*"="[[:space:]]*

_protocol [[:alpha:]][[:alnum:]+-\.]*
_url {_protocol}"://"{_string}|["."{_identifier}]?"/"{_string}

/* we must do this here, because nearly everything matches a {_string} */ 
_assignment {_identifier}{_assign}{_string}

/* get pad/element references and stuff with dots right */
_padref "."{_identifier}
_ref {_identifier}"."{_identifier}?
_binref {_identifier}[[:space:]]*"."[[:space:]]*"("

/* links */
_mimechar ([[:alnum:]-])
_mimetype ({_mimechar}+"/"{_mimechar}+)|("\""([^\"]|"\\\"")*"\"")|("'"([^']|"\\\"")*"'")
_capschar ("\\".)|([^\;!])
_capsstring {_capschar}+|("\""([^\"]|"\\\"")*"\"")|("'"([^']|"\\\"")*"'")
_caps {_mimetype}(","[^!]|{_capsstring})*
_link ("!"[[:space:]]*{_caps}([[:space:]]*(";"[[:space:]]*{_caps})*[[:space:]]*)*"!")|("!")

%x value
%option noyywrap
%option nounput
%%

{_assignment} {
    /* "=" */
    PRINT ("ASSIGNMENT: %s", yytext);
    lvalp->s = gst_parse_strdup (yytext);
    BEGIN (INITIAL);
    return ASSIGNMENT;
}

{_padref} {
    yytext++;
    PRINT ("PADREF: %s", yytext);
    lvalp->s = gst_parse_strdup (yytext);
    BEGIN (INITIAL);
    return PADREF;
}

{_ref} {
    PRINT ("REF: %s", yytext);
    lvalp->s = gst_parse_strdup (yytext);
    BEGIN (INITIAL);
    return REF;
}

{_binref} {
    gchar *pos = yytext;
    while (!g_ascii_isspace (*pos) && (*pos != '.')) pos++;
    *pos = '\0';
    PRINT ("BINREF: %s", yytext);
    lvalp->s = gst_parse_strdup (yytext);
    BEGIN (INITIAL);
    return BINREF;
}

{_identifier} {
    PRINT ("IDENTIFIER: %s", yytext);
    lvalp->s = gst_parse_strdup (yytext);
    BEGIN (INITIAL);
    return IDENTIFIER;
}

{_link} {
    gchar *c = yytext;
    PRINT ("LINK: %s", yytext);
    c++;
    if (*c) {
      while (g_ascii_isspace (*c)) c++;
      c = lvalp->s = gst_parse_strdup (c);
      while (*c) c++;
      if (*--c != '!')
	g_assert_not_reached ();
      while (g_ascii_isspace (*--c));
      *++c = '\0';
    } else {
      lvalp->s = NULL;
    }
    BEGIN (INITIAL);
    return LINK;
}
{_url} {
  PRINT ("URL: %s", yytext);
  if (gst_uri_is_valid (yytext)) {
    lvalp->s = g_strdup (yytext);
  } else {
    lvalp->s = gst_uri_construct ("file", yytext);
  }
  gst_parse_unescape (lvalp->s);
  BEGIN (INITIAL);
  return PARSE_URL;
}

{_operator} { PRINT ("OPERATOR: [%s]", yytext); return *yytext; }

[[:space:]]+ { PRINT ("SPACE: [%s]", yytext); }

. {
    printf ("???: %s\n", yytext);
    return *yytext;
}

%%
