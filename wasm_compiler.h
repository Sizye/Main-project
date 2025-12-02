#ifndef WASM_COMPILER_H
#define WASM_COMPILER_H

#include "ast.h"

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <set>

// Binaryen includes
#include <wasm.h>
#include <wasm-builder.h>
#include <wasm-io.h>
#include <pass.h>

// Type system for type checking and conversion
enum class ValueType {
    INTEGER,
    REAL,
    BOOLEAN,
    UNKNOWN
};

class WasmCompiler {
private:
    struct FuncInfo {
        std::string name;
        std::vector<wasm::Type> paramTypes;   // Binaryen types
        std::vector<wasm::Type> resultTypes;  // Binaryen types
        std::shared_ptr<ASTNode> node;       // ROUTINE_DECL
        uint32_t typeIndex;                  // index in type section
        uint32_t funcIndex;                  // index in function index space
    };

    // Binaryen module
    std::unique_ptr<wasm::Module> module;
    wasm::Builder builder;

    // All functions in program (helpers + main)
    std::vector<FuncInfo> funcs;
    // Name->function index
    std::unordered_map<std::string, uint32_t> funcIndexByName;

    // Per-function locals: name -> local index
    std::unordered_map<std::string, int> localVarIndices;
    std::unordered_map<std::string, wasm::Type> localVarTypes;  // Track local variable types
    int nextLocalIndex;
    
    // Track if print statements are used
    bool hasPrintStatements;

    // Array variable tracking
    struct ArrayInfo {
        wasm::Type elemType;        // Binaryen type
        std::string elemTypeName;   // Original type name ("integer", "real", "Person")
        int size;
        int baseOffset;
    };
    std::unordered_map<std::string, ArrayInfo> arrayInfos;
    // Memory management for arrays
    int globalMemoryOffset;

    struct RecordInfo {
        std::string name;
        std::vector<std::pair<std::string, std::pair<wasm::Type, int>>> fields;
        // field_name -> (type, offset_in_bytes)
        std::unordered_map<std::string, std::string> arrayFieldElementTypes;
        // field_name -> element_type_name (for array fields only)
        int totalSize;
    };

    std::unordered_map<std::string, RecordInfo> recordTypes;
    // Type definitions (for type aliases like "type myId is real")
    std::unordered_map<std::string, std::shared_ptr<ASTNode>> typeDefinitions;
    struct RecordVarInfo {
        std::string recordType;
        int baseOffset; // In linear memory
        int size;
    };

    std::unordered_map<std::string, RecordVarInfo> recordVariables;
    std::unordered_map<std::string, RecordVarInfo> globalRecordVariables;

    struct GlobalVarInfo {
        std::string name;
        wasm::Type type;
        int memoryOffset;  // Offset in linear memory
        int size;          // Size in bytes
        std::shared_ptr<ASTNode> initializer;  // Initializer expression (if any)
    };
    std::unordered_map<std::string, GlobalVarInfo> globalVars;
    std::unordered_map<std::string, ArrayInfo> globalArrays;  // Global arrays

public:
    WasmCompiler() : module(std::make_unique<wasm::Module>()), builder(*module), nextLocalIndex(0), globalMemoryOffset(0) {}

    // Compile full AST into a single-module WASM file exporting `main`
    bool compile(std::shared_ptr<ASTNode> program, const std::string& filename);

private:
    // Collect and index all routines
    bool collectFunctions(std::shared_ptr<ASTNode> program);

    // Signature inference
    void analyzeFunctionSignature(FuncInfo& F);
    wasm::Type mapPrimitiveToWasm(const std::string& tname);

    // Per-function codegen
    void resetLocals();
    void addParametersToLocals(const FuncInfo& F);
    void collectAllVariableDeclarations(std::shared_ptr<ASTNode> node,
                                       std::vector<std::shared_ptr<ASTNode>>& varDecls,
                                       bool insideBody = false);
    void collectAllLoopVariables(std::shared_ptr<ASTNode> node,
                                std::set<std::string>& loopVars,
                                bool insideBody = false);
    std::vector<wasm::Type> analyzeLocalVariables(const FuncInfo& F);
    
