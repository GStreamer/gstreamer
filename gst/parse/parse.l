%{
#include <math.h>
#include <ctype.h>
#include <string.h>
#include "types.h"
#include <grammar.tab.h>

#ifdef G_HAVE_ISO_VARARGS

#ifdef DEBUG
# define PRINT(...) printf(__VAR_ARGS__)
#else
#define PRINT(...)
#endif

#elif defined(G_HAVE_GNUC_VARARGS)

#ifdef DEBUG
# define PRINT(a...) printf(##a)
#else
#define PRINT(a...)
#endif

#endif

#define CHAR(x) PRINT ("char: %c\n", *yytext); return *yytext;

#define YY_DECL int _gst_parse_yylex (YYSTYPE *lvalp)
#define YY_NO_UNPUT
%}

_integer [-+]?[[:digit:]]+
_double [-+]?[[:digit:]]+"."*[[:digit:]]*
_number {_integer}|{_double}
_boolean "true"|"false"|"TRUE"|"FALSE"
_identifier [[:alpha:]][[:alnum:]\-_%:]*
_char ([^[:space:]])|("\\".)
_string {_char}+|("\""([^\"]|"\\\"")*"\"")
_comma [[:space:]]*","[[:space:]]*
_assign [[:space:]]*"="[[:space:]]*
_caps_type_string "fourcc"|"string"
_caps_type_double "float"
_caps_string {_string}{_assign}{_caps_type_string}[[:space:]]+{_string}
_caps_int {_string}{_assign}"int"[[:space:]]+{_integer}
_caps_double {_string}{_assign}{_caps_type_double}[[:space:]]+{_double}
_caps_boolean {_string}{_assign}"boolean"[[:space:]]+{_boolean}
_caps_pair {_caps_string}|{_caps_int}|{_caps_double}|{_caps_boolean}
_caps {_string}({_comma}{_caps_pair})*
_llink ({_identifier}\.)?{_identifier}!
_rlink !({_identifier}\.)?{_identifier}
_blink ({_identifier}\.)?{_identifier}!({_identifier}\.)?{_identifier}
_flink ({_identifier}\.)?{_identifier}!{_caps}!({_identifier}\.)?{_identifier}

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

{_llink} {
    gchar *d1, *q;
    lvalp->c = g_new0 (link_t, 1);
    PRINT ("An link: %s\n", yytext);
    q = strchr (yytext, '!');
    d1 = strchr (yytext, '.');
    if (d1) {
        lvalp->c->src_name = g_strndup (yytext, d1 - yytext);
        lvalp->c->src_pads = g_list_append (lvalp->c->src_pads, g_strndup (d1 + 1, q - d1 - 1));
    } else {
        lvalp->c->src_pads = g_list_append (lvalp->c->src_pads, g_strndup (yytext, q - yytext));
    }
    
    return LINK;
}

{_rlink} {
    gchar *d2;
    lvalp->c = g_new0 (link_t, 1);
    PRINT ("An rlink: %s\n", yytext);
    d2 = strchr (yytext, '.');
    if (d2) {
        lvalp->c->sink_name = g_strndup (yytext + 1, d2 - yytext - 1);
        lvalp->c->sink_pads = g_list_append (lvalp->c->sink_pads, g_strdup (d2 + 1));
    } else {
        lvalp->c->sink_pads = g_list_append (lvalp->c->sink_pads, g_strdup (yytext + 1));
    }

    return LINK;
}

{_blink} {
    gchar *d1, *d2, *q;
    lvalp->c = g_new0 (link_t, 1);
    PRINT ("A blink: %s\n", yytext);
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

    return BLINK;
}

{_flink} {
    gchar *d1, *d2, *q1, *q2, *a1, *m1;
    gchar *mime;
    GstProps *props;

    lvalp->c = g_new0 (link_t, 1);
    PRINT ("An flink: %s\n", yytext);
    q1 = strchr (yytext, '!');
    d1 = strchr (yytext, '.');
    q2 = strchr (q1+1, '!');
    d2 = strchr (q2, '.');
    if (d1 && d1 < q1) {
        lvalp->c->src_name = g_strndup (yytext, d1 - yytext);
        lvalp->c->src_pads = g_list_append (lvalp->c->src_pads, g_strndup (d1 + 1, q1 - d1 - 1));
    } else {
        lvalp->c->src_pads = g_list_append (lvalp->c->src_pads, g_strndup (yytext, q1 - yytext));
    }
    
    if (d2) {
        lvalp->c->sink_name = g_strndup (q2 + 1, d2 - q2 - 1);
        lvalp->c->sink_pads = g_list_append (lvalp->c->sink_pads, g_strdup (d2 + 1));
    } else {
        lvalp->c->sink_pads = g_list_append (lvalp->c->sink_pads, g_strdup (q2 + 1));
    }
    /* parse mime type */
    m1 = strchr (q1 + 1, ',');
    mime = g_strndup (q1 + 1, m1 - q1 - 1);

    props = gst_props_empty_new ();

    a1 = strchr (m1 + 1, ',');
    if (a1 == NULL)
      a1 = q2;

    while (a1 && a1 <= q2) {
      gchar *key, *t, *v;
      gchar *k1, *k2;
      GstPropsEntry *entry = NULL;

      k1 = strchr (m1, '=');
      key = g_strstrip (g_strndup (m1 + 1, k1 - m1 -1));

      k1++;

      while (g_ascii_isspace (*k1)) k1++;

      k2 = strchr (k1, ' ');
      t = g_strstrip (g_strndup (k1, k2 - k1));

      while (g_ascii_isspace (*k2)) k2++;

      v = g_strstrip (g_strndup (k2, a1 - k2));

      if (!strcmp (t, "string")) {
        entry = gst_props_entry_new (key, GST_PROPS_STRING (v));
      }
      else if (!strcmp (t, "fourcc")) {
        entry = gst_props_entry_new (key, GST_PROPS_FOURCC (GST_STR_FOURCC(v)));
      }
      else if (!strcmp (t, "float")) {
        gfloat f;
	sscanf (v, "%f", &f);
        entry = gst_props_entry_new (key, GST_PROPS_FLOAT (f));
      }
      else if (!strcmp (t, "int")) {
        gint i;
	sscanf (v, "%d", &i);
        entry = gst_props_entry_new (key, GST_PROPS_INT (i));
      }
      else if (!strcmp (t, "boolean")) {
        gboolean b;
        b = (!strcmp (v, "true") || ! strcmp (v, "TRUE"));
        entry = gst_props_entry_new (key, GST_PROPS_BOOLEAN (b));
      }
      gst_props_add_entry (props, entry);
      
      m1 = a1;
      if (a1 < q2) {
        a1 = strchr (m1 + 1, ',');
        if (a1 == NULL) 
          a1 = q2;
      }
      else
        break;
    }
    lvalp->c->caps = gst_caps_new ("parse_caps", mime, props);

    return FLINK;
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
