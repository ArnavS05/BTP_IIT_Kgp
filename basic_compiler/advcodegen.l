%option noyywrap yylineno
%x COMMENT

%{
#include "y.tab.h"
#include <stdlib.h>

extern YYLTYPE yylloc;
char *copy_text(const char *text);
void lexer_error(const char *text);

#define YY_USER_ACTION do { \
    yylloc.first_line=yylineno; \
    yylloc.last_line =yylineno; \
    yylloc.first_column=1; \
    yylloc.last_column =1; \
} while(0);
%}

DIGIT       [0-9]
ID          [A-Za-z_][A-Za-z_0-9]*
INTEGER     {DIGIT}+
REAL1       {DIGIT}+\.{DIGIT}*
REAL2       \.{DIGIT}+
EXPONENT    [eE][+-]?{DIGIT}+
REAL        (({REAL1}|{REAL2})({EXPONENT})?)|({INTEGER}{EXPONENT})

%%

"//"[^\n]*             { }
"/*"                   { BEGIN(COMMENT); }
<COMMENT>"*/"          { BEGIN(INITIAL); }
<COMMENT>\n            { }
<COMMENT>.             { }
<COMMENT><<EOF>>       { lexer_error("unterminated comment"); return 0; }

"struct"               { return STRUCT; }
"long"[ \t]+"int"      { return LNG; }
"long"                 { return LNG; }
"int"                  { return INT; }
"float"                { return FLT; }
"double"               { return DBL; }
"if"                   { return IF; }
"else"                 { return ELSE; }
"while"                { return WHILE; }

"=="                   { return EQ; }
"!="                   { return NEQ; }
"<="                   { return LEQ; }
">="                   { return GEQ; }
"&&"                   { return AND; }
"||"                   { return OR; }
"!"                    { return NOT; }

{REAL}                 { yylval.real=strtod(yytext,NULL); return FLTCONST; }
{INTEGER}              { yylval.number=strtoll(yytext,NULL,10); return NUM; }
{ID}                   { yylval.text=copy_text(yytext); return ID; }

[ \t\r\n]+             { }

[;,\[\]{}.+\-*/%()=<>] { return yytext[0]; }

.                      { lexer_error(yytext); }

%%
