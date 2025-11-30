# Binaryen Migration Summary

## Overview

The WASM compiler has been completely rewritten to use Binaryen instead of manual WASM binary encoding. This provides a cleaner, more maintainable codebase with automatic optimizations.

## What Changed

### 1. Header File (`wasm_compiler.h`)

**Before:** Manual binary encoding with `std::vector<uint8_t>` for WASM bytecode
**After:** Binaryen API using `wasm::Expression*`, `wasm::Module`, and `wasm::Builder`

**Key Changes:**
- Replaced `std::vector<uint8_t>& body` parameters with `wasm::Expression*` return types
- Added Binaryen includes: `wasm.h`, `wasm-builder.h`, `wasm-printing.h`
- Changed type system from `uint8_t` (raw WASM types) to `wasm::Type` (Binaryen types)
- Added `wasm::Module` and `wasm::Builder` as class members

### 2. Implementation File (`wasm_compiler.cpp`)

**Before:** ~2688 lines of manual binary encoding
**After:** ~1800 lines using Binaryen's high-level API

**Key Changes:**
- **Expression Generation:** All expression generation now returns `wasm::Expression*` instead of pushing bytes
- **Function Creation:** Functions are created using `wasm::Function` objects instead of manual encoding
- **Module Building:** Module is built using Binaryen's `Module` class
- **Optimization:** Automatic optimization via Binaryen's `PassRunner`
- **File Writing:** Uses `ModuleWriter` instead of manual binary writing

### 3. Build System (`CMakeLists.txt`)

**Added:**
- Binaryen detection and linking
- Support for system-installed or local Binaryen
- Thread library linking (required by Binaryen)

## API Mapping

### Expression Generation

| Old (Manual Encoding) | New (Binaryen) |
|----------------------|----------------|
| `emitI32Const(body, v)` | `builder.makeConst(wasm::Literal(v))` |
| `emitF64Const(body, d)` | `builder.makeConst(wasm::Literal(d))` |
| `emitLocalGet(body, name)` | `builder.makeLocalGet(index, type)` |
| `emitLocalSet(body, name)` | `builder.makeLocalSet(index, value)` |
| `body.push_back(0x6a)` (i32.add) | `builder.makeBinary(wasm::AddInt32, left, right)` |
| `body.push_back(0x28)` (i32.load) | `builder.makeLoad(4, false, 0, 0, addr, type)` |
| `body.push_back(0x36)` (i32.store) | `builder.makeStore(4, 0, 0, addr, value, type)` |

### Control Flow

| Old | New |
|-----|-----|
| Manual block/loop encoding | `builder.makeBlock()`, `builder.makeLoop()`, `builder.makeIf()` |
| Manual break encoding | `builder.makeBreak()` |
| Manual return encoding | `builder.makeReturn()` |

### Function Creation

| Old | New |
|-----|-----|
| Manual type/function/code sections | `wasm::Function` objects with `func->sig`, `func->body` |
| Manual export encoding | `module->addExport()` |

## Benefits

1. **Cleaner Code:** ~900 lines shorter, more readable
2. **Type Safety:** Binaryen's type system catches errors at compile time
3. **Automatic Optimization:** Binaryen's optimization passes improve generated code
4. **Easier Maintenance:** High-level API is easier to understand and modify
5. **Better Error Handling:** Binaryen validates WASM modules automatically

## Testing

After setting up Binaryen (see `BINARYEN_SETUP.md`), test the compiler:

```bash
# Build the project
mkdir build && cd build
cmake ..
make

# Test with a simple program
echo 'routine main(): integer is return 42; end' > test.txt
./parser test.txt

# Verify WASM was generated
file output.wasm  # Should show "WebAssembly"

# Run it (if wasmtime is installed)
wasmtime --invoke main output.wasm  # Should output: 42
```

## Potential Issues and Fixes

### Issue: Compilation Errors

If you see errors about missing Binaryen headers or undefined references:

1. **Check Binaryen Installation:**
   ```bash
   ls /usr/local/include/wasm.h  # or wherever you installed it
   ls /usr/local/lib/libbinaryen.a
   ```

2. **Set CMake Variables:**
   ```bash
   cmake -DBINARYEN_INCLUDE_DIR=/path/to/binaryen/src \
         -DBINARYEN_LIBRARY=/path/to/binaryen/lib/libbinaryen.a \
         .
   ```

### Issue: Runtime Errors

If the generated WASM doesn't work:

1. **Check Binaryen Version:** Ensure you're using a recent version (2020+)
2. **Verify Module:** Binaryen automatically validates modules, so if it compiles, the WASM should be valid
3. **Check Optimization:** Try disabling optimizations temporarily:
   ```cpp
   // In compile(), comment out:
   // passRunner.addDefaultOptimizationPasses();
   // passRunner.run();
   ```

### Issue: API Differences

Binaryen's API may vary slightly between versions. Common adjustments:

1. **Function Signatures:** May need to adjust `wasm::Signature` construction
2. **Expression Builders:** Method names might differ (e.g., `makeCall` vs `makeCallDirect`)
3. **Module Writing:** `ModuleWriter` API might need adjustment

Check Binaryen's documentation for your specific version.

## Migration Checklist

- [x] Rewrite header file to use Binaryen types
- [x] Rewrite implementation to use Binaryen API
- [x] Update CMakeLists.txt for Binaryen
- [x] Create setup guide
- [ ] Test compilation with Binaryen
- [ ] Test generated WASM files
- [ ] Verify all language features work
- [ ] Update documentation

## Next Steps

1. **Install Binaryen** (see `BINARYEN_SETUP.md`)
2. **Build the project** and fix any compilation issues
3. **Test with existing test files** in `compilation_base_tests/`
4. **Report any issues** - API differences may need adjustment

## Files Modified

- `wasm_compiler.h` - Complete rewrite for Binaryen
- `wasm_compiler.cpp` - Complete rewrite for Binaryen  
- `CMakeLists.txt` - Added Binaryen detection and linking
- `wasm_compiler_old.cpp` - Backup of original implementation

## Files Created

- `BINARYEN_SETUP.md` - Setup and installation guide
- `BINARYEN_MIGRATION.md` - This file

## Support

If you encounter issues:

1. Check `BINARYEN_SETUP.md` for installation problems
2. Verify Binaryen version compatibility
3. Check Binaryen's GitHub issues for known problems
4. Review Binaryen documentation for API details

