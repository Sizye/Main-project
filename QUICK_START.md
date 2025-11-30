# Quick Start Guide

## Building

```bash
# Setup Binaryen (first time only)
make setup

# Build the compiler
make build

# Or just use 'make' (builds by default)
make
```

## Testing

```bash
# Run all basic tests
make test

# Test specific examples
make test-simple   # Test simple.txt
make test-call     # Test function calls
make test-if       # Test if/else

# Compile and run a specific file
make run FILE=compilation_base_tests/simple.txt
```

## Using the Compiler

### Option 1: Using the Makefile
```bash
make run FILE=path/to/your/file.txt
```

### Option 2: Using the compile script
```bash
# Compile only
./compile.sh path/to/your/file.txt

# Compile and run
./compile.sh path/to/your/file.txt --run
```

### Option 3: Direct usage
```bash
# Set library path and run
export LD_LIBRARY_PATH=./binaryen/lib:$LD_LIBRARY_PATH
./build/parser path/to/your/file.txt

# Run generated WASM
wasmtime --invoke main output.wasm
```

## Project Structure

```
.
├── build/              # CMake build directory
├── legacy/             # Old compiler implementations
├── docs/               # Documentation
├── compilation_base_tests/  # Test files
├── wasm_compiler.cpp   # Main compiler (Binaryen-based)
├── wasm_compiler.h     # Compiler header
├── Makefile            # Build and test commands
└── compile.sh          # Simple compilation script
```

## Cleaning

```bash
# Clean build artifacts
make clean

# Deep clean (including Binaryen)
make clean-all
```

## Help

```bash
make help    # Show all available commands
```

