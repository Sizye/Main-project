%code requires {
    #include <memory>
    #include <fstream>
    class ASTNode;
}
%{
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include <fstream>
#include "ast.h"
#include "wasm_compiler.h"
int yylex();

extern int yylineno;
extern char* yytext;
void yyerror(const char *s);
int runParser();
extern FILE* yyin;

int debug_enabled = 1;
int ast_debug = 1;
static int indent_level = 0;

void debug_print(const char* rule_name, const char* production) {
    if (!debug_enabled) return;
    for (int i = 0; i < indent_level; i++) {
        fprintf(stderr, "  ");
    }
    fprintf(stderr, "PARSING: %s -> %s (line %d)\n", rule_name, production, yylineno);
    indent_level++;
}

void debug_reduce(const char* rule_name, std::shared_ptr<ASTNode> node = nullptr) {
    if (!debug_enabled) return;
    indent_level--;
    for (int i = 0; i < indent_level; i++) {
        fprintf(stderr, "  ");
    }
    fprintf(stderr, "REDUCED: %s", rule_name);
    if (node && ast_debug) {
        fprintf(stderr, " -> AST Node: %s", 
               (node->value.empty() ? "created" : node->value.c_str()));
    }
    fprintf(stderr, " (line %d)\n", yylineno);
}

void debug_ast(const char* description, std::shared_ptr<ASTNode> node) {
    if (!ast_debug || !node) return;
    fprintf(stderr, "AST: %s - Type: %d", description, static_cast<int>(node->type));
    if (!node->value.empty()) {
        fprintf(stderr, ", Value: '%s'", node->value.c_str());
    }
    fprintf(stderr, ", Children: %zu\n", node->children.size());
}

void print_context(const char* s) {
    fprintf(stderr, "\nPARSE ERROR at line %d: %s\n", yylineno, s);
    fprintf(stderr, "Near token: '%s'\n\n", yytext);
}

%}

%code requires {
    #include <memory>
    #include <functional>
    class ASTNode;
}

%union {
    int ival;
    double dval;
    bool bval;
    char* sval;
    std::shared_ptr<ASTNode>* node_ptr;
}

%token KEYWORD_VAR KEYWORD_TYPE KEYWORD_ROUTINE KEYWORD_PRINT KEYWORD_IF KEYWORD_ELSE
%token KEYWORD_WHILE KEYWORD_FOR KEYWORD_IN KEYWORD_REVERSE KEYWORD_RETURN KEYWORD_IS
%token KEYWORD_END KEYWORD_LOOP KEYWORD_THEN KEYWORD_RECORD KEYWORD_ARRAY KEYWORD_SIZE
%token ASSIGN COLON COMMA SEMICOLON LPAREN RPAREN LBRACKET RBRACKET DOTDOT EQ_GT DOT
%token AND_OP OR_OP XOR_OP NOT_OP LE_OP GE_OP LT_OP GT_OP EQ_OP NEQ_OP
%token MOD_OP PLUS_OP MINUS_OP MUL_OP DIV_OP
%token TYPE_INTEGER TYPE_REAL TYPE_BOOLEAN
%token <sval> STRING_LITERAL 
%token <ival> INT_LITERAL
%token <dval> REAL_LITERAL
%token <bval> BOOL_LITERAL
%token <sval> IDENTIFIER

%type <node_ptr> program top_level_declaration SimpleDeclaration VariableDeclaration
%type <node_ptr> TypeDeclaration type primitive_type user_type ArrayType RecordType
%type <node_ptr> RoutineDeclaration routine_forward_declaration parameters_opt parameters parameter
%type <node_ptr> routine_return_opt routine_body body body_item statement
%type <node_ptr> expression relation term simple primary
%type <node_ptr> routine_call arguments_opt arguments Assignment ModifiablePrimary
%type <node_ptr> WhileLoop ForLoop range IfStatement PrintStatement expression_list
%type <node_ptr> ReturnStatement is_expression_opt record_fields reverse_opt else_opt

