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
        std::vector<uint8_t> paramTypes;   // wasm value types
        std::vector<uint8_t> resultTypes;  // 0 or 1
        std::shared_ptr<ASTNode> node;     // ROUTINE_DECL
        uint32_t typeIndex;                // index in type section
        uint32_t funcIndex;                // index in function index space
    };

    // All functions in program (helpers + main)
    std::vector<FuncInfo> funcs;
    // Name->function index
    std::unordered_map<std::string, uint32_t> funcIndexByName;

    // Per-function locals: name -> local index
    std::unordered_map<std::string,int> localVarIndices;
    int nextLocalIndex;

    // Array variable tracking (add to private members)
    struct ArrayInfo {
        uint8_t elemType;           // WASM type (0x7f for i32, 0x7c for f64)
        std::string elemTypeName;   // Original type name ("integer", "real", "Person")
        int size;
        int baseOffset;
    };
    std::unordered_map<std::string, ArrayInfo> arrayInfos;
    // Memory management for arrays
    int globalMemoryOffset;

    struct RecordInfo {
        std::string name;
        std::vector<std::pair<std::string, std::pair<uint8_t, int>>> fields;
        // field_name -> (type, offset_in_bytes)
        int totalSize;
    };

    std::unordered_map<std::string, RecordInfo> recordTypes;
        struct RecordVarInfo {
        std::string recordType;
        int baseOffset; // In linear memory
        int size;
    };

    std::unordered_map<std::string, RecordVarInfo> recordVariables;

    struct GlobalVarInfo {
        std::string name;
        uint8_t type;
        int memoryOffset;  // Offset in linear memory
        int size;          // Size in bytes
    };
    std::unordered_map<std::string, GlobalVarInfo> globalVars;
    std::unordered_map<std::string, ArrayInfo> globalArrays;  // Global arrays
public:
    WasmCompiler() : nextLocalIndex(0), globalMemoryOffset(0) {}

    // Compile full AST into a single-module WASM file exporting `main`
    bool compile(std::shared_ptr<ASTNode> program, const std::string& filename);

private:
    // Collect and index all routines
    bool collectFunctions(std::shared_ptr<ASTNode> program);

    // Signature inference
    void analyzeFunctionSignature(FuncInfo& F);
    uint8_t mapPrimitiveToWasm(const std::string& tname);

    // Wasm writers
    void writeUnsignedLeb128(std::vector<uint8_t>& buf, uint32_t value);
    void writeString(std::vector<uint8_t>& buf, const std::string& s);
    void writeSignedLeb128(std::vector<uint8_t>& buf, int32_t v);

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
    void generateForLoop(std::vector<uint8_t>& body,
                         std::shared_ptr<ASTNode> forNode,
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
    void emitI32Load(std::vector<uint8_t>& body, uint32_t offset);
    void emitI32Store(std::vector<uint8_t>& body, uint32_t offset);
    void emitF64Load(std::vector<uint8_t>& body, uint32_t offset);
    void emitF64Store(std::vector<uint8_t>& body, uint32_t offset);
    std::vector<uint8_t> buildMemorySection();

    // For array type handling
    uint8_t getArrayType(std::shared_ptr<ASTNode> arrayTypeNode);
    std::tuple<uint8_t, std::string, int> analyzeArrayType(std::shared_ptr<ASTNode> arrayTypeNode);
    
    // Array and member access generation
    void generateArrayAccess(std::vector<uint8_t>& body,
                             std::shared_ptr<ASTNode> arrayAccess,
                             const FuncInfo& F);
    void generateMemberAccess(std::vector<uint8_t>& body,
                              std::shared_ptr<ASTNode> memberAccess,
                              const FuncInfo& F);
    void generateArrayAssignment(std::vector<uint8_t>& body,
                                 std::shared_ptr<ASTNode> arrayAccess,
                                 std::shared_ptr<ASTNode> rhs,
                                 const FuncInfo& F);
    std::tuple<int, uint8_t, int> resolveArrayMember(std::vector<uint8_t>& body,
                                                               std::shared_ptr<ASTNode> memberAccess,
                                                               const FuncInfo& F);
    void generateArrayAccessForRecord(std::vector<uint8_t>& body,
                                                std::shared_ptr<ASTNode> arrayAccess,
                                                const FuncInfo& F);
    std::tuple<int, uint8_t, int> resolveArrayAccessMember(std::vector<uint8_t>& body,
                                                          std::shared_ptr<ASTNode> arrayAccess,
                                                          const std::string& fieldName,
                                                          const FuncInfo& F);
    void generateSimpleArrayAccess(std::vector<uint8_t>& body,
                                   std::shared_ptr<ASTNode> arrayRef,
                                   std::shared_ptr<ASTNode> indexExpr,
                                   const FuncInfo& F);
    
    void generateMemberArrayAccess(std::vector<uint8_t>& body,
                                   std::shared_ptr<ASTNode> memberAccess,
                                   std::shared_ptr<ASTNode> indexExpr,
                                   const FuncInfo& F);
    // Records
    void collectRecordTypes(std::shared_ptr<ASTNode> program);
    std::pair<uint8_t, int> analyzeFieldType(std::shared_ptr<ASTNode> fieldDecl);
    void generateMemberAssignment(std::vector<uint8_t>& body,
                              std::shared_ptr<ASTNode> memberAccess,
                              std::shared_ptr<ASTNode> rhs,
                              const FuncInfo& F);
};

#endif // WASM_COMPILER_H
