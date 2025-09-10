%option c++
%option noyywrap
%{
#include <iostream>
using namespace std;
%}

DIGIT       [0-9]
ID          [a-zA-Z_][a-zA-Z0-9_]*

%%

"var"               { cout << "KEYWORD_VAR\n";}
"type"              { cout << "KEYWORD_TYPE\n";}
"routine"           { cout << "KEYWORD_ROUTINE\n";}
"print"             { cout << "KEYWORD_PRINT\n";}
"if"                { cout << "KEYWORD_IF\n";}
"else"              { cout << "KEYWORD_ELSE\n";}
"while"             { cout << "KEYWORD_WHILE\n";}
"for"               { cout << "KEYWORD_FOR\n";}
"in"                { cout << "KEYWORD_IN\n";}
"reverse"           { cout << "KEYWORD_REVERSE\n";}
"return"            { cout << "KEYWORD_RETURN\n";}
"is"                { cout << "KEYWORD_IS\n";}
"end"               { cout << "KEYWORD_END\n";}
"loop"              { cout << "KEYWORD_LOOP\n";}
"then"              { cout << "KEYWORD_THEN\n";}
"record"            { cout << "KEYWORD_RECORD\n";}
"array"             { cout << "KEYWORD_ARRAY\n";}
"size"              { cout << "KEYWORD_SIZE\n";}  // Added for array size

"true"              { cout << "BOOL_LITERAL\n";}
"false"             { cout << "BOOL_LITERAL\n";}

":="                { cout << "ASSIGN\n";}
":"                 { cout << "COLON\n";}
","                 { cout << "COMMA\n";}
";"                 { cout << "SEMICOLON\n";}
"("                 { cout << "LPAREN\n";}
")"                 { cout << "RPAREN\n";}
"["                 { cout << "LBRACKET\n";}
"]"                 { cout << "RBRACKET\n";}
".."                { cout << "DOTDOT\n";}
"=>"                { cout << "EQ_GT\n";}
"."                 { cout << "DOT\n";}           // Added for member access

"and"               { cout << "AND_OP\n";}
"or"                { cout << "OR_OP\n";}
"xor"               { cout << "XOR_OP\n";}
"not"               { cout << "NOT_OP\n";}

"<="                { cout << "LE_OP\n";}
">="                { cout << "GE_OP\n";}
"<"                 { cout << "LT_OP\n";}
">"                 { cout << "GT_OP\n";}
"="                 { cout << "EQ_OP\n";}
"/="                { cout << "NEQ_OP\n";}

"%"                 { cout << "MOD_OP\n";}
"+"                 { cout << "PLUS_OP\n";}
"-"                 { cout << "MINUS_OP\n";}
"*"                 { cout << "MUL_OP\n";}
"/"                 { cout << "DIV_OP\n";}

"integer"           { cout << "TYPE_INTEGER\n";}
"real"              { cout << "TYPE_REAL\n";}
"boolean"           { cout << "TYPE_BOOLEAN\n";}

{DIGIT}+"."{DIGIT}+  {
                        cout << "REAL_LITERAL\n";
                    }
{DIGIT}+            {
                        cout << "INT_LITERAL\n";
                    }
{ID}                {
                        cout << "IDENTIFIER\n";
                    }

[ \t\r\n]+          { /* skip whitespace */ }
"//".*              { /* skip single-line comments */ }

.                   { /* unknown character */ 
                      return 0;
                    }

%%

int main() {
    yyFlexLexer scanner;
    while(scanner.yylex() != 0);
    return 0;
}