%left OR_OP
%left XOR_OP
%left AND_OP
%left EQ_OP NEQ_OP
%left LT_OP LE_OP GT_OP GE_OP
%left PLUS_OP MINUS_OP
%left MUL_OP DIV_OP MOD_OP
%right NOT_OP UMINUS

%start program

%%

program:
    /* empty */ 
    { 
        debug_print("program", "empty");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::PROGRAM));
        astRoot = *$$;
        debug_reduce("program", *$$);
    }
  | program top_level_declaration 
    { 
        debug_print("program", "program top_level_declaration");
        $$ = $1;
        if ($2 && *$2) {
            (*$$)->addChild(*$2);
        }
        astRoot = *$$;
        debug_reduce("program", *$$);
    }
  ;

top_level_declaration:
    SimpleDeclaration
    {
        debug_print("top_level_declaration", "SimpleDeclaration");
        $$ = $1;
        debug_reduce("top_level_declaration", *$$);
    }
  | RoutineDeclaration
    {
        debug_print("top_level_declaration", "RoutineDeclaration");
        $$ = $1;
        debug_reduce("top_level_declaration", *$$);
    }
  | routine_forward_declaration
    {
        debug_print("top_level_declaration", "routine_forward_declaration");
        $$ = $1;
        debug_reduce("top_level_declaration", *$$);
    }
  ;

routine_forward_declaration:
    KEYWORD_ROUTINE IDENTIFIER LPAREN parameters_opt RPAREN routine_return_opt SEMICOLON
    {
        debug_print("routine_forward_declaration", "KEYWORD_ROUTINE IDENTIFIER ...");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::ROUTINE_FORWARD_DECL, $2));
        if ($4 && *$4) (*$$)->addChild(*$4);
        if ($6 && *$6) (*$$)->addChild(*$6);
        debug_ast("Forward routine declaration", *$$);
        debug_reduce("routine_forward_declaration", *$$);
    }
  ;

SimpleDeclaration:
    VariableDeclaration
    {
        debug_print("SimpleDeclaration", "VariableDeclaration");
        $$ = $1;
        debug_reduce("SimpleDeclaration", *$$);
    }
  | TypeDeclaration
    {
        debug_print("SimpleDeclaration", "TypeDeclaration");
        $$ = $1;
        debug_reduce("SimpleDeclaration", *$$);
    }
  ;

VariableDeclaration:
    KEYWORD_VAR IDENTIFIER COLON type is_expression_opt SEMICOLON
    {
        debug_print("VariableDeclaration", "var IDENTIFIER : type [is expression]");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::VAR_DECL, $2));
        if ($4 && *$4) (*$$)->addChild(*$4);
        if ($5 && *$5) (*$$)->addChild(*$5);
        debug_ast("Variable declaration with type", *$$);
        debug_reduce("VariableDeclaration", *$$);
    }
  | KEYWORD_VAR IDENTIFIER KEYWORD_IS expression SEMICOLON
    {
        debug_print("VariableDeclaration", "var IDENTIFIER is expression");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::VAR_DECL, $2));
        if ($4 && *$4) (*$$)->addChild(*$4);
        debug_ast("Variable declaration with inference", *$$);
        debug_reduce("VariableDeclaration", *$$);
    }
  ;

is_expression_opt:
    /* empty */
    {
        debug_print("is_expression_opt", "empty");
        $$ = nullptr;
        debug_reduce("is_expression_opt");
    }
  | KEYWORD_IS expression
    {
        debug_print("is_expression_opt", "is expression");
        $$ = $2;
        debug_reduce("is_expression_opt", $$ ? *$$ : nullptr);
    }
  ;

TypeDeclaration:
    KEYWORD_TYPE IDENTIFIER KEYWORD_IS type SEMICOLON
    {
        debug_print("TypeDeclaration", "type IDENTIFIER is type");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::TYPE_DECL, $2));
        if ($4 && *$4) (*$$)->addChild(*$4);
        debug_ast("Type declaration", *$$);
        debug_reduce("TypeDeclaration", *$$);
    }
  ;

