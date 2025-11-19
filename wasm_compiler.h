#ifndef WASM_COMPILER_H
#define WASM_COMPILER_H

#include "ast.h"

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

class WasmCompiler {
private:
    // Optional: reserved for future use (globals/functions)
    std::unordered_map<std::string,int>         globalVars;
    std::unordered_map<std::string,std::string> functions;

    // Local variable name -> wasm local index
    std::unordered_map<std::string,int> localVarIndices;
    int nextLocalIndex;

public:
    WasmCompiler() : nextLocalIndex(0) {}

    // Compile full AST into a single-module WASM file exporting `main`
    bool compile(std::shared_ptr<ASTNode> program,
                 const std::string& filename);

private:
    // High-level helpers
    std::shared_ptr<ASTNode> findMainFunction(std::shared_ptr<ASTNode> program);
    uint8_t                  mapPrimitiveToWasm(const std::string& typeName);
    uint8_t                  inferReturnType(std::shared_ptr<ASTNode> mainFunc);

    // Low-level encoding helpers
    void writeLeb128(std::vector<uint8_t>& buf, uint32_t value);
    void writeString(std::vector<uint8_t>& buf, const std::string& s);

    // Section builders
    std::vector<uint8_t> buildTypeSection(std::shared_ptr<ASTNode> mainFunc);
    std::vector<uint8_t> buildFunctionSection();
    std::vector<uint8_t> buildExportSection();
    std::vector<uint8_t> buildCodeSection(std::shared_ptr<ASTNode> mainFunc);

    // Locals and initialization
    std::vector<uint8_t> analyzeLocalVariables(std::shared_ptr<ASTNode> mainFunc);
    void generateLocalVariableInitialization(std::vector<uint8_t>& body,
                                             std::shared_ptr<ASTNode> mainFunc);

    // Statements
    void generateMainLogic(std::vector<uint8_t>& body,
                           std::shared_ptr<ASTNode> mainFunc);
    void generateIfStatement(std::vector<uint8_t>& body,
                             std::shared_ptr<ASTNode> ifStmt,
                             std::shared_ptr<ASTNode> mainFunc);
    void generateWhileLoop(std::vector<uint8_t>& body,
                           std::shared_ptr<ASTNode> whileStmt,
                           std::shared_ptr<ASTNode> mainFunc);
    void generateAssignment(std::vector<uint8_t>& body,
                            std::shared_ptr<ASTNode> assignment,
                            std::shared_ptr<ASTNode> mainFunc);
    void generateReturn(std::vector<uint8_t>& body,
                        std::shared_ptr<ASTNode> returnStmt,
                        std::shared_ptr<ASTNode> mainFunc);

    // Expressions
    void generateI32Const(std::vector<uint8_t>& body, int value);
    void generateF64Const(std::vector<uint8_t>& body, double value);
    void generateVariableLoad(std::vector<uint8_t>& body,
                              const std::string& name);
    void generateBinaryOp(std::vector<uint8_t>& body,
                          std::shared_ptr<ASTNode> binaryOp,
                          std::shared_ptr<ASTNode> mainFunc);
    void generateExpression(std::vector<uint8_t>& body,
                            std::shared_ptr<ASTNode> expr,
                            std::shared_ptr<ASTNode> mainFunc);
};

#endif // WASM_COMPILER_H
