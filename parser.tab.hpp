/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_PARSER_TAB_HPP_INCLUDED
# define YY_YY_PARSER_TAB_HPP_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    KEYWORD_VAR = 258,             /* KEYWORD_VAR  */
    KEYWORD_TYPE = 259,            /* KEYWORD_TYPE  */
    KEYWORD_ROUTINE = 260,         /* KEYWORD_ROUTINE  */
    KEYWORD_PRINT = 261,           /* KEYWORD_PRINT  */
    KEYWORD_IF = 262,              /* KEYWORD_IF  */
    KEYWORD_ELSE = 263,            /* KEYWORD_ELSE  */
    KEYWORD_WHILE = 264,           /* KEYWORD_WHILE  */
    KEYWORD_FOR = 265,             /* KEYWORD_FOR  */
    KEYWORD_IN = 266,              /* KEYWORD_IN  */
    KEYWORD_REVERSE = 267,         /* KEYWORD_REVERSE  */
    KEYWORD_RETURN = 268,          /* KEYWORD_RETURN  */
    KEYWORD_IS = 269,              /* KEYWORD_IS  */
    KEYWORD_END = 270,             /* KEYWORD_END  */
    KEYWORD_LOOP = 271,            /* KEYWORD_LOOP  */
    KEYWORD_THEN = 272,            /* KEYWORD_THEN  */
    KEYWORD_RECORD = 273,          /* KEYWORD_RECORD  */
    KEYWORD_ARRAY = 274,           /* KEYWORD_ARRAY  */
    KEYWORD_SIZE = 275,            /* KEYWORD_SIZE  */
    ASSIGN = 276,                  /* ASSIGN  */
    COLON = 277,                   /* COLON  */
    COMMA = 278,                   /* COMMA  */
    SEMICOLON = 279,               /* SEMICOLON  */
    LPAREN = 280,                  /* LPAREN  */
    RPAREN = 281,                  /* RPAREN  */
    LBRACKET = 282,                /* LBRACKET  */
    RBRACKET = 283,                /* RBRACKET  */
    DOTDOT = 284,                  /* DOTDOT  */
    EQ_GT = 285,                   /* EQ_GT  */
    DOT = 286,                     /* DOT  */
    AND_OP = 287,                  /* AND_OP  */
    OR_OP = 288,                   /* OR_OP  */
    XOR_OP = 289,                  /* XOR_OP  */
    NOT_OP = 290,                  /* NOT_OP  */
    LE_OP = 291,                   /* LE_OP  */
    GE_OP = 292,                   /* GE_OP  */
    LT_OP = 293,                   /* LT_OP  */
    GT_OP = 294,                   /* GT_OP  */
    EQ_OP = 295,                   /* EQ_OP  */
    NEQ_OP = 296,                  /* NEQ_OP  */
    MOD_OP = 297,                  /* MOD_OP  */
    PLUS_OP = 298,                 /* PLUS_OP  */
    MINUS_OP = 299,                /* MINUS_OP  */
    MUL_OP = 300,                  /* MUL_OP  */
    DIV_OP = 301,                  /* DIV_OP  */
    TYPE_INTEGER = 302,            /* TYPE_INTEGER  */
    TYPE_REAL = 303,               /* TYPE_REAL  */
    TYPE_BOOLEAN = 304,            /* TYPE_BOOLEAN  */
    INT_LITERAL = 305,             /* INT_LITERAL  */
    REAL_LITERAL = 306,            /* REAL_LITERAL  */
    BOOL_LITERAL = 307,            /* BOOL_LITERAL  */
    IDENTIFIER = 308,              /* IDENTIFIER  */
    UMINUS = 309                   /* UMINUS  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 27 "parser.y"

    int ival;
    double dval;
    bool bval;
    char* sval;

#line 125 "parser.tab.hpp"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_PARSER_TAB_HPP_INCLUDED  */