type:
    primitive_type
    {
        debug_print("type", "primitive_type");
        $$ = $1;
        debug_reduce("type", *$$);
    }
  | user_type
    {
        debug_print("type", "user_type");
        $$ = $1;
        debug_reduce("type", *$$);
    }
  | IDENTIFIER
    {
        debug_print("type", "IDENTIFIER");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::USER_TYPE, $1));
        debug_ast("User type", *$$);
        debug_reduce("type", *$$);
    }
  ;

primitive_type:
    TYPE_INTEGER
    {
        debug_print("primitive_type", "integer");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::PRIMITIVE_TYPE, "integer"));
        debug_reduce("primitive_type", *$$);
    }
  | TYPE_REAL
    {
        debug_print("primitive_type", "real");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::PRIMITIVE_TYPE, "real"));
        debug_reduce("primitive_type", *$$);
    }
  | TYPE_BOOLEAN
    {
        debug_print("primitive_type", "boolean");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::PRIMITIVE_TYPE, "boolean"));
        debug_reduce("primitive_type", *$$);
    }
  ;

user_type:
    ArrayType
    {
        debug_print("user_type", "ArrayType");
        $$ = $1;
        debug_reduce("user_type", *$$);
    }
  | RecordType
    {
        debug_print("user_type", "RecordType");
        $$ = $1;
        debug_reduce("user_type", *$$);
    }
  ;

ArrayType:
    KEYWORD_ARRAY LBRACKET expression RBRACKET type
    {
        debug_print("ArrayType", "array[expression] type");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::ARRAY_TYPE));
        if ($3 && *$3) (*$$)->addChild(*$3);
        if ($5 && *$5) (*$$)->addChild(*$5);
        debug_ast("Array type with size", *$$);
        debug_reduce("ArrayType", *$$);
    }
  | KEYWORD_ARRAY LBRACKET RBRACKET type
    {
        debug_print("ArrayType", "array[] type");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::ARRAY_TYPE));
        if ($4 && *$4) (*$$)->addChild(*$4);
        debug_ast("Array type without size", *$$);
        debug_reduce("ArrayType", *$$);
    }
  ;

RecordType:
    KEYWORD_RECORD record_fields KEYWORD_END
    {
        debug_print("RecordType", "record fields end");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::RECORD_TYPE));
        if ($2 && *$2) (*$$)->addChild(*$2);
        debug_ast("Record type", *$$);
        debug_reduce("RecordType", *$$);
    }
  ;

record_fields:
    /* empty */
    {
        debug_print("record_fields", "empty");
        $$ = nullptr;
        debug_reduce("record_fields");
    }
  | record_fields VariableDeclaration
    {
        debug_print("record_fields", "record_fields VariableDeclaration");
        if (!$1) {
            $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::BODY));
        } else {
            $$ = $1;
        }
        if ($2 && *$2) (*$$)->addChild(*$2);
        debug_reduce("record_fields", *$$);
    }
  ;

RoutineDeclaration:
    KEYWORD_ROUTINE IDENTIFIER LPAREN parameters_opt RPAREN routine_return_opt routine_body
    {
        debug_print("RoutineDeclaration", "routine IDENTIFIER (params) [: type] body");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::ROUTINE_DECL, $2));
        if ($4 && *$4) (*$$)->addChild(*$4);
        if ($6 && *$6) (*$$)->addChild(*$6);
        if ($7 && *$7) (*$$)->addChild(*$7);
        debug_ast("Routine declaration", *$$);
        debug_reduce("RoutineDeclaration", *$$);
    }
  ;

parameters_opt:
    /* empty */
    {
        debug_print("parameters_opt", "empty");
        $$ = nullptr;
        debug_reduce("parameters_opt");
    }
  | parameters
    {
        debug_print("parameters_opt", "parameters");
        $$ = $1;
        debug_reduce("parameters_opt", *$$);
    }
  ;

parameters:
    parameter
    {
        debug_print("parameters", "parameter");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::PARAMETER_LIST));
        if ($1 && *$1) (*$$)->addChild(*$1);
        debug_reduce("parameters", *$$);
    }
  | parameters COMMA parameter
    {
        debug_print("parameters", "parameters, parameter");
        $$ = $1;
        if ($3 && *$3) (*$$)->addChild(*$3);
        debug_reduce("parameters", *$$);
    }
  ;

