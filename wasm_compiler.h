#ifndef WASM_COMPILER_H
#define WASM_COMPILER_H

#include "ast.h"
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

class WasmCompiler {
private:
    std::unordered_map<std::string, int> globalVars;
    std::unordered_map<std::string, std::string> functions;
    std::unordered_map<std::string, int> localVarIndices; // var_name -> local_index
    int nextLocalIndex;

public:
    WasmCompiler() : nextLocalIndex(0) {}
    
    bool compile(std::shared_ptr<ASTNode> ast, const std::string& filename);
    
private:
    int extractReturnValueFromAST(std::shared_ptr<ASTNode> ast);
    std::shared_ptr<ASTNode> findMainFunction(std::shared_ptr<ASTNode> program);
    int extractIntegerFromReturn(std::shared_ptr<ASTNode> mainFunc);
    
    void generateWasmWithReturnValue(std::ofstream& file, int returnValue, std::shared_ptr<ASTNode> mainFunc);
    std::vector<uint8_t> buildTypeSection(std::shared_ptr<ASTNode> mainFunc);
    uint8_t mapTypeToWasm(const std::string& typeName);
    uint8_t inferReturnTypeFromBody(std::shared_ptr<ASTNode> mainFunc);
    uint8_t extractTypeFromParam(std::shared_ptr<ASTNode> param);
    std::vector<uint8_t> analyzeReturnType(std::shared_ptr<ASTNode> mainFunc);
    std::vector<uint8_t> analyzeParameters(std::shared_ptr<ASTNode> mainFunc);
    std::vector<uint8_t> analyzeFunctionSignature(std::shared_ptr<ASTNode> mainFunc);

    bool shouldGenerateParameterAccess(std::shared_ptr<ASTNode> mainFunc);

    std::vector<uint8_t> buildFunctionSection(); 
    std::vector<uint8_t> buildExportSection();
    std::vector<uint8_t> buildCodeSection(int returnValue, std::shared_ptr<ASTNode> mainFunc);

    uint8_t inferTypeFromIdentifier(const std::string& name, std::shared_ptr<ASTNode> mainFunc);
    void generateConstantInstruction(std::vector<uint8_t>& funcBody, uint8_t type, std::shared_ptr<ASTNode> mainFunc);
    void generateI32Constant(std::vector<uint8_t>& funcBody, const std::string& value, std::shared_ptr<ASTNode> mainFunc);
    void generateF64Constant(std::vector<uint8_t>& funcBody, const std::string& value, std::shared_ptr<ASTNode> mainFunc);
    std::string extractLiteralValueFromReturn(std::shared_ptr<ASTNode> mainFunc);
    std::shared_ptr<ASTNode> getReturnExpression(std::shared_ptr<ASTNode> mainFunc);

    std::vector<uint8_t> analyzeLocalVariables(std::shared_ptr<ASTNode> mainFunc);
    int countParameters(std::shared_ptr<ASTNode> mainFunc);

    void generateLocalVariableInitialization(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> mainFunc);
    void generateMainLogic(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> mainFunc);
    void generateExpression(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> expr, std::shared_ptr<ASTNode> mainFunc);
    void generateVariableLoad(std::vector<uint8_t>& funcBody, const std::string& varName, std::shared_ptr<ASTNode> mainFunc);
    void generateAssignment(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> assignment, std::shared_ptr<ASTNode> mainFunc);
    void generateReturnStatement(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> returnStmt, std::shared_ptr<ASTNode> mainFunc);
    void generateRealLiteral(std::vector<uint8_t>& funcBody, const std::string& value);
    void generateBinaryOperation(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> binaryOp, std::shared_ptr<ASTNode> mainFunc);

    uint8_t inferLocalVariableType(std::shared_ptr<ASTNode> varDecl);
    void writeLeb128(std::vector<uint8_t>& vec, uint32_t value);
    void writeString(std::vector<uint8_t>& vec, const std::string& str);
};

#endif