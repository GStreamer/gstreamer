%{
#include "ges-structure-parser.h"

%}

%option noyywrap
%option nounput
%option reentrant
%option extra-type="GESStructureParser *"
%option never-interactive
%option noinput

%%

\"(\\.|[^"])*\"  {
		ges_structure_parser_parse_string (yyextra, yytext, FALSE);
}

[ ]+--clip[ ]+|[ ]+-c[ ]+|[ ]+--transition[ ]+|[ ]+-t[ ]+|[ ]+--effect[ ]+|[ ]+-e[ ]+|[ ]+--set-property[ ]+|[ ]+-s[ ]+   {
		ges_structure_parser_parse_symbol (yyextra, yytext);
}

[ \t\n]+ 	{
		ges_structure_parser_parse_whitespace (yyextra);
}

. 		{
		/* add everything else */
		ges_structure_parser_parse_default (yyextra, yytext);
}

%%