parameter:
    IDENTIFIER COLON type
    {
        debug_print("parameter", "IDENTIFIER : type");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::PARAMETER, $1));
        if ($3 && *$3) (*$$)->addChild(*$3);
        debug_ast("Parameter", *$$);
        debug_reduce("parameter", *$$);
    }
  ;

routine_return_opt:
    /* empty */
    {
        debug_print("routine_return_opt", "empty");
        $$ = nullptr;
        debug_reduce("routine_return_opt");
    }
  | COLON type
    {
        debug_print("routine_return_opt", ": type");
        $$ = $2;
        debug_reduce("routine_return_opt", *$$);
    }
  ;

routine_body:
    KEYWORD_IS body KEYWORD_END
    {
        debug_print("routine_body", "is body end");
        $$ = $2;
        debug_reduce("routine_body", *$$);
    }
  | EQ_GT expression
    {
        debug_print("routine_body", "=> expression");
        $$ = $2;
        debug_reduce("routine_body", *$$);
    }
  ;

body:
    /* empty */
    {
        debug_print("body", "empty");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::BODY));
        debug_reduce("body", *$$);
    }
  | body body_item
    {
        debug_print("body", "body body_item");
        $$ = $1;
        if ($2 && *$2) (*$$)->addChild(*$2);
        debug_reduce("body", *$$);
    }
  ;

body_item:
    SimpleDeclaration
    {
        debug_print("body_item", "SimpleDeclaration");
        $$ = $1;
        debug_reduce("body_item", *$$);
    }
  | statement
    {
        debug_print("body_item", "statement");
        $$ = $1;
        debug_reduce("body_item", *$$);
    }
  ;

statement:
    routine_call SEMICOLON
    {
        debug_print("statement", "routine_call;");
        $$ = $1;
        debug_reduce("statement", *$$);
    }
  | Assignment SEMICOLON
    {
        debug_print("statement", "Assignment;");
        $$ = $1;
        debug_reduce("statement", *$$);
    }
  | WhileLoop
    {
        debug_print("statement", "WhileLoop");
        $$ = $1;
        debug_reduce("statement", *$$);
    }
  | ForLoop
    {
        debug_print("statement", "ForLoop");
        $$ = $1;
        debug_reduce("statement", *$$);
    }
  | IfStatement
    {
        debug_print("statement", "IfStatement");
        $$ = $1;
        debug_reduce("statement", *$$);
    }
  | PrintStatement SEMICOLON
    {
        debug_print("statement", "PrintStatement;");
        $$ = $1;
        debug_reduce("statement", *$$);
    }
  | ReturnStatement SEMICOLON
    {
        debug_print("statement", "ReturnStatement;");
        $$ = $1;
        debug_reduce("statement", *$$);
    }
  ;

expression:
    relation
    {
        debug_print("expression", "relation");
        $$ = $1;
        debug_reduce("expression", *$$);
    }
  | expression OR_OP relation
    {
        debug_print("expression", "expression OR relation");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("or", *$1, *$3));
        debug_ast("OR expression", *$$);
        debug_reduce("expression", *$$);
    }
  | expression XOR_OP relation
    {
        debug_print("expression", "expression XOR relation");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("xor", *$1, *$3));
        debug_ast("XOR expression", *$$);
        debug_reduce("expression", *$$);
    }
  | expression AND_OP relation
    {
        debug_print("expression", "expression AND relation");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("and", *$1, *$3));
        debug_ast("AND expression", *$$);
        debug_reduce("expression", *$$);
    }
  ;

