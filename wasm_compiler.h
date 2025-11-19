#ifndef WASM_COMPILER_H
#define WASM_COMPILER_H

#include "ast.h"

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

class WasmCompiler {
private:
    struct FuncInfo {
        std::string name;
        std::vector<uint8_t> paramTypes;  // wasm value types
        std::vector<uint8_t> resultTypes; // 0 or 1
        std::shared_ptr<ASTNode> node;    // ROUTINE_DECL
        uint32_t typeIndex;               // index in type section
        uint32_t funcIndex;               // index in function index space
    };

    // All functions in program (helpers + main)
    std::vector<FuncInfo> funcs;
    // Name->function index
    std::unordered_map<std::string, uint32_t> funcIndexByName;

    // Per-function locals: name -> local index
    std::unordered_map<std::string,int> localVarIndices;
    int nextLocalIndex;

public:
    WasmCompiler() : nextLocalIndex(0) {}

    bool compile(std::shared_ptr<ASTNode> program, const std::string& filename);

private:
    // Collect and index all routines
    bool collectFunctions(std::shared_ptr<ASTNode> program);

    // Signature inference
    void analyzeFunctionSignature(FuncInfo& F);
    uint8_t mapPrimitiveToWasm(const std::string& tname);

    // Wasm writers
    void writeLeb128(std::vector<uint8_t>& buf, uint32_t value);
    void writeString(std::vector<uint8_t>& buf, const std::string& s);

    // Sections for all functions
    std::vector<uint8_t> buildTypeSection();
    std::vector<uint8_t> buildFunctionSection();
    std::vector<uint8_t> buildExportSection();
    std::vector<uint8_t> buildCodeSection();

    // Per-function codegen (called from buildCodeSection)
    void resetLocals();
    void addParametersToLocals(const FuncInfo& F);
    std::vector<uint8_t> analyzeLocalVariables(const FuncInfo& F);
    void generateLocalInitializers(std::vector<uint8_t>& body, const FuncInfo& F);
    void generateFunctionBody(std::vector<uint8_t>& body, const FuncInfo& F);

    // Statements
    void generateAssignment(std::vector<uint8_t>& body,
                            std::shared_ptr<ASTNode> assignment,
                            const FuncInfo& F);
    void generateIfStatement(std::vector<uint8_t>& body,
                             std::shared_ptr<ASTNode> ifStmt,
                             const FuncInfo& F);
    void generateWhileLoop(std::vector<uint8_t>& body,
                           std::shared_ptr<ASTNode> whileStmt,
                           const FuncInfo& F);
    void generateReturn(std::vector<uint8_t>& body,
                        std::shared_ptr<ASTNode> returnStmt,
                        const FuncInfo& F);

    // Expressions
    void generateExpression(std::vector<uint8_t>& body,
                            std::shared_ptr<ASTNode> expr,
                            const FuncInfo& F);
    void generateBinaryOp(std::vector<uint8_t>& body,
                          std::shared_ptr<ASTNode> bin,
                          const FuncInfo& F);
    void generateCall(std::vector<uint8_t>& body,
                      std::shared_ptr<ASTNode> call,
                      const FuncInfo& F);

    // Small emitters
    void emitI32Const(std::vector<uint8_t>& body, int v);
    void emitF64Const(std::vector<uint8_t>& body, double d);
    void emitLocalGet(std::vector<uint8_t>& body, const std::string& name);
    void emitLocalSet(std::vector<uint8_t>& body, const std::string& name);
};

#endif // WASM_COMPILER_H
