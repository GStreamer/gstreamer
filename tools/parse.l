%{
#include "ges-structure-parser.h"

%}

%option noyywrap
%option nounput
%option reentrant
%option extra-type="GESStructureParser *"
%option never-interactive
%option noinput

CLIP 		[ ]+--clip[ ]+|[ ]+-c[ ]+
TRANSITION	[ ]+--transition[ ]+|[ ]+-t[ ]+
EFFECT		[ ]+--effect[ ]+|[ ]+-e[ ]+

SETTER		[ ]+--set-.[ ]+

%%

\"(\\.|[^"])*\"  {
		ges_structure_parser_parse_string (yyextra, yytext, FALSE);
}

{CLIP}|{TRANSITION}|{EFFECT}   {
		ges_structure_parser_parse_symbol (yyextra, yytext);
}

{SETTER}	 {
	ges_structure_parser_parse_setter (yyextra, yytext);
}

[ \t\n]+ 	{
		ges_structure_parser_parse_whitespace (yyextra);
}

. 		{
		/* add everything else */
		ges_structure_parser_parse_default (yyextra, yytext);
}

%%
