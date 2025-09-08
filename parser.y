%{
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern int yylex();
extern int yylineno;
void yyerror(const char *s);

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
%token KEYWORD_END ASSIGN COLON COMMA SEMICOLON LPAREN RPAREN LBRACKET RBRACKET
%token AND_OP OR_OP XOR_OP NOT_OP LE_OP GE_OP LT_OP GT_OP EQ_OP NEQ_OP MOD_OP
%token PLUS_OP MINUS_OP MUL_OP DIV_OP
%token EQ_GT           /* for => */
%token KEYWORD_LOOP    /* for "loop" */
%token DOTDOT          /* for ".." */
%token KEYWORD_THEN    /* for "then" */

%token <ival> INT_LITERAL
%token <dval> REAL_LITERAL
%token <bval> BOOL_LITERAL
%token <sval> IDENTIFIER


/* Define the start symbol */
%start program

%%

program:
    /* empty */
  | program declaration
  ;

declaration:
    SimpleDeclaration
  | RoutineDeclaration
  ;

SimpleDeclaration:
    VariableDeclaration
  | TypeDeclaration
  ;

VariableDeclaration:
    KEYWORD_VAR IDENTIFIER COLON type_opt is_expression_opt SEMICOLON
  | KEYWORD_VAR IDENTIFIER is_expression SEMICOLON
  ;

type_opt:
    IDENTIFIER     /* Type is identifier */
  |                 /* empty */
  ;

is_expression_opt:
    KEYWORD_IS expression
  |                /* empty */
  ;

is_expression:
    KEYWORD_IS expression
  ;

TypeDeclaration:
    KEYWORD_TYPE IDENTIFIER KEYWORD_IS type
  ;

type:
    IDENTIFIER
  | primitive_type
  ;

primitive_type:
    /* predefined types */
    "integer"
  | "real"
  | "boolean"
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
    declarations_opt statements_opt
  ;

declarations_opt:
    /* empty */
  | declarations
  ;

declarations:
    declaration
  | declarations declaration
  ;

statements_opt:
    /* empty */
  | statements
  ;

statements:
    statement
  | statements statement
  ;

statement:
    VariableDeclaration
  | routine_call SEMICOLON
  | Assignment SEMICOLON
  | WhileLoop
  | ForLoop
  | IfStatement
  | PrintStatement SEMICOLON
  ;

/* Define other rules as needed like expressions, routine_call, assignment, etc. */

/* Placeholder for actual Expression grammar */

expression:
    IDENTIFIER
  | INT_LITERAL
  | REAL_LITERAL
  | BOOL_LITERAL
  /* extend with full expressions */
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
  | ModifiablePrimary '.' IDENTIFIER
  | ModifiablePrimary LBRACKET expression RBRACKET
  ;

/* Control-flow constructs placeholders */

WhileLoop:
    KEYWORD_WHILE expression KEYWORD_LOOP body KEYWORD_END
  ;

ForLoop:
    KEYWORD_FOR IDENTIFIER KEYWORD_IN range reverse_opt KEYWORD_LOOP body KEYWORD_END
  ;

reverse_opt:
    /* empty */
  | KEYWORD_REVERSE
  ;

range:
    expression
  | expression DOTDOT expression
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
  fprintf(stderr, "Parse error at line %d: %s\n", yylineno, s);
}