relation:
    term
    {
        debug_print("relation", "term");
        $$ = $1;
        debug_reduce("relation", *$$);
    }
  | relation LT_OP term
    {
        debug_print("relation", "relation < term");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("<", *$1, *$3));
        debug_ast("LT relation", *$$);
        debug_reduce("relation", *$$);
    }
  | relation LE_OP term
    {
        debug_print("relation", "relation <= term");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("<=", *$1, *$3));
        debug_ast("LE relation", *$$);
        debug_reduce("relation", *$$);
    }
  | relation GT_OP term
    {
        debug_print("relation", "relation > term");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp(">", *$1, *$3));
        debug_ast("GT relation", *$$);
        debug_reduce("relation", *$$);
    }
  | relation GE_OP term
    {
        debug_print("relation", "relation >= term");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp(">=", *$1, *$3));
        debug_ast("GE relation", *$$);
        debug_reduce("relation", *$$);
    }
  | relation EQ_OP term
    {
        debug_print("relation", "relation = term");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("=", *$1, *$3));
        debug_ast("EQ relation", *$$);
        debug_reduce("relation", *$$);
    }
  | relation NEQ_OP term
    {
        debug_print("relation", "relation /= term");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("/=", *$1, *$3));
        debug_ast("NEQ relation", *$$);
        debug_reduce("relation", *$$);
    }
  ;

term:
    simple
    {
        debug_print("term", "simple");
        $$ = $1;
        debug_reduce("term", *$$);
    }
  | term PLUS_OP simple
    {
        debug_print("term", "term + simple");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("+", *$1, *$3));
        debug_ast("PLUS term", *$$);
        debug_reduce("term", *$$);
    }
  | term MINUS_OP simple
    {
        debug_print("term", "term - simple");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("-", *$1, *$3));
        debug_ast("MINUS term", *$$);
        debug_reduce("term", *$$);
    }
  ;

simple:
    primary
    {
        debug_print("simple", "primary");
        $$ = $1;
        debug_reduce("simple", *$$);
    }
  | simple MUL_OP primary
    {
        debug_print("simple", "simple * primary");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("*", *$1, *$3));
        debug_ast("MUL simple", *$$);
        debug_reduce("simple", *$$);
    }
  | simple DIV_OP primary
    {
        debug_print("simple", "simple / primary");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("/", *$1, *$3));
        debug_ast("DIV simple", *$$);
        debug_reduce("simple", *$$);
    }
  | simple MOD_OP primary
    {
        debug_print("simple", "simple % primary");
        $$ = new std::shared_ptr<ASTNode>(createBinaryOp("%", *$1, *$3));
        debug_ast("MOD simple", *$$);
        debug_reduce("simple", *$$);
    }
  | LPAREN expression RPAREN
    {
        debug_print("simple", "(expression)");
        $$ = $2;
        debug_reduce("simple", *$$);
    }
  ;

primary:
    INT_LITERAL
    {
        debug_print("primary", "INT_LITERAL");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::LITERAL_INT, std::to_string($1)));
        debug_ast("Integer literal", *$$);
        debug_reduce("primary", *$$);
    }
  | REAL_LITERAL
    {
        debug_print("primary", "REAL_LITERAL");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::LITERAL_REAL, std::to_string($1)));
        debug_ast("Real literal", *$$);
        debug_reduce("primary", *$$);
    }
  | BOOL_LITERAL
    {
        debug_print("primary", "BOOL_LITERAL");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::LITERAL_BOOL, $1 ? "true" : "false"));
        debug_ast("Boolean literal", *$$);
        debug_reduce("primary", *$$);
    }
  | STRING_LITERAL
    {
        debug_print("primary", "STRING_LITERAL");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::LITERAL_STRING, $1));
        debug_ast("String literal", *$$);
        debug_reduce("primary", *$$);
    }
  | ModifiablePrimary
    {
        debug_print("primary", "ModifiablePrimary");
        $$ = $1;
        debug_reduce("primary", *$$);
    }
  | routine_call
    {
        debug_print("primary", "routine_call");
        $$ = $1;
        debug_reduce("primary", *$$);
    }
  | KEYWORD_SIZE LPAREN primary RPAREN
    {
        debug_print("primary", "size(primary)");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::SIZE_EXPRESSION));
        if ($3 && *$3) (*$$)->addChild(*$3);
        debug_ast("Size expression", *$$);
        debug_reduce("primary", *$$);
    }
  | PLUS_OP primary %prec UMINUS
    {
        debug_print("primary", "+primary");
        $$ = new std::shared_ptr<ASTNode>(createUnaryOp("+", *$2));
        debug_ast("Unary plus", *$$);
        debug_reduce("primary", *$$);
    }
  | MINUS_OP primary %prec UMINUS
    {
        debug_print("primary", "-primary");
        $$ = new std::shared_ptr<ASTNode>(createUnaryOp("-", *$2));
        debug_ast("Unary minus", *$$);
        debug_reduce("primary", *$$);
    }
  | NOT_OP primary
    {
        debug_print("primary", "not primary");
        $$ = new std::shared_ptr<ASTNode>(createUnaryOp("not", *$2));
        debug_ast("NOT expression", *$$);
        debug_reduce("primary", *$$);
    }
  ;

