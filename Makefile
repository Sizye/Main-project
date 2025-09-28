# Makefile
CC = g++
LEX = flex
YACC = bison
CFLAGS = -std=c++11

parser: parser.tab.cpp lex.yy.cpp
	$(CC) $(CFLAGS) -o parser parser.tab.cpp lex.yy.cpp -lfl

parser.tab.cpp: parser.y
	$(YACC) -d -o parser.tab.cpp parser.y

lexer: lexer.l
	$(LEX) -o lex.yy.cpp lexer.l

all: lexer parser

clean:
	rm -f parser parser.tab.* parser.tab.h lex.yy.c*

test:
	./parser < test.txt

parser_clean:
	rm -f parser.tab.*
	rm parser

lexer_clean:
	rm -f lexer.yy.*

test_succ:
	@for file in $$(find tests/success -name "test*" -type f | sort); do \
		echo "Testing $$file..."; \
		./parser < "$$file" || { echo "FAILED: $$file"; exit 1; }; \
		echo "PASS: $$file"; \
	done
	@echo "Parsing completed."

test_fail:
	@for file in $$(find tests/fail -type f | sort); do \
		echo "Testing $$file..."; \
		./parser < "$$file" || { echo "FAILED: $$file"; exit 1; }; \
		echo "PASS: $$file"; \
	done
	@echo "Parsing completed"