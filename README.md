# Main-project
### Technology stack
- C++ (compiler language)
- Flex
- Bison

### Before any run make sure binaryen is compiled and cloned
    > git submodule update

### Then compile lib
    > cd binaryen
    > cmake -DBUILD_TESTS=OFF . 2>&1
    > make -j$(nproc) 2>&1

### As lib is compiled build project
    > cd ..
    > make
### Now you can run compiler from
    > build/parser [test].txt
    > wasmtime --invoke main output.wasm

