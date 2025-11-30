# Binaryen API Fixes Needed

The Binaryen API has changed significantly. Here are the fixes needed:

## Critical API Changes

1. **Function Signature**: `func->sig` → `func->type` (or use constructor)
2. **Export**: `wasm::Export::Function` → Check Export API
3. **PassRunner**: May need different include or namespace
4. **ModuleWriter**: API may have changed
5. **makeLoad/makeStore**: Parameter order changed - need `wasm::Type` not `wasm::Type::BasicType`
6. **Memory**: `module->memory` → `module->memories` (vector)

## Quick Fix Strategy

The Binaryen version we're using has a different API than expected. We have two options:

1. **Fix all API calls** (time-consuming but proper)
2. **Use a specific Binaryen version** that matches our code
3. **Simplify the implementation** to use basic Binaryen features

For now, let's test if we can get a minimal version working.

