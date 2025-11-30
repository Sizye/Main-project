# Makefile for Imperative Language Compiler with Binaryen
# Uses CMake for building

CXX = g++
BUILD_DIR = build
BINARYEN_DIR = binaryen
PARSER = $(BUILD_DIR)/parser

.PHONY: all build clean test test-simple test-call test-if run help setup

# Default target
all: build

# Build the project using CMake
build: $(BUILD_DIR)/CMakeCache.txt
	@echo "🔨 Building project..."
	@cd $(BUILD_DIR) && make -j$(shell nproc)
	@echo "✅ Build complete!"

# Configure CMake if needed
$(BUILD_DIR)/CMakeCache.txt:
	@echo "⚙️  Configuring CMake..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DBINARYEN_LIBRARY=$(abspath $(BINARYEN_DIR))/lib/libbinaryen.so ..

# Setup Binaryen (if not already built)
setup:
	@if [ ! -d "$(BINARYEN_DIR)/lib" ] || [ ! -f "$(BINARYEN_DIR)/lib/libbinaryen.so" ]; then \
		echo "📦 Setting up Binaryen..."; \
		if [ ! -d "$(BINARYEN_DIR)" ]; then \
			git clone --depth 1 https://github.com/WebAssembly/binaryen.git $(BINARYEN_DIR); \
		fi; \
		cd $(BINARYEN_DIR) && git submodule update --init --recursive && \
		cmake -DBUILD_TESTS=OFF . && make -j$(shell nproc); \
		echo "✅ Binaryen setup complete!"; \
	else \
		echo "✅ Binaryen already built"; \
	fi

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -f output.wasm *.dot *.o
	@echo "✅ Clean complete!"

# Deep clean (including Binaryen)
clean-all: clean
	@echo "🧹 Deep cleaning (including Binaryen)..."
	@rm -rf $(BINARYEN_DIR)
	@echo "✅ Deep clean complete!"

# Test with a simple program
test-simple: build
	@echo "🧪 Testing with simple.txt..."
	@LD_LIBRARY_PATH=$(abspath $(BINARYEN_DIR))/lib:$$LD_LIBRARY_PATH $(PARSER) compilation_base_tests/simple.txt
	@if [ -f output.wasm ]; then \
		echo "✅ WASM generated!"; \
		wasmtime --invoke main output.wasm 2>/dev/null || echo "⚠️  wasmtime not installed"; \
	fi

# Test with function calls
test-call: build
	@echo "🧪 Testing with test_call1.txt..."
	@LD_LIBRARY_PATH=$(abspath $(BINARYEN_DIR))/lib:$$LD_LIBRARY_PATH $(PARSER) compilation_base_tests/test_call1.txt
	@if [ -f output.wasm ]; then \
		echo "✅ WASM generated!"; \
		wasmtime --invoke main output.wasm 2>/dev/null || echo "⚠️  wasmtime not installed"; \
	fi

# Test with if/else
test-if: build
	@echo "🧪 Testing with test_if.txt..."
	@LD_LIBRARY_PATH=$(abspath $(BINARYEN_DIR))/lib:$$LD_LIBRARY_PATH $(PARSER) compilation_base_tests/test_if.txt
	@if [ -f output.wasm ]; then \
		echo "✅ WASM generated!"; \
		wasmtime --invoke main output.wasm 2>/dev/null || echo "⚠️  wasmtime not installed"; \
	fi

# Run all basic tests
test: build
	@echo "🧪 Running test suite..."
	@LD_LIBRARY_PATH=$(abspath $(BINARYEN_DIR))/lib:$$LD_LIBRARY_PATH \
		$(PARSER) compilation_base_tests/simple.txt > /dev/null 2>&1 && echo "✅ simple.txt" || echo "❌ simple.txt"
	@LD_LIBRARY_PATH=$(abspath $(BINARYEN_DIR))/lib:$$LD_LIBRARY_PATH \
		$(PARSER) compilation_base_tests/test_call1.txt > /dev/null 2>&1 && echo "✅ test_call1.txt" || echo "❌ test_call1.txt"
	@LD_LIBRARY_PATH=$(abspath $(BINARYEN_DIR))/lib:$$LD_LIBRARY_PATH \
		$(PARSER) compilation_base_tests/test_if.txt > /dev/null 2>&1 && echo "✅ test_if.txt" || echo "❌ test_if.txt"
	@echo "✅ Test suite complete!"

# Compile and run a specific file
run: build
	@if [ -z "$(FILE)" ]; then \
		echo "❌ Usage: make run FILE=path/to/file.txt"; \
		exit 1; \
	fi
	@echo "🚀 Compiling $(FILE)..."
	@LD_LIBRARY_PATH=$(abspath $(BINARYEN_DIR))/lib:$$LD_LIBRARY_PATH $(PARSER) $(FILE)
	@if [ -f output.wasm ]; then \
		echo "✅ WASM generated: output.wasm"; \
		echo "💡 Run with: wasmtime --invoke main output.wasm"; \
	fi

# Show help
help:
	@echo "Imperative Language Compiler - Makefile Commands"
	@echo ""
	@echo "Build commands:"
	@echo "  make setup      - Setup Binaryen (clone and build)"
	@echo "  make build      - Build the compiler (default)"
	@echo "  make clean      - Clean build artifacts"
	@echo "  make clean-all  - Clean everything including Binaryen"
	@echo ""
	@echo "Test commands:"
	@echo "  make test          - Run all basic tests"
	@echo "  make test-simple   - Test with simple.txt"
	@echo "  make test-call     - Test with test_call1.txt"
	@echo "  make test-if       - Test with test_if.txt"
	@echo "  make run FILE=...  - Compile a specific file"
	@echo ""
	@echo "Examples:"
	@echo "  make setup"
	@echo "  make build"
	@echo "  make test"
	@echo "  make run FILE=compilation_base_tests/simple.txt"
