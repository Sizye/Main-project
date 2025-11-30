#include <wasm.h>
#include <wasm-builder.h>
#include <wasm-io.h>
#include <pass.h>
#include <iostream>
#include <fstream>

// Minimal example to test Binaryen API
int main() {
    std::cout << "Testing Binaryen API..." << std::endl;
    
    // Create module
    wasm::Module module;
    wasm::Builder builder(module);
    
    // Create a simple function: main() -> i32, returns 42
    wasm::Function* func = new wasm::Function();
    func->name = "main";
    
    // Set function type: () -> i32
    wasm::Signature sig(wasm::Type::none, wasm::Type::i32);
    func->type = wasm::Type(sig, wasm::NonNullable, wasm::Exact);
    
    // Function body: return 42
    func->body = builder.makeReturn(builder.makeConst(wasm::Literal(42)));
    
    // Add function to module
    module.addFunction(func);
    
    // Export the function
    wasm::Export* exp = new wasm::Export("main", wasm::ExternalKind::Function, wasm::Name("main"));
    module.addExport(exp);
    
    // Run optimizations
    wasm::PassOptions options;
    wasm::PassRunner passRunner(&module, options);
    passRunner.addDefaultOptimizationPasses();
    passRunner.run();
    
    // Write to file
    wasm::ModuleWriter writer(options);
    writer.setBinary(true);
    writer.write(module, "test_minimal.wasm");
    
    std::cout << "✅ Generated test_minimal.wasm" << std::endl;
    std::cout << "Test with: wasmtime --invoke main test_minimal.wasm" << std::endl;
    
    return 0;
}

