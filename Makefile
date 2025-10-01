CXX = g++
LEX = flex
YACC = bison
CXXFLAGS = -std=c++11 -Wall -Wextra -g
INCLUDES = -I.

all: parser

parser: parser.tab.cpp parser.tab.hpp lex.yy.cpp ast.cpp ast.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o parser parser.tab.cpp lex.yy.cpp ast.cpp -lfl

parser.tab.cpp parser.tab.hpp: parser-with-ast.y
	$(YACC) -d -o parser.tab.cpp parser-with-ast.y

lex.yy.cpp: lexer-with-ast.l parser.tab.hpp
	$(LEX) -o lex.yy.cpp lexer-with-ast.l

clean:
	rm -f parser parser.tab.cpp parser.tab.hpp lex.yy.cpp ast_output.dot ast_tree.png