    // Helper to resolve type alias (e.g., "myId" -> "real")
    std::shared_ptr<ASTNode> resolveTypeAlias(const std::string& typeName) const;
    
    wasm::Expression* generateFunctionBody(const FuncInfo& F);
    void generateVarDeclaration(std::vector<wasm::Expression*>& body,
                                std::shared_ptr<ASTNode> decl,
                                const FuncInfo& F);

    // Statements
    wasm::Expression* generateAssignment(std::shared_ptr<ASTNode> assignment,
                                       const FuncInfo& F);
    wasm::Expression* generateIfStatement(std::shared_ptr<ASTNode> ifStmt,
                                          const FuncInfo& F);
    wasm::Expression* generateWhileLoop(std::shared_ptr<ASTNode> whileStmt,
                                       const FuncInfo& F);
    wasm::Expression* generateForLoop(std::shared_ptr<ASTNode> forNode,
                                     const FuncInfo& F);
    wasm::Expression* generateReturn(std::shared_ptr<ASTNode> returnStmt,
                                    const FuncInfo& F);

    // Expressions
    wasm::Expression* generateExpression(std::shared_ptr<ASTNode> expr,
                                        const FuncInfo& F);
    wasm::Expression* generateBinaryOp(std::shared_ptr<ASTNode> bin,
                                      const FuncInfo& F);
    wasm::Expression* generateCall(std::shared_ptr<ASTNode> call,
                                  const FuncInfo& F);

    // Small emitters
    wasm::Expression* emitI32Const(int v);
    wasm::Expression* emitF64Const(double d);
    wasm::Expression* emitLocalGet(const std::string& name);
    wasm::Expression* emitRecordBaseAddress(const std::string& name);
    wasm::Expression* emitLocalSet(const std::string& name, wasm::Expression* value);
    
    // Memory section setup
    void setupMemory();

    // For array type handling
    std::tuple<wasm::Type, std::string, int> analyzeArrayType(std::shared_ptr<ASTNode> arrayTypeNode);
    
    // Array and member access generation
    wasm::Expression* generateArrayAccess(std::shared_ptr<ASTNode> arrayAccess,
                                         const FuncInfo& F);
    wasm::Expression* adjustArrayIndexToZeroBased(wasm::Expression* oneBasedIndex,
                                                  const std::string& debugContext);
    wasm::Expression* generateMemberAccess(std::shared_ptr<ASTNode> memberAccess,
                                          const FuncInfo& F);
    wasm::Expression* generateArrayAssignment(std::shared_ptr<ASTNode> arrayAccess,
                                             wasm::Expression* rhs,
                                             const FuncInfo& F);
    bool resolveRecordTypeForIdentifier(const std::string& name,
                                        const FuncInfo& F,
                                        std::string& recordTypeOut) const;
    wasm::Expression* generateSimpleArrayAccess(std::shared_ptr<ASTNode> arrayRef,
                                                std::shared_ptr<ASTNode> indexExpr,
                                                const FuncInfo& F);
    
    wasm::Expression* generateMemberArrayAccess(std::shared_ptr<ASTNode> memberAccess,
                                                std::shared_ptr<ASTNode> indexExpr,
                                                const FuncInfo& F);
    
    // Records
    void collectRecordTypes(std::shared_ptr<ASTNode> program);
    std::pair<wasm::Type, int> analyzeFieldType(std::shared_ptr<ASTNode> fieldDecl);
    wasm::Expression* generateMemberAssignment(std::shared_ptr<ASTNode> memberAccess,
                                             wasm::Expression* rhs,
                                             const FuncInfo& F);
    
    // Type system
    ValueType getExpressionType(std::shared_ptr<ASTNode> expr, const FuncInfo& F);
    wasm::Expression* emitTypeConversion(wasm::Expression* expr, ValueType fromType, ValueType toType);
    bool validateAssignmentConversion(ValueType fromType, ValueType toType, const std::string& context = "");
    
    // Print statement
    wasm::Expression* generatePrintStatement(std::shared_ptr<ASTNode> printStmt,
                                           const FuncInfo& F);
    
    
    // Add imported print functions
    void addPrintImports();
};

#endif // WASM_COMPILER_H