routine_call:
    IDENTIFIER LPAREN arguments_opt RPAREN
    {
        debug_print("routine_call", "IDENTIFIER(arguments)");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::ROUTINE_CALL, $1));
        if ($3 && *$3) (*$$)->addChild(*$3);
        debug_ast("Routine call", *$$);
        debug_reduce("routine_call", *$$);
    }
  ;

arguments_opt:
    /* empty */
    {
        debug_print("arguments_opt", "empty");
        $$ = nullptr;
        debug_reduce("arguments_opt");
    }
  | arguments
    {
        debug_print("arguments_opt", "arguments");
        $$ = $1;
        debug_reduce("arguments_opt", *$$);
    }
  ;

arguments:
    expression
    {
        debug_print("arguments", "expression");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::ARGUMENT_LIST));
        if ($1 && *$1) (*$$)->addChild(*$1);
        debug_reduce("arguments", *$$);
    }
  | arguments COMMA expression
    {
        debug_print("arguments", "arguments, expression");
        $$ = $1;
        if ($3 && *$3) (*$$)->addChild(*$3);
        debug_reduce("arguments", *$$);
    }
  ;

Assignment:
    ModifiablePrimary ASSIGN expression
    {
        debug_print("Assignment", "ModifiablePrimary := expression");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::ASSIGNMENT));
        if ($1 && *$1) (*$$)->addChild(*$1);
        if ($3 && *$3) (*$$)->addChild(*$3);
        debug_ast("Assignment", *$$);
        debug_reduce("Assignment", *$$);
    }
  ;

ModifiablePrimary:
    IDENTIFIER
    {
        debug_print("ModifiablePrimary", "IDENTIFIER");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::IDENTIFIER, $1));
        debug_ast("Identifier", *$$);
        debug_reduce("ModifiablePrimary", *$$);
    }
  | ModifiablePrimary DOT IDENTIFIER
    {
        debug_print("ModifiablePrimary", "ModifiablePrimary.IDENTIFIER");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::MEMBER_ACCESS, $3));
        if ($1 && *$1) (*$$)->addChild(*$1);
        debug_ast("Member access", *$$);
        debug_reduce("ModifiablePrimary", *$$);
    }
  | ModifiablePrimary LBRACKET expression RBRACKET
    {
        debug_print("ModifiablePrimary", "ModifiablePrimary[expression]");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::ARRAY_ACCESS));
        if ($1 && *$1) (*$$)->addChild(*$1);
        if ($3 && *$3) (*$$)->addChild(*$3);
        debug_ast("Array access", *$$);
        debug_reduce("ModifiablePrimary", *$$);
    }
  ;

WhileLoop:
    KEYWORD_WHILE expression KEYWORD_LOOP body KEYWORD_END
    {
        debug_print("WhileLoop", "while expression loop body end");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::WHILE_LOOP));
        if ($2 && *$2) (*$$)->addChild(*$2);
        if ($4 && *$4) (*$$)->addChild(*$4);
        debug_ast("While loop", *$$);
        debug_reduce("WhileLoop", *$$);
    }
  ;

