%option noyywrap
%{
#include <stdlib.h>
#include <string.h>
#include "parser.tab.hpp"
extern YYSTYPE yylval;
extern int yylineno;
static int lexer_debug = 0;
#define DBG(tok) if(lexer_debug) \
        fprintf(stderr,"LEX %-15s '%s' line %d\n",tok,yytext,yylineno);
%}

DIGIT    [0-9]
ID       [a-zA-Z_][a-zA-Z0-9_]*
STRING   \"([^\"\n\\]|\\.)*\"
WS       [ \t\r]+

%%

{STRING}      {
                 size_t len = yyleng;
                 char *s = (char*)malloc(len-1);
                 memcpy(s,yytext+1,len-2);
                 s[len-2]='\0';
                 yylval.sval = s;
                 DBG("STRING_LITERAL");
                 return STRING_LITERAL;
             }

\n            { ++yylineno; }
"//".*        { /* skip */ }

"var"         { return KEYWORD_VAR; }
"type"        { return KEYWORD_TYPE; }
"routine"     { return KEYWORD_ROUTINE; }
"print"       { return KEYWORD_PRINT; }
"if"          { return KEYWORD_IF; }
"else"        { return KEYWORD_ELSE; }
"while"       { return KEYWORD_WHILE; }
"for"         { return KEYWORD_FOR; }
"in"          { return KEYWORD_IN; }
"reverse"     { return KEYWORD_REVERSE; }
"return"      { return KEYWORD_RETURN; }
"is"          { return KEYWORD_IS; }
"end"         { return KEYWORD_END; }
"loop"        { return KEYWORD_LOOP; }
"then"        { return KEYWORD_THEN; }
"record"      { return KEYWORD_RECORD; }
"array"       { return KEYWORD_ARRAY; }
"size"        { return KEYWORD_SIZE; }

"true"        { yylval.bval=true;  return BOOL_LITERAL; }
"false"       { yylval.bval=false; return BOOL_LITERAL; }

":="          { return ASSIGN; }
":"           { return COLON;  }
","           { return COMMA;  }
";"           { return SEMICOLON; }
"("           { return LPAREN; }
")"           { return RPAREN; }
"["           { return LBRACKET; }
"]"           { return RBRACKET; }
".."          { return DOTDOT; }
"=>"          { return EQ_GT; }
"."           { return DOT; }

"and"         { return AND_OP; }
"or"          { return OR_OP; }
"xor"         { return XOR_OP; }
"not"         { return NOT_OP; }

"<="          { return LE_OP; }
">="          { return GE_OP; }
"<"           { return LT_OP; }
">"           { return GT_OP; }
"="           { return EQ_OP; }
"/="          { return NEQ_OP; }

"%"           { return MOD_OP; }
"+"           { return PLUS_OP; }
"-"           { return MINUS_OP; }
"*"           { return MUL_OP; }
"/"           { return DIV_OP; }

"integer"     { return TYPE_INTEGER; }
"real"        { return TYPE_REAL; }
"boolean"     { return TYPE_BOOLEAN; }

{DIGIT}+"."{DIGIT}+   { yylval.dval = atof(yytext); return REAL_LITERAL; }
{DIGIT}+              { yylval.ival = atoi(yytext); return INT_LITERAL; }
{ID}                  { yylval.sval = strdup(yytext); return IDENTIFIER; }
{WS}                  { /* skip */ }
.                     { fprintf(stderr,"LEX ERROR: '%c' line %d\n",*yytext,yylineno); return 0; }

%%
