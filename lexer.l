%option c++
%option noyywrap
%{
#include <iostream>
#include <fstream>
#include <string>

using namespace std;
    std::ifstream inputFile;
    std::ofstream outputFile;
%}



DIGIT       [0-9]
ID          [a-zA-Z_][a-zA-Z0-9_]*

%%

"var"               { outputFile << "KEYWORD_VAR\n"; }
"type"              { outputFile << "KEYWORD_TYPE\n"; }
"routine"           { outputFile << "KEYWORD_ROUTINE\n"; }
"print"             { outputFile << "KEYWORD_PRINT\n"; }
"if"                { outputFile << "KEYWORD_IF\n"; }
"else"              { outputFile << "KEYWORD_ELSE\n"; }
"while"             { outputFile << "KEYWORD_WHILE\n"; }
"for"               { outputFile << "KEYWORD_FOR\n"; }
"in"                { outputFile << "KEYWORD_IN\n"; }
"reverse"           { outputFile << "KEYWORD_REVERSE\n"; }
"return"            { outputFile << "KEYWORD_RETURN\n"; }
"is"                { outputFile << "KEYWORD_IS\n"; }
"end"               { outputFile << "KEYWORD_END\n"; }
"loop"              { outputFile << "KEYWORD_LOOP\n"; }
"then"              { outputFile << "KEYWORD_THEN\n"; }
"record"            { outputFile << "KEYWORD_RECORD\n"; }
"array"             { outputFile << "KEYWORD_ARRAY\n"; }
"size"              { outputFile << "KEYWORD_SIZE\n"; }

"true"              { outputFile << "BOOL_LITERAL\n"; }
"false"             { outputFile << "BOOL_LITERAL\n"; }

":="                { outputFile << "ASSIGN\n"; }
":"                 { outputFile << "COLON\n"; }
","                 { outputFile << "COMMA\n"; }
";"                 { outputFile << "SEMICOLON\n"; }
"("                 { outputFile << "LPAREN\n"; }
")"                 { outputFile << "RPAREN\n"; }
"["                 { outputFile << "LBRACKET\n"; }
"]"                 { outputFile << "RBRACKET\n"; }
".."                { outputFile << "DOTDOT\n"; }
"=>"                { outputFile << "EQ_GT\n"; }
"."                 { outputFile << "DOT\n"; }

"and"               { outputFile << "AND_OP\n"; }
"or"                { outputFile << "OR_OP\n"; }
"xor"               { outputFile << "XOR_OP\n"; }
"not"               { outputFile << "NOT_OP\n"; }

"<="                { outputFile << "LE_OP\n"; }
">="                { outputFile << "GE_OP\n"; }
"<"                 { outputFile << "LT_OP\n"; }
">"                 { outputFile << "GT_OP\n"; }
"="                 { outputFile << "EQ_OP\n"; }
"/="                { outputFile << "NEQ_OP\n"; }

"%"                 { outputFile << "MOD_OP\n"; }
"+"                 { outputFile << "PLUS_OP\n"; }
"-"                 { outputFile << "MINUS_OP\n"; }
"*"                 { outputFile << "MUL_OP\n"; }
"/"                 { outputFile << "DIV_OP\n"; }

"integer"           { outputFile << "TYPE_INTEGER\n"; }
"real"              { outputFile << "TYPE_REAL\n"; }
"boolean"           { outputFile << "TYPE_BOOLEAN\n"; }

{DIGIT}+"."{DIGIT}+ { outputFile << "REAL_LITERAL\n"; }
{DIGIT}+            { outputFile << "INT_LITERAL\n"; }
{ID}                { outputFile << "IDENTIFIER\n"; }

[ \t\r\n]+          { /* skip whitespace */ }
"//".*              { /* skip single-line comments */ }

.                   { /* unknown character */ 
                      return 0;
                    }

%%

int main(int argc, char** argv) {
    // Check command line arguments
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <input_file> <output_file>" << endl;
        return 1;
    }
    
    // Open input file
    inputFile.open(argv[1]);
    if (!inputFile.is_open()) {
        cerr << "Error: Cannot open input file '" << argv[1] << "'" << endl;
        return 1;
    }
    
    // Open output file
    outputFile.open(argv[2]);
    if (!outputFile.is_open()) {
        cerr << "Error: Cannot open output file '" << argv[2] << "'" << endl;
        inputFile.close();
        return 1;
    }
    
    // Create lexer and set input stream
    yyFlexLexer scanner(&inputFile, &outputFile);
    
    // Tokenize the input
    while(scanner.yylex() != 0);
    
    // Close files
    inputFile.close();
    outputFile.close();
    
    cout << "Tokenization complete. Output written to '" << argv[2] << "'" << endl;
    
    return 0;
}