ForLoop:
    KEYWORD_FOR IDENTIFIER KEYWORD_IN range reverse_opt KEYWORD_LOOP body KEYWORD_END
    {
        debug_print("ForLoop", "for IDENTIFIER in range [reverse] loop body end");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::FOR_LOOP, $2));
        if ($4 && *$4) (*$$)->addChild(*$4);
        if ($5 && *$5) (*$$)->addChild(*$5);
        if ($7 && *$7) (*$$)->addChild(*$7);
        debug_ast("For loop with range", *$$);
        debug_reduce("ForLoop", *$$);
    }
  | KEYWORD_FOR IDENTIFIER KEYWORD_IN expression reverse_opt KEYWORD_LOOP body KEYWORD_END
    {
        debug_print("ForLoop", "for IDENTIFIER in expression [reverse] loop body end");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::FOR_LOOP, $2));
        if ($4 && *$4) (*$$)->addChild(*$4);
        if ($5 && *$5) (*$$)->addChild(*$5);
        if ($7 && *$7) (*$$)->addChild(*$7);
        debug_ast("For loop with expression", *$$);
        debug_reduce("ForLoop", *$$);
    }
  ;

reverse_opt:
    /* empty */
    {
        debug_print("reverse_opt", "empty");
        $$ = nullptr;
        debug_reduce("reverse_opt");
    }
  | KEYWORD_REVERSE
    {
        debug_print("reverse_opt", "reverse");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::IDENTIFIER, "reverse"));
        debug_reduce("reverse_opt", *$$);
    }
  ;

range:
    expression DOTDOT expression
    {
        debug_print("range", "expression .. expression");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::RANGE));
        if ($1 && *$1) (*$$)->addChild(*$1);
        if ($3 && *$3) (*$$)->addChild(*$3);
        debug_ast("Range", *$$);
        debug_reduce("range", *$$);
    }
  ;

IfStatement:
    KEYWORD_IF expression KEYWORD_THEN body else_opt KEYWORD_END
    {
        debug_print("IfStatement", "if expression then body [else body] end");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::IF_STMT));
        if ($2 && *$2) (*$$)->addChild(*$2);
        if ($4 && *$4) (*$$)->addChild(*$4);
        if ($5 && *$5) (*$$)->addChild(*$5);
        debug_ast("If statement", *$$);
        debug_reduce("IfStatement", *$$);
    }
  ;

else_opt:
    /* empty */
    {
        debug_print("else_opt", "empty");
        $$ = nullptr;
        debug_reduce("else_opt");
    }
  | KEYWORD_ELSE body
    {
        debug_print("else_opt", "else body");
        $$ = $2;
        debug_reduce("else_opt", *$$);
    }
  ;

PrintStatement:
    KEYWORD_PRINT expression_list
    {
        debug_print("PrintStatement", "print expression_list");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::PRINT_STMT));
        if ($2 && *$2) (*$$)->addChild(*$2);
        debug_ast("Print statement", *$$);
        debug_reduce("PrintStatement", *$$);
    }
  ;

expression_list:
    expression
    {
        debug_print("expression_list", "expression");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::EXPRESSION_LIST));
        if ($1 && *$1) (*$$)->addChild(*$1);
        debug_reduce("expression_list", *$$);
    }
  | expression_list COMMA expression
    {
        debug_print("expression_list", "expression_list, expression");
        $$ = $1;
        if ($3 && *$3) (*$$)->addChild(*$3);
        debug_reduce("expression_list", *$$);
    }
  ;

ReturnStatement:
    KEYWORD_RETURN
    {
        debug_print("ReturnStatement", "return");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::RETURN_STMT));
        debug_ast("Return statement", *$$);
        debug_reduce("ReturnStatement", *$$);
    }
  | KEYWORD_RETURN expression
    {
        debug_print("ReturnStatement", "return expression");
        $$ = new std::shared_ptr<ASTNode>(createNode(ASTNodeType::RETURN_STMT));
        if ($2 && *$2) (*$$)->addChild(*$2);
        debug_ast("Return statement with value", *$$);
        debug_reduce("ReturnStatement", *$$);
    }
  ;

%%

#include "semantics.h"

