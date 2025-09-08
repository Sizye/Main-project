%option c++
%option noyywrap

%{
#include <iostream>
using namespace std;
%}

DIGIT       [0-9]
ID          [a-zA-Z_][a-zA-Z0-9_]*

%%

"var"               { cout << "KEYWORD_VAR\n"; }
"type"              { cout << "KEYWORD_TYPE\n"; }
"routine"           { cout << "KEYWORD_ROUTINE\n"; }
"print"             { cout << "KEYWORD_PRINT\n"; }
"if"                { cout << "KEYWORD_IF\n"; }
"else"              { cout << "KEYWORD_ELSE\n"; }
"while"             { cout << "KEYWORD_WHILE\n"; }
"for"               { cout << "KEYWORD_FOR\n"; }
"in"                { cout << "KEYWORD_IN\n"; }
"reverse"           { cout << "KEYWORD_REVERSE\n"; }
"return"            { cout << "KEYWORD_RETURN\n"; }
"is"                { cout << "KEYWORD_IS\n"; }
"end"               { cout << "KEYWORD_END\n"; }
"true"              { cout << "BOOL_LITERAL (true)\n"; }
"false"             { cout << "BOOL_LITERAL (false)\n"; }

":="                { cout << "ASSIGN\n"; }
":"                 { cout << "COLON\n"; }
","                 { cout << "COMMA\n"; }
";"                 { cout << "SEMICOLON\n"; }
"("                 { cout << "LPAREN\n"; }
")"                 { cout << "RPAREN\n"; }
"["                 { cout << "LBRACKET\n"; }
"]"                 { cout << "RBRACKET\n"; }

"and"               { cout << "AND_OP\n"; }
"or"                { cout << "OR_OP\n"; }
"xor"               { cout << "XOR_OP\n"; }
"not"               { cout << "NOT_OP\n"; }

"<="                { cout << "LE_OP\n"; }
">="                { cout << "GE_OP\n"; }
"<"                 { cout << "LT_OP\n"; }
">"                 { cout << "GT_OP\n"; }
"="                 { cout << "EQ_OP\n"; }
"/="                { cout << "NEQ_OP\n"; }

"%"                 { cout << "MOD_OP\n"; }
"+"                 { cout << "PLUS_OP\n"; }
"-"                 { cout << "MINUS_OP\n"; }
"*"                 { cout << "MUL_OP\n"; }
"/"                 { cout << "DIV_OP\n"; }

{DIGIT}+"."{DIGIT}+  { cout << "REAL_LITERAL: " << YYText() << "\n"; }
{DIGIT}+            { cout << "INT_LITERAL: " << YYText() << "\n"; }

{ID}                { cout << "IDENTIFIER: " << YYText() << "\n"; }

[ \t\r\n]+          { /* skip whitespace */ }
"//".*              { /* skip single-line comments */ }

.                   { cout << "UNKNOWN_CHAR: " << YYText() << "\n"; }

%%

int main() {
    yyFlexLexer scanner;
    while(scanner.yylex() != 0);
    return 0;
}

