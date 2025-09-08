# Makefile
CC = g++
LEX = flex
YACC = bison
CFLAGS = -std=c++11

parser: parser.tab.cpp lex.yy.cpp
	$(CC) $(CFLAGS) -o parser parser.tab.cpp lex.yy.cpp -lfl

parser.tab.cpp: parser.y
	$(YACC) -d -o parser.tab.cpp parser.y

lex.yy.cpp: lexer.l
	$(LEX) -o lex.yy.cpp lexer.l

clean:
	rm -f parser parser.tab.cpp parser.tab.hpp lex.yy.cpp

test:
	./parser < test.txt