int main(int argc, char* argv[]) {
    printf("=== IMPERATIVE LANGUAGE COMPILER WITH SEMANTIC ANALYSIS ===\n");
    
    if (argc > 1) {
        // File input mode
        std::string filename = argv[1];
        printf("Processing file: %s\n", filename.c_str());
        
        FILE* file = fopen(filename.c_str(), "r");
        if (!file) {
            fprintf(stderr, "Error: Cannot open file %s\n", filename.c_str());
            return 1;
        }
        
        yyin = file;
        int result = yyparse();
        fclose(file);
        
        if (result == 0) {
            printf("\n=== PARSER SUCCESSFUL! ===\n");
            
            if (astRoot) {
                printf("\n=== RUNNING SEMANTIC ANALYSIS ===\n");
                SemanticAnalyzer analyzer;
                bool semanticSuccess = analyzer.analyze(astRoot);
                
                printf("\n=== FINAL RESULT ===\n");
                if (semanticSuccess) {
                    printf("\n=== 🚀 STARTING WASM COMPILATION ===\n");
    
                    WasmCompiler compiler;
                    std::string wasmFilename = "output.wasm";
                    
                    if (compiler.compile(astRoot, wasmFilename)) {
                        printf("✅ WASM COMPILATION SUCCESSFUL!\n");
                        printf("📁 Output: %s\n", wasmFilename.c_str());
                        printf("💡 You can run it with: wasmtime %s\n", wasmFilename.c_str());
                    } else {
                        printf("❌ WASM COMPILATION FAILED\n");
                        return 1;
                    }
                    
                    // Print the AST
                    printf("\n=== ABSTRACT SYNTAX TREE ===\n");
                    astRoot->print();
                    
                    // Generate DOT visualization to file
                    std::string dotFilename = filename + ".dot";
                    std::string pngFilename = filename + ".png";
                    
                    std::ofstream dotFile(dotFilename);
                    if (dotFile) {
                        dotFile << astRoot->toDot();
                        dotFile.close();
                        printf("\n=== AST VISUALIZATION ===\n");
                        printf("DOT file: %s\n", dotFilename.c_str());
                        
                        // Try to generate PNG if Graphviz is installed
                        std::string command = "dot -Tpng " + dotFilename + " -o " + pngFilename + " 2>/dev/null";
                        if (system(command.c_str()) == 0) {
                            printf("PNG visualization: %s\n", pngFilename.c_str());
                        } else {
                            printf("Install Graphviz (dot) for PNG visualization\n");
                        }
                    }
                } else {
                    printf("✗ COMPILATION FAILED - SEMANTIC ERRORS DETECTED\n");
                    return 1;
                }
            } else {
                printf("No AST root created.\n");
                return 1;
            }
        } else {
            printf("\n=== PARSER FAILED ===\n");
            return 1;
        }
    } else {
        // Interactive mode
        printf("Usage: %s <filename>\n", argv[0]);
        printf("Example: %s my_program.txt\n", argv[0]);
        printf("\nOr provide input file:\n");
        printf("No input file provided. Running in interactive mode...\n");
        printf("Enter your program (Ctrl+D to finish):\n");
        
        int result = yyparse();
        
        if (result == 0 && astRoot) {
            printf("\n=== PARSER SUCCESSFUL! ===\n");
            
            printf("\n=== RUNNING SEMANTIC ANALYSIS ===\n");
            SemanticAnalyzer analyzer;
            bool semanticSuccess = analyzer.analyze(astRoot);
            
            printf("\n=== FINAL RESULT ===\n");
            if (semanticSuccess) {
                printf("✓ COMPILATION SUCCESSFUL - NO ERRORS FOUND\n");
                
                printf("\n=== ABSTRACT SYNTAX TREE ===\n");
                astRoot->print();
            } else {
                printf("✗ COMPILATION FAILED - SEMANTIC ERRORS DETECTED\n");
                return 1;
            }
        } else {
            printf("\n=== PARSER FAILED ===\n");
            return 1;
        }
    }
    
    return 0;
}


void yyerror(const char* s) {
    print_context(s);
}