# Legacy Compiler Implementation

This directory contains the old manual WASM binary encoding implementation.

## Files

- `wasm_compiler_old.cpp` - Original manual WASM binary encoding implementation (~2689 lines)
- `wasm_compiler_binaryen.cpp` - Intermediate Binaryen implementation (superseded by current version)
- `test_binaryen_minimal.cpp` - Minimal Binaryen API test example used for reference

## Migration

The compiler has been migrated to use Binaryen's high-level API. See `../docs/BINARYEN_MIGRATION.md` for details.

These files are kept for reference and historical purposes.

