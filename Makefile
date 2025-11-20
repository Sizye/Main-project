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
	@printf 'routine main(): integer is\n  var x: integer is 1;\n  return x;\nend\n' > test_wasm.txt

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


# Test: IF statement and comparisons
wasm-if-test: $(TARGET)
	@echo "=== TESTING WASM IF/ELSE ==="
	@printf 'routine main(): integer is\n'                >  test_if.txt
	@printf '  if 10 < 5 then\n'                          >> test_if.txt
	@printf '    return 10;\n'                           >> test_if.txt
	@printf '  else\n'                                   >> test_if.txt
	@printf '    return 20;\n'                           >> test_if.txt
	@printf '  end\n'                                    >> test_if.txt
	@printf 'end\n'                                      >> test_if.txt
	@./$(TARGET) test_if.txt
	@echo "--- Running generated WASM (expect 10) ---"
	@wasmtime run --invoke main output.wasm  

# Test: WHILE loop and arithmetic
wasm-while-test: $(TARGET)
	@echo "=== TESTING WASM WHILE LOOP ==="
	@printf 'routine main(): integer is\n'               >  test_while.txt
	@printf '  var i: integer is 0;\n'                   >> test_while.txt
	@printf '  var s: integer is 0;\n'                   >> test_while.txt
	@printf '  while i < 1000 loop\n'                      >> test_while.txt
	@printf '    s := s + 1;\n'                          >> test_while.txt
	@printf '    i := i + 2;\n'                          >> test_while.txt
	@printf '  end\n'                                   >> test_while.txt
	@printf '  return s;\n'                             >> test_while.txt
	@printf 'end\n'                                     >> test_while.txt
	@./$(TARGET) test_while.txt
	@echo "--- Running generated WASM (expect 10 = 0+1+2+3+4) ---"
	@wasmtime run --invoke main output.wasm


# Function call: add(2,3) -> 5
wasm-call-test: $(TARGET)
	@echo "=== TESTING CALL ==="
	@printf 'routine add(a: integer, b: integer): integer is\n' >  test_call1.txt
	@printf '  return a + b;\n'                               >> test_call1.txt
	@printf 'end\n'                                           >> test_call1.txt
	@printf 'routine main(): integer is\n'                    >> test_call1.txt
	@printf '  return add(2, 3);\n'                                   >> test_call1.txt
	@printf 'end\n'                                           >> test_call1.txt
	@./$(TARGET) test_call1.txt
	@echo "--- Running generated WASM (expect 5) ---"
	@wasmtime --invoke main output.wasm || true

# Nested calls: add(add(1,2),3) -> 6
wasm-call-nested: $(TARGET)
	@echo "=== TESTING NESTED CALL ==="
	@printf 'routine add(a: integer, b: integer): integer is\n' >  test_call2.txt
	@printf '  return a + b;\n'                                 >> test_call2.txt
	@printf 'end\n'                                             >> test_call2.txt
	@printf 'routine devide(a: integer, b: integer): integer is\n' >> test_call2.txt
	@printf '  return a / b;\n'                                 >> test_call2.txt
	@printf 'end\n'                                             >> test_call2.txt
	@printf 'routine main(): integer is\n'                      >> test_call2.txt
	@printf '  var x: integer is add(add(1, 2), 3);\n'          >> test_call2.txt
	@printf '  return add(devide(1000, 200), 1);\n'                     				>> test_call2.txt
	@printf 'end\n'                                             >> test_call2.txt
	@./$(TARGET) test_call2.txt
	@echo "--- Running generated WASM (expect 6) ---"
	@wasmtime --invoke main output.wasm || true


wasm-for-test: $(TARGET)
	@echo "=== TESTING FOR (0..4) ==="
	@printf 'routine main(): integer is\n'                 >  test_for.txt
	@printf '  var i: integer is 0;\n'                     >> test_for.txt
	@printf '  var s: integer is 0;\n'                     >> test_for.txt
	@printf '  for i in 0 .. 10 loop\n'                     >> test_for.txt
	@printf '    s := s + i;\n'                            >> test_for.txt
	@printf '  end\n'                                      >> test_for.txt
	@printf '  return s;\n'                                >> test_for.txt
	@printf 'end\n'                                        >> test_for.txt
	@./$(TARGET) test_for.txt
	@echo "--- Running generated WASM (expect 10) ---"
	@wasmtime --invoke main output.wasm || true


wasm-for-rev-test: $(TARGET)
	@echo "=== TESTING FOR REVERSE (5..1) ==="
	@printf 'routine main(): integer is\n'                 >  test_for_rev.txt
	@printf '  var i: integer is 0;\n'                     >> test_for_rev.txt
	@printf '  var s: integer is 0;\n'                     >> test_for_rev.txt
	@printf '  for i in 5 .. 1 reverse loop\n'             >> test_for_rev.txt
	@printf '    s := s + i;\n'                            >> test_for_rev.txt
	@printf '  end\n'                                      >> test_for_rev.txt
	@printf '  return s;\n'                                >> test_for_rev.txt
	@printf 'end\n'                                        >> test_for_rev.txt
	@./$(TARGET) test_for_rev.txt
	@echo "--- Running generated WASM (expect 15 = 5+4+3+2+1) ---"
	@wasmtime --invoke main output.wasm || true
