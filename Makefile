CXX = g++
LEX = flex
YACC = bison
CXXFLAGS = -std=c++11 -Wall -Wextra -g
INCLUDES = -I.
LDFLAGS = -lfl

# Source files
YACC_SRC = parser-with-ast.y
LEX_SRC = lexer-with-ast.l
AST_SRC = ast.cpp ast.h
SEMANTICS_SRC = semantics.h

# Generated files
YACC_OUT = parser.tab.cpp
YACC_HEADER = parser.tab.hpp
LEX_OUT = lex.yy.cpp
WASM_SRC = wasm_compiler.cpp wasm_compiler.h

# Targets
TARGET = parser

.PHONY: all clean test manual demo

all: $(TARGET)

# Build parser from YACC file (now contains main)
$(TARGET): $(YACC_OUT) $(LEX_OUT) $(AST_SRC) $(SEMANTICS_SRC) $(WASM_SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(YACC_OUT) $(LEX_OUT) ast.cpp wasm_compiler.cpp $(LDFLAGS)

$(YACC_OUT) $(YACC_HEADER): $(YACC_SRC)
	$(YACC) -d -o $(YACC_OUT) $(YACC_SRC)

$(LEX_OUT): $(LEX_SRC) $(YACC_HEADER)
	$(LEX) -o $(LEX_OUT) $(LEX_SRC)

# Test with file input
wasm-test: $(TARGET)
	@echo "=== TESTING WASM COMPILATION ==="
	@echo 'var x: integer; routine main() is return 42; end;' > test_wasm.txt
	@./$(TARGET) test_wasm.txt
	@if [ -f output.wasm ]; then \
		echo "✅ WASM file generated!"; \
		echo "📊 File size:" `wc -c output.wasm`; \
		echo "🔍 Hex dump:"; \
		hexdump -C output.wasm | head -20; \
	else \
		echo "❌ No WASM file generated"; \
	fi

test: $(TARGET)
	@if [ -f test_program.txt ]; then \
		echo "=== TESTING WITH FILE INPUT ==="; \
		./$(TARGET) test_program.txt; \
	else \
		echo "Test file test_program.txt not found. Creating example..."; \
		echo 'var arr: array[10] integer; arr[5] := 42;' > test_program.txt; \
		echo "=== TESTING WITH FILE INPUT ==="; \
		./$(TARGET) test_program.txt; \
	fi

# Interactive mode
manual: $(TARGET)
	@echo "=== INTERACTIVE MODE ==="
	@echo "Enter your program (Ctrl+D to finish):"
	@./$(TARGET)

# Demo with predefined input
demo: $(TARGET)
	@echo "=== DEMO MODE ==="
	@echo "var arr: array[10] integer; arr[15] := 100;" | ./$(TARGET) || true

# Generate AST visualization
visualize: $(TARGET)
	@echo "=== GENERATING AST VISUALIZATION ==="
	@if [ -f test_program.txt ]; then \
		./$(TARGET) test_program.txt 2>/dev/null | grep -A 100 "AST VISUALIZATION" > ast_output.dot; \
		if [ -f ast_output.dot ]; then \
			dot -Tpng ast_output.dot -o ast_tree.png 2>/dev/null && echo "AST visualization saved as ast_tree.png"; \
		else \
			echo "No DOT output generated"; \
		fi \
	else \
		echo "Create test_program.txt first"; \
	fi

clean:
	rm -f $(TARGET) $(YACC_OUT) $(YACC_HEADER) $(LEX_OUT)
	rm -f *.dot ast_output.dot ast_tree.png test_program.txt test_wasm.txt output.wasm
	find . -name "*.dot" -type f -delete
	rm -f *.o
	echo "🧹 Cleaned all generated files!"


# Quick rebuild
rebuild: clean all