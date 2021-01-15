%{
#include "ges-structure-parser.h"

%}

%option noyywrap
%option nounput
%option reentrant
%option extra-type="GESStructureParser *"
%option never-interactive
%option noinput
%option nounistd

CLIP            [ ]+\+clip[ ]+
TEST_CLIP       [ ]+\+test-clip[ ]+
TRANSITION      [ ]+\+transition[ ]+
EFFECT          [ ]+\+effect[ ]+
TITLE           [ ]+\+title[ ]+
TRACK           [ ]+\+track[ ]+
KEYFRAME        [ ]+\+keyframes[ ]+

SETTER          [ ]+set-[^ ]+[ ]+

STRING          \"(\\.|[^"])*\"
/* A value string, as understood by gst_structure_from_string
 * Characters are from GST_ASCII_IS_STRING
 * NOTE: character set is *not* supposed to be locale dependent */
VALUE           {STRING}|([abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_+/:.-]+)

%%

={VALUE}        {
		ges_structure_parser_parse_value (yyextra, yytext);
}

{STRING}        {
		ges_structure_parser_parse_string (yyextra, yytext, FALSE);
}

{KEYFRAME}|{TRACK}|{CLIP}|{TRANSITION}|{EFFECT}|{TEST_CLIP}|{TITLE}   {
		ges_structure_parser_parse_symbol (yyextra, yytext);
}

{SETTER}        {
		ges_structure_parser_parse_setter (yyextra, yytext);
}

[ \t\n]+        {
		ges_structure_parser_parse_whitespace (yyextra);
}

.               {
		/* add everything else */
		ges_structure_parser_parse_default (yyextra, yytext);
}

%%
