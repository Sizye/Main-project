# Test targets
test1: $(TARGET)
	./$(TARGET) test1.txt

test2: $(TARGET) 
	./$(TARGET) test2.txt

test3: $(TARGET)
	./$(TARGET) test3.txt

# Create test files
create-tests:
	echo "var arr: array[10] integer; routine main() is arr[5] := 42; end" > test1.txt
	echo "var arr: array[5] integer; routine main() is arr[10] := 20; end" > test2.txt
	echo "var data: array[8] integer; var index: integer; routine process() is data[15] := 77; end" > test3.txt

# Run all tests
test-all: create-tests test1 test2 test3

# Visualize AST
visualize-test: $(TARGET)
	./$(TARGET) test1.txt
	@if [ -f test1.txt.dot ]; then \
		dot -Tpng test1.txt.dot -o test1_ast.png && echo "AST visualization: test1_ast.png"; \
	fi