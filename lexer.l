%option c++
%option noyywrap
%{
#include <cstdlib>
#include <cstring>
#include "tokens.hpp"  // Token definitions

// Declare Bison semantic value variable (YYSTYPE defined in parser)
extern YYSTYPE yylval;

%}

DIGIT       [0-9]
ID          [a-zA-Z_][a-zA-Z0-9_]*

%%

"var"               { return KEYWORD_VAR; }
"type"              { return KEYWORD_TYPE; }
"routine"           { return KEYWORD_ROUTINE; }
"print"             { return KEYWORD_PRINT; }
"if"                { return KEYWORD_IF; }
"else"              { return KEYWORD_ELSE; }
"while"             { return KEYWORD_WHILE; }
"for"               { return KEYWORD_FOR; }
"in"                { return KEYWORD_IN; }
"reverse"           { return KEYWORD_REVERSE; }
"return"            { return KEYWORD_RETURN; }
"is"                { return KEYWORD_IS; }
"end"               { return KEYWORD_END; }

"true"              { yylval.bval = true; return BOOL_LITERAL; }
"false"             { yylval.bval = false; return BOOL_LITERAL; }

":="                { return ASSIGN; }
":"                 { return COLON; }
","                 { return COMMA; }
";"                 { return SEMICOLON; }
"("                 { return LPAREN; }
")"                 { return RPAREN; }
"["                 { return LBRACKET; }
"]"                 { return RBRACKET; }

"and"               { return AND_OP; }
"or"                { return OR_OP; }
"xor"               { return XOR_OP; }
"not"               { return NOT_OP; }

"<="                { return LE_OP; }
">="                { return GE_OP; }
"<"                 { return LT_OP; }
">"                 { return GT_OP; }
"="                 { return EQ_OP; }
"/="                { return NEQ_OP; }

"%"                 { return MOD_OP; }
"+"                 { return PLUS_OP; }
"-"                 { return MINUS_OP; }
"*"                 { return MUL_OP; }
"/"                 { return DIV_OP; }

{DIGIT}+"."{DIGIT}+  {
                        yylval.dval = atof(YYText());
                        return REAL_LITERAL;
                    }
{DIGIT}+            {
                        yylval.ival = atoi(YYText());
                        return INT_LITERAL;
                    }
{ID}                {
                        yylval.sval = strdup(YYText());
                        return IDENTIFIER;
                    }

[ \t\r\n]+          { /* skip whitespace */ }
"//".*              { /* skip single-line comments */ }

.                   { /* unknown character error or ignore*/ return YYUNDEF; }

%%

