%{
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif
int yylex();
#ifdef __cplusplus
}
#endif

extern int yylineno;
extern char* yytext;
void yyerror(const char *s);

void print_context(const char* s) {
    fprintf(stderr, "Parse error at line %d: %s\n", yylineno, s);
    fprintf(stderr, "Near: '%s'\n", yytext);
}

%}

%union {
    int ival;
    double dval;
    bool bval;
    char* sval;
}

/* Token declarations */
%token KEYWORD_VAR KEYWORD_TYPE KEYWORD_ROUTINE KEYWORD_PRINT KEYWORD_IF KEYWORD_ELSE
%token KEYWORD_WHILE KEYWORD_FOR KEYWORD_IN KEYWORD_REVERSE KEYWORD_RETURN KEYWORD_IS
%token KEYWORD_END KEYWORD_LOOP KEYWORD_THEN KEYWORD_RECORD KEYWORD_ARRAY KEYWORD_SIZE
%token ASSIGN COLON COMMA SEMICOLON LPAREN RPAREN LBRACKET RBRACKET DOTDOT EQ_GT DOT
%token AND_OP OR_OP XOR_OP NOT_OP LE_OP GE_OP LT_OP GT_OP EQ_OP NEQ_OP
%token MOD_OP PLUS_OP MINUS_OP MUL_OP DIV_OP
%token TYPE_INTEGER TYPE_REAL TYPE_BOOLEAN
%token <ival> INT_LITERAL
%token <dval> REAL_LITERAL
%token <bval> BOOL_LITERAL
%token <sval> IDENTIFIER

/* Precedence and associativity */
%left OR_OP
%left XOR_OP
%left AND_OP
%left EQ_OP NEQ_OP
%left LT_OP LE_OP GT_OP GE_OP
%left PLUS_OP MINUS_OP
%left MUL_OP DIV_OP MOD_OP
%right NOT_OP UMINUS

/* Define the start symbol */
%start program

%%

program:
    /* empty */
  | program top_level_declaration
  ;

top_level_declaration:
    SimpleDeclaration
  | RoutineDeclaration
  | routine_forward_declaration
  ;

/* REMOVED: declaration rule that allowed nested routines */

routine_forward_declaration:
    KEYWORD_ROUTINE IDENTIFIER LPAREN parameters_opt RPAREN routine_return_opt SEMICOLON
  ;

SimpleDeclaration:
    VariableDeclaration
  | TypeDeclaration
  ;

VariableDeclaration:
    KEYWORD_VAR IDENTIFIER COLON type is_expression_opt SEMICOLON
  | KEYWORD_VAR IDENTIFIER KEYWORD_IS expression SEMICOLON
  ;

is_expression_opt:
    /* empty */
  | KEYWORD_IS expression
  ;

TypeDeclaration:
    KEYWORD_TYPE IDENTIFIER KEYWORD_IS type SEMICOLON
  ;

type:
    primitive_type
  | user_type
  | IDENTIFIER
  ;

primitive_type:
    TYPE_INTEGER
  | TYPE_REAL
  | TYPE_BOOLEAN
  ;

user_type:
    ArrayType
  | RecordType
  ;

ArrayType:
    KEYWORD_ARRAY LBRACKET expression RBRACKET type
  | KEYWORD_ARRAY LBRACKET RBRACKET type
  ;

RecordType:
    KEYWORD_RECORD record_fields KEYWORD_END
  ;

record_fields:
    /* empty */
  | record_fields VariableDeclaration
  ;

RoutineDeclaration:
    KEYWORD_ROUTINE IDENTIFIER LPAREN parameters_opt RPAREN routine_return_opt routine_body
  ;

parameters_opt:
    /* empty */
  | parameters
  ;

parameters:
    parameter
  | parameters COMMA parameter
  ;

parameter:
    IDENTIFIER COLON type
  ;

routine_return_opt:
    /* empty */
  | COLON type
  ;

routine_body:
    KEYWORD_IS body KEYWORD_END
  | EQ_GT expression
  ;

body:
    /* empty */
  | body body_item
  ;

/* FIXED: Only allow SimpleDeclaration (variables/types), NOT RoutineDeclaration */
body_item:
    SimpleDeclaration    /* CHANGED: No RoutineDeclaration here! */
  | statement
  ;

statement:
    routine_call SEMICOLON
  | Assignment SEMICOLON
  | WhileLoop
  | ForLoop  
  | IfStatement
  | PrintStatement SEMICOLON
  | ReturnStatement SEMICOLON
  ;

expression:
    relation
  | expression OR_OP relation
  | expression XOR_OP relation
  | expression AND_OP relation
  ;

relation:
    simple
  | relation LT_OP simple
  | relation LE_OP simple
  | relation GT_OP simple
  | relation GE_OP simple
  | relation EQ_OP simple
  | relation NEQ_OP simple
  ;

simple:
    factor
  | simple PLUS_OP factor
  | simple MINUS_OP factor
  ;

factor:
    term
  | factor MUL_OP term
  | factor DIV_OP term
  | factor MOD_OP term
  ;

term:
    primary
  | PLUS_OP term %prec UMINUS
  | MINUS_OP term %prec UMINUS
  | NOT_OP term
  ;

primary:
    INT_LITERAL
  | REAL_LITERAL
  | BOOL_LITERAL
  | ModifiablePrimary
  | routine_call
  | LPAREN expression RPAREN
  | KEYWORD_SIZE LPAREN primary RPAREN
  ;

routine_call:
    IDENTIFIER LPAREN arguments_opt RPAREN
  ;

arguments_opt:
    /* empty */
  | arguments
  ;

arguments:
    expression
  | arguments COMMA expression
  ;

Assignment:
    ModifiablePrimary ASSIGN expression
  ;

ModifiablePrimary:
    IDENTIFIER
  | ModifiablePrimary DOT IDENTIFIER
  | ModifiablePrimary LBRACKET expression RBRACKET
  ;

WhileLoop:
    KEYWORD_WHILE expression KEYWORD_LOOP body KEYWORD_END
  ;

ForLoop:
    KEYWORD_FOR IDENTIFIER KEYWORD_IN range reverse_opt KEYWORD_LOOP body KEYWORD_END
  | KEYWORD_FOR IDENTIFIER KEYWORD_IN expression reverse_opt KEYWORD_LOOP body KEYWORD_END
  ;

reverse_opt:
    /* empty */
  | KEYWORD_REVERSE
  ;

range:
    expression DOTDOT expression
  ;

IfStatement:
    KEYWORD_IF expression KEYWORD_THEN body else_opt KEYWORD_END
  ;

else_opt:
    /* empty */
  | KEYWORD_ELSE body
  ;

PrintStatement:
    KEYWORD_PRINT expression_list
  ;

expression_list:
    expression
  | expression_list COMMA expression
  ;

ReturnStatement:
    KEYWORD_RETURN
  | KEYWORD_RETURN expression
  ;

%%

int main() {
    printf("Starting parsing...\n");
    if (yyparse() == 0)
        printf("Parse successful!\n");
    else
        printf("Parse failed.\n");
    return 0;
}

void yyerror(const char* s) {
    print_context(s);
}