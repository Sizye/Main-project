# Binaryen Setup Guide

This guide explains how to set up Binaryen for the WASM compiler project.

## What is Binaryen?

Binaryen is a compiler and toolchain infrastructure library for WebAssembly, written in C++. It provides a high-level API for building, optimizing, and emitting WebAssembly modules, replacing the need for manual binary encoding.

## Installation Methods

### Method 1: Build from Source (Recommended)

This is the most reliable method and ensures you have the latest version.

1. **Clone Binaryen repository:**
   ```bash
   git clone https://github.com/WebAssembly/binaryen.git
   cd binaryen
   ```

2. **Build Binaryen:**
   ```bash
   cmake .
   make -j$(nproc)  # Use all CPU cores for faster compilation
   ```

3. **Install Binaryen (optional, for system-wide installation):**
   ```bash
   sudo make install
   ```

   This installs Binaryen to `/usr/local` by default. You can change the installation prefix:
   ```bash
   cmake -DCMAKE_INSTALL_PREFIX=/path/to/install .
   make
   sudo make install
   ```

4. **Verify installation:**
   ```bash
   ls -la lib/libbinaryen.a  # Should exist after building
   ls -la src/wasm.h          # Header file should exist
   ```

### Method 2: Use System Package Manager

Some Linux distributions provide Binaryen packages:

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install binaryen
```

**Arch Linux:**
```bash
sudo pacman -S binaryen
```

**macOS (Homebrew):**
```bash
brew install binaryen
```

Note: Package versions may be older than building from source.

### Method 3: Development Setup (In Project Directory)

If you want to keep Binaryen in your project directory:

1. **Clone Binaryen into your project:**
   ```bash
   cd /home/ilia/Desktop/vsCode/Main-project
   git clone https://github.com/WebAssembly/binaryen.git
   ```

2. **Build Binaryen:**
   ```bash
   cd binaryen
   cmake .
   make -j$(nproc)
   cd ..
   ```

3. **CMake will automatically detect it** if it's in the project directory.

## Configuring CMake

The CMakeLists.txt file is configured to find Binaryen in several locations:

1. System installation (`/usr/include`, `/usr/local/include`)
2. Project subdirectory (`${CMAKE_SOURCE_DIR}/binaryen`)
3. Custom paths (via CMake variables)

### Custom Binaryen Location

If Binaryen is installed in a custom location, you can specify it when running CMake:

```bash
cmake -DBINARYEN_INCLUDE_DIR=/path/to/binaryen/src \
      -DBINARYEN_LIBRARY=/path/to/binaryen/lib/libbinaryen.a \
      .
```

Or set environment variables:
```bash
export BINARYEN_INCLUDE_DIR=/path/to/binaryen/src
export BINARYEN_LIBRARY=/path/to/binaryen/lib/libbinaryen.a
cmake .
```

## Building the Project

Once Binaryen is set up:

1. **Create build directory:**
   ```bash
   mkdir build
   cd build
   ```

2. **Configure with CMake:**
   ```bash
   cmake ..
   ```

   CMake should detect Binaryen and print:
   ```
   -- Found Binaryen: /path/to/binaryen/src
   ```

3. **Build the project:**
   ```bash
   make
   ```

4. **If Binaryen is not found**, CMake will print a warning with instructions.

## Troubleshooting

### Issue: "Binaryen not found"

**Solution 1:** Ensure Binaryen is built and the library exists:
```bash
ls -la /usr/local/lib/libbinaryen.a  # For system install
# or
ls -la binaryen/lib/libbinaryen.a    # For local install
```

**Solution 2:** Specify paths manually:
```bash
cmake -DBINARYEN_INCLUDE_DIR=/path/to/binaryen/src \
      -DBINARYEN_LIBRARY=/path/to/binaryen/lib/libbinaryen.a \
      .
```

### Issue: "undefined reference to Binaryen functions"

This means the linker can't find the Binaryen library.

**Solution:** Ensure `BINARYEN_LIBRARY` points to the correct `.a` file:
```bash
cmake -DBINARYEN_LIBRARY=/absolute/path/to/libbinaryen.a .
```

### Issue: "wasm.h: No such file or directory"

The include path is incorrect.

**Solution:** Ensure `BINARYEN_INCLUDE_DIR` points to the directory containing `wasm.h`:
```bash
cmake -DBINARYEN_INCLUDE_DIR=/path/to/binaryen/src .
```

### Issue: Build errors related to C++ standard

Binaryen requires C++11 or later. The project is already configured for C++11, but if you see errors:

**Solution:** Ensure your compiler supports C++11:
```bash
g++ --version  # Should be 4.8+ for GCC
```

## Testing the Setup

After building, test that everything works:

1. **Compile a test program:**
   ```bash
   echo 'routine main(): integer is return 42; end' > test.txt
   ./parser test.txt
   ```

2. **Check that output.wasm was created:**
   ```bash
   ls -lh output.wasm
   file output.wasm  # Should show "WebAssembly"
   ```

3. **Run the WASM file (if wasmtime is installed):**
   ```bash
   wasmtime --invoke main output.wasm
   # Should output: 42
   ```

## Dependencies

Binaryen itself requires:
- C++11 compatible compiler (GCC 4.8+, Clang 3.3+)
- CMake 3.10+
- Make or Ninja build system

The project requires:
- Binaryen (as described above)
- Flex (lexer generator)
- Bison (parser generator)
- C++11 compiler

## Additional Resources

- **Binaryen GitHub:** https://github.com/WebAssembly/binaryen
- **Binaryen Documentation:** https://github.com/WebAssembly/binaryen/wiki
- **WebAssembly Specification:** https://webassembly.org/

## Quick Start Summary

```bash
# 1. Install Binaryen
git clone https://github.com/WebAssembly/binaryen.git
cd binaryen
cmake . && make -j$(nproc)
sudo make install  # Optional
cd ..

# 2. Build the project
mkdir build && cd build
cmake ..
make

# 3. Test
echo 'routine main(): integer is return 42; end' > test.txt
./parser test.txt
wasmtime --invoke main output.wasm
```

## Differences from Manual WASM Encoding

The new Binaryen-based implementation:

1. **Uses high-level API** instead of manual byte encoding
2. **Automatic optimization** via Binaryen's optimization passes
3. **Type safety** through Binaryen's type system
4. **Easier to maintain** and extend
5. **Better error handling** through Binaryen's validation

The functionality remains the same, but the code is cleaner and more maintainable.

