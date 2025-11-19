#include "wasm_compiler.h"

#include <iostream>
#include <cstring>
#include <cmath>

// Debug helper
static std::string astNodeTypeToString(ASTNodeType type) {
    switch (type) {
        case ASTNodeType::PROGRAM:        return "PROGRAM";
        case ASTNodeType::VAR_DECL:       return "VAR_DECL";
        case ASTNodeType::TYPE_DECL:      return "TYPE_DECL";
        case ASTNodeType::ROUTINE_DECL:   return "ROUTINE_DECL";
        case ASTNodeType::ROUTINE_FORWARD_DECL: return "ROUTINE_FORWARD_DECL";
        case ASTNodeType::PARAMETER:      return "PARAMETER";
        case ASTNodeType::PRIMITIVE_TYPE: return "PRIMITIVE_TYPE";
        case ASTNodeType::ARRAY_TYPE:     return "ARRAY_TYPE";
        case ASTNodeType::RECORD_TYPE:    return "RECORD_TYPE";
        case ASTNodeType::USER_TYPE:      return "USER_TYPE";
        case ASTNodeType::BINARY_OP:      return "BINARY_OP";
        case ASTNodeType::UNARY_OP:       return "UNARY_OP";
        case ASTNodeType::LITERAL_INT:    return "LITERAL_INT";
        case ASTNodeType::LITERAL_REAL:   return "LITERAL_REAL";
        case ASTNodeType::LITERAL_BOOL:   return "LITERAL_BOOL";
        case ASTNodeType::LITERAL_STRING: return "LITERAL_STRING";
        case ASTNodeType::IDENTIFIER:     return "IDENTIFIER";
        case ASTNodeType::ROUTINE_CALL:   return "ROUTINE_CALL";
        case ASTNodeType::ARRAY_ACCESS:   return "ARRAY_ACCESS";
        case ASTNodeType::MEMBER_ACCESS:  return "MEMBER_ACCESS";
        case ASTNodeType::ASSIGNMENT:     return "ASSIGNMENT";
        case ASTNodeType::PRINT_STMT:     return "PRINT_STMT";
        case ASTNodeType::IF_STMT:        return "IF_STMT";
        case ASTNodeType::WHILE_LOOP:     return "WHILE_LOOP";
        case ASTNodeType::FOR_LOOP:       return "FOR_LOOP";
        case ASTNodeType::RETURN_STMT:    return "RETURN_STMT";
        case ASTNodeType::BODY:           return "BODY";
        default:                          return "UNKNOWN";
    }
}

// ============================================================================
// Public API
// ============================================================================

bool WasmCompiler::compile(std::shared_ptr<ASTNode> program,
                           const std::string& filename) {
    std::cout << "🚀 COMPILING TO WASM: " << filename << std::endl;

    auto mainFunc = findMainFunction(program);
    if (!mainFunc) {
        std::cerr << "❌ No main routine found in AST" << std::endl;
        return false;
    }

    // Module buffer
    std::vector<uint8_t> module;

    // Magic + version
    const uint8_t header[8] = {
        0x00, 0x61, 0x73, 0x6d, // "\0asm"
        0x01, 0x00, 0x00, 0x00  // version 1
    };
    module.insert(module.end(), header, header + 8);

    auto typeSection     = buildTypeSection(mainFunc);
    auto functionSection = buildFunctionSection();
    auto exportSection   = buildExportSection();
    auto codeSection     = buildCodeSection(mainFunc);

    module.insert(module.end(), typeSection.begin(),     typeSection.end());
    module.insert(module.end(), functionSection.begin(), functionSection.end());
    module.insert(module.end(), exportSection.begin(),   exportSection.end());
    module.insert(module.end(), codeSection.begin(),     codeSection.end());

    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::cerr << "❌ Failed to open " << filename << " for writing" << std::endl;
        return false;
    }
    out.write(reinterpret_cast<const char*>(module.data()), module.size());
    out.close();

    std::cout << "✅ WROTE WASM module (" << module.size() << " bytes)" << std::endl;
    std::cout << "💡 You can run it with: wasmtime " << filename
              << " --invoke main" << std::endl;
    return true;
}

// ============================================================================
// Main routine and types
// ============================================================================

std::shared_ptr<ASTNode> WasmCompiler::findMainFunction(std::shared_ptr<ASTNode> program) {
    if (!program || program->type != ASTNodeType::PROGRAM) return nullptr;

    for (auto& child : program->children) {
        if (child &&
            child->type == ASTNodeType::ROUTINE_DECL &&
            child->value == "main") {
            std::cout << "✅ FOUND main routine!" << std::endl;
            return child;
        }
    }
    std::cout << "❌ main routine not found" << std::endl;
    return nullptr;
}

// Map primitive type names to WASM value types
uint8_t WasmCompiler::mapPrimitiveToWasm(const std::string& typeName) {
    if (typeName == "integer" || typeName == "boolean") {
        return 0x7f; // i32
    } else if (typeName == "real") {
        return 0x7c; // f64
    }
    return 0x7f; // fallback i32
}

// For now: always return i32 type for main
uint8_t WasmCompiler::inferReturnType(std::shared_ptr<ASTNode> /*mainFunc*/) {
    return 0x7f; // i32
}

// ============================================================================
// LEB128 and strings
// ============================================================================

void WasmCompiler::writeLeb128(std::vector<uint8_t>& buf, uint32_t value) {
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        if (value != 0) byte |= 0x80;
        buf.push_back(byte);
    } while (value != 0);
}

void WasmCompiler::writeString(std::vector<uint8_t>& buf, const std::string& s) {
    writeLeb128(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

// ============================================================================
// Sections
// ============================================================================

// Type section: one function type for main: () -> i32
std::vector<uint8_t> WasmCompiler::buildTypeSection(std::shared_ptr<ASTNode> mainFunc) {
    uint8_t retType = inferReturnType(mainFunc);

    std::vector<uint8_t> payload;
    // type count
    writeLeb128(payload, 1);
    // func type
    payload.push_back(0x60); // func
    // param count = 0 (no params in tests)
    writeLeb128(payload, 0);
    // result types
    if (retType == 0x40) {
        writeLeb128(payload, 0); // no result
    } else {
        writeLeb128(payload, 1);
        payload.push_back(retType);
    }

    std::vector<uint8_t> section;
    section.push_back(0x01); // Type section id
    writeLeb128(section, static_cast<uint32_t>(payload.size()));
    section.insert(section.end(), payload.begin(), payload.end());
    return section;
}

// Function section: one function using type 0
std::vector<uint8_t> WasmCompiler::buildFunctionSection() {
    std::vector<uint8_t> payload;
    writeLeb128(payload, 1); // function count
    writeLeb128(payload, 0); // type index 0

    std::vector<uint8_t> section;
    section.push_back(0x03); // Function section
    writeLeb128(section, static_cast<uint32_t>(payload.size()));
    section.insert(section.end(), payload.begin(), payload.end());
    return section;
}

// Export section: export "main" as function 0
std::vector<uint8_t> WasmCompiler::buildExportSection() {
    std::vector<uint8_t> payload;
    writeLeb128(payload, 1);          // export count
    writeString(payload, "main");     // name
    payload.push_back(0x00);          // kind: func
    writeLeb128(payload, 0);          // func index

    std::vector<uint8_t> section;
    section.push_back(0x07); // Export section
    writeLeb128(section, static_cast<uint32_t>(payload.size()));
    section.insert(section.end(), payload.begin(), payload.end());
    return section;
}

// Code section: one function body for main
std::vector<uint8_t> WasmCompiler::buildCodeSection(std::shared_ptr<ASTNode> mainFunc) {
    std::vector<uint8_t> body;

    // Reset locals
    localVarIndices.clear();
    nextLocalIndex = 0;

    // Local declarations
    auto localsHeader = analyzeLocalVariables(mainFunc);
    body.insert(body.end(), localsHeader.begin(), localsHeader.end());

    // Local variable initialization
    generateLocalVariableInitialization(body, mainFunc);

    // Main logic: assignments, if/while, return
    generateMainLogic(body, mainFunc);

    // Fallback: in case some path reaches end without return, push 0 and return
    generateI32Const(body, 0);
    body.push_back(0x0f); // return

    // End of function body
    body.push_back(0x0b); // end

    // Wrap into code section
    std::vector<uint8_t> payload;
    writeLeb128(payload, 1); // function count

    std::vector<uint8_t> funcEntry;
    writeLeb128(funcEntry, static_cast<uint32_t>(body.size()));
    funcEntry.insert(funcEntry.end(), body.begin(), body.end());

    payload.insert(payload.end(), funcEntry.begin(), funcEntry.end());

    std::vector<uint8_t> section;
    section.push_back(0x0a); // Code section
    writeLeb128(section, static_cast<uint32_t>(payload.size()));
    section.insert(section.end(), payload.begin(), payload.end());
    return section;
}

// ============================================================================
// Locals and initialization
// ============================================================================

std::vector<uint8_t> WasmCompiler::analyzeLocalVariables(std::shared_ptr<ASTNode> mainFunc) {
    std::vector<uint8_t> buf;
    std::vector<std::pair<uint32_t,uint8_t>> locals; // (count, type)

    // Find BODY
    std::shared_ptr<ASTNode> body = nullptr;
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            body = child;
            break;
        }
    }
    if (!body) {
        writeLeb128(buf, 0); // no locals
        return buf;
    }

    // Each VAR_DECL -> one i32 local (integer/boolean)
    for (auto& stmt : body->children) {
        if (!stmt) continue;
        if (stmt->type != ASTNodeType::VAR_DECL) continue;

        const std::string& name = stmt->value;
        if (localVarIndices.find(name) == localVarIndices.end()) {
            localVarIndices[name] = nextLocalIndex++;
            locals.push_back({1, 0x7f}); // i32
        }
    }

    writeLeb128(buf, static_cast<uint32_t>(locals.size()));
    for (auto& p : locals) {
        writeLeb128(buf, p.first); // count
        buf.push_back(p.second);   // type
    }
    return buf;
}

void WasmCompiler::generateLocalVariableInitialization(std::vector<uint8_t>& body,
                                                       std::shared_ptr<ASTNode> mainFunc) {
    // Find BODY
    std::shared_ptr<ASTNode> mainBody = nullptr;
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            mainBody = child;
            break;
        }
    }
    if (!mainBody) return;

    for (auto& stmt : mainBody->children) {
        if (!stmt) continue;
        if (stmt->type != ASTNodeType::VAR_DECL) continue;

        const std::string& varName = stmt->value;
        auto it = localVarIndices.find(varName);
        if (it == localVarIndices.end()) continue;

        // children: [typeNode, initExpr]
        if (stmt->children.size() < 2) continue;
        auto initExpr = stmt->children[1];
        if (!initExpr) continue;

        generateExpression(body, initExpr, mainFunc);
        body.push_back(0x21); // local.set
        writeLeb128(body, static_cast<uint32_t>(it->second));
    }
}

// ============================================================================
// Statements
// ============================================================================

void WasmCompiler::generateMainLogic(std::vector<uint8_t>& body,
                                     std::shared_ptr<ASTNode> mainFunc) {
    std::cout << " 🔧 Generating main logic..." << std::endl;

    // Find BODY
    std::shared_ptr<ASTNode> mainBody = nullptr;
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            mainBody = child;
            break;
        }
    }
    if (!mainBody) {
        std::cout << "⚠️ No BODY in main" << std::endl;
        return;
    }

    for (auto& stmt : mainBody->children) {
        if (!stmt) continue;
        std::cout << "  🔍 Statement node: "
                  << astNodeTypeToString(stmt->type) << std::endl;

        switch (stmt->type) {
            case ASTNodeType::ASSIGNMENT:
                generateAssignment(body, stmt, mainFunc);
                break;
            case ASTNodeType::RETURN_STMT:
                generateReturn(body, stmt, mainFunc);
                break;
            case ASTNodeType::IF_STMT:
                generateIfStatement(body, stmt, mainFunc);
                break;
            case ASTNodeType::WHILE_LOOP:
                generateWhileLoop(body, stmt, mainFunc);
                break;
            case ASTNodeType::VAR_DECL:
                // initialization already done
                break;
            default:
                std::cout << "  ⚠️ Unhandled statement type in WASM backend: "
                          << astNodeTypeToString(stmt->type) << std::endl;
                break;
        }
    }
}

void WasmCompiler::generateIfStatement(std::vector<uint8_t>& body,
                                       std::shared_ptr<ASTNode> ifStmt,
                                       std::shared_ptr<ASTNode> mainFunc) {
    if (!ifStmt || ifStmt->children.size() < 2) {
        std::cout << "⚠️ Malformed IF_STMT node\n";
        return;
    }

    auto cond     = ifStmt->children[0];
    auto thenBody = ifStmt->children[1];
    std::shared_ptr<ASTNode> elseBody =
        (ifStmt->children.size() > 2) ? ifStmt->children[2] : nullptr;

    generateExpression(body, cond, mainFunc); // condition (i32)

    body.push_back(0x04); // if
    body.push_back(0x40); // block type: empty

    // THEN
    if (thenBody && thenBody->type == ASTNodeType::BODY) {
        for (auto& stmt : thenBody->children) {
            if (!stmt) continue;
            switch (stmt->type) {
                case ASTNodeType::ASSIGNMENT:
                    generateAssignment(body, stmt, mainFunc);
                    break;
                case ASTNodeType::RETURN_STMT:
                    generateReturn(body, stmt, mainFunc);
                    break;
                case ASTNodeType::IF_STMT:
                    generateIfStatement(body, stmt, mainFunc);
                    break;
                case ASTNodeType::WHILE_LOOP:
                    generateWhileLoop(body, stmt, mainFunc);
                    break;
                case ASTNodeType::VAR_DECL:
                    break;
                default:
                    std::cout << "  ⚠️ Unhandled THEN stmt: "
                              << astNodeTypeToString(stmt->type) << std::endl;
                    break;
            }
        }
    }

    // ELSE
    if (elseBody) {
        body.push_back(0x05); // else
        if (elseBody->type == ASTNodeType::BODY) {
            for (auto& stmt : elseBody->children) {
                if (!stmt) continue;
                switch (stmt->type) {
                    case ASTNodeType::ASSIGNMENT:
                        generateAssignment(body, stmt, mainFunc);
                        break;
                    case ASTNodeType::RETURN_STMT:
                        generateReturn(body, stmt, mainFunc);
                        break;
                    case ASTNodeType::IF_STMT:
                        generateIfStatement(body, stmt, mainFunc);
                        break;
                    case ASTNodeType::WHILE_LOOP:
                        generateWhileLoop(body, stmt, mainFunc);
                        break;
                    case ASTNodeType::VAR_DECL:
                        break;
                    default:
                        std::cout << "  ⚠️ Unhandled ELSE stmt: "
                                  << astNodeTypeToString(stmt->type) << std::endl;
                        break;
                }
            }
        }
    }

    body.push_back(0x0b); // end
}

void WasmCompiler::generateWhileLoop(std::vector<uint8_t>& body,
                                     std::shared_ptr<ASTNode> whileStmt,
                                     std::shared_ptr<ASTNode> mainFunc) {
    if (!whileStmt || whileStmt->children.size() < 2) {
        std::cout << "⚠️ Malformed WHILE_LOOP node\n";
        return;
    }

    auto cond     = whileStmt->children[0];
    auto loopBody = whileStmt->children[1];

    body.push_back(0x02); // block
    body.push_back(0x40); // void

    body.push_back(0x03); // loop
    body.push_back(0x40); // void

    // condition
    generateExpression(body, cond, mainFunc);
    body.push_back(0x45); // i32.eqz
    body.push_back(0x0d); // br_if
    body.push_back(0x01); // to outer block

    // body
    if (loopBody && loopBody->type == ASTNodeType::BODY) {
        for (auto& stmt : loopBody->children) {
            if (!stmt) continue;
            switch (stmt->type) {
                case ASTNodeType::ASSIGNMENT:
                    generateAssignment(body, stmt, mainFunc);
                    break;
                case ASTNodeType::IF_STMT:
                    generateIfStatement(body, stmt, mainFunc);
                    break;
                case ASTNodeType::WHILE_LOOP:
                    generateWhileLoop(body, stmt, mainFunc);
                    break;
                case ASTNodeType::RETURN_STMT:
                    generateReturn(body, stmt, mainFunc);
                    break;
                case ASTNodeType::VAR_DECL:
                    break;
                default:
                    std::cout << "  ⚠️ Unhandled WHILE body stmt: "
                              << astNodeTypeToString(stmt->type) << std::endl;
                    break;
            }
        }
    }

    body.push_back(0x0c); // br
    body.push_back(0x00); // to innermost loop

    body.push_back(0x0b); // end loop
    body.push_back(0x0b); // end block
}

void WasmCompiler::generateAssignment(std::vector<uint8_t>& body,
                                      std::shared_ptr<ASTNode> assignment,
                                      std::shared_ptr<ASTNode> mainFunc) {
    if (!assignment || assignment->children.size() != 2) {
        std::cout << "⚠️ Malformed ASSIGNMENT node\n";
        return;
    }

    auto lhs = assignment->children[0];
    auto rhs = assignment->children[1];

    if (!lhs || lhs->type != ASTNodeType::IDENTIFIER) {
        std::cout << "⚠️ Only simple identifier assignments supported\n";
        return;
    }

    const std::string& varName = lhs->value;
    auto it = localVarIndices.find(varName);
    if (it == localVarIndices.end()) {
        std::cout << "⚠️ Unknown local variable in assignment: " << varName << std::endl;
        return;
    }

    generateExpression(body, rhs, mainFunc);
    body.push_back(0x21); // local.set
    writeLeb128(body, static_cast<uint32_t>(it->second));
}

void WasmCompiler::generateReturn(std::vector<uint8_t>& body,
                                  std::shared_ptr<ASTNode> returnStmt,
                                  std::shared_ptr<ASTNode> mainFunc) {
    if (!returnStmt) return;

    if (!returnStmt->children.empty()) {
        auto expr = returnStmt->children[0];
        if (expr) generateExpression(body, expr, mainFunc);
    } else {
        generateI32Const(body, 0);
    }

    body.push_back(0x0f); // return
}

// ============================================================================
// Expressions
// ============================================================================

void WasmCompiler::generateI32Const(std::vector<uint8_t>& body, int value) {
    body.push_back(0x41); // i32.const
    writeLeb128(body, static_cast<uint32_t>(value));
}

void WasmCompiler::generateF64Const(std::vector<uint8_t>& body, double value) {
    body.push_back(0x44); // f64.const
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(double));
    for (int i = 0; i < 8; ++i) {
        body.push_back(static_cast<uint8_t>(bits & 0xff));
        bits >>= 8;
    }
}

void WasmCompiler::generateVariableLoad(std::vector<uint8_t>& body,
                                        const std::string& name) {
    auto it = localVarIndices.find(name);
    if (it == localVarIndices.end()) {
        std::cout << "⚠️ Unknown local variable load: " << name
                  << " (using 0 instead)" << std::endl;
        generateI32Const(body, 0);
        return;
    }
    body.push_back(0x20); // local.get
    writeLeb128(body, static_cast<uint32_t>(it->second));
}

void WasmCompiler::generateBinaryOp(std::vector<uint8_t>& body,
                                    std::shared_ptr<ASTNode> binaryOp,
                                    std::shared_ptr<ASTNode> mainFunc) {
    if (!binaryOp || binaryOp->children.size() != 2) {
        std::cout << "⚠️ Invalid BINARY_OP node\n";
        return;
    }

    auto left  = binaryOp->children[0];
    auto right = binaryOp->children[1];

    generateExpression(body, left, mainFunc);
    generateExpression(body, right, mainFunc);

    const std::string& op = binaryOp->value;

    if (op == "+") {
        body.push_back(0x6a); // i32.add
    } else if (op == "-") {
        body.push_back(0x6b); // i32.sub
    } else if (op == "*") {
        body.push_back(0x6c); // i32.mul
    } else if (op == "/") {
        body.push_back(0x6d); // i32.div_s
    } else if (op == "and") {
        body.push_back(0x71); // i32.and
    } else if (op == "or") {
        body.push_back(0x72); // i32.or
    } else if (op == "xor") {
        body.push_back(0x73); // i32.xor
    } else if (op == "<") {
        body.push_back(0x48); // i32.lt_s
    } else if (op == "<=") {
        body.push_back(0x4c); // i32.le_s
    } else if (op == ">") {
        body.push_back(0x4a); // i32.gt_s
    } else if (op == ">=") {
        body.push_back(0x4e); // i32.ge_s
    } else if (op == "=") {
        body.push_back(0x46); // i32.eq
    } else if (op == "/=") {
        body.push_back(0x47); // i32.ne
    } else {
        std::cout << "⚠️ Unhandled binary operator: '" << op << "'\n";
    }
}

void WasmCompiler::generateExpression(std::vector<uint8_t>& body,
                                      std::shared_ptr<ASTNode> expr,
                                      std::shared_ptr<ASTNode> mainFunc) {
    if (!expr) return;

    switch (expr->type) {
        case ASTNodeType::LITERAL_INT: {
            try {
                int v = std::stoi(expr->value);
                generateI32Const(body, v);
            } catch (...) {
                generateI32Const(body, 0);
            }
            break;
        }
        case ASTNodeType::LITERAL_BOOL: {
            if (expr->value == "true") generateI32Const(body, 1);
            else                        generateI32Const(body, 0);
            break;
        }
        case ASTNodeType::LITERAL_REAL: {
            try {
                double d = std::stod(expr->value);
                generateF64Const(body, d);
            } catch (...) {
                generateF64Const(body, 0.0);
            }
            break;
        }
        case ASTNodeType::IDENTIFIER:
            generateVariableLoad(body, expr->value);
            break;
        case ASTNodeType::BINARY_OP:
            generateBinaryOp(body, expr, mainFunc);
            break;
        case ASTNodeType::UNARY_OP: {
            const std::string& op = expr->value;
            if (op == "not" && !expr->children.empty()) {
                generateExpression(body, expr->children[0], mainFunc);
                body.push_back(0x45); // i32.eqz
            } else {
                std::cout << "⚠️ Unhandled unary operator: " << op << std::endl;
                generateI32Const(body, 0);
            }
            break;
        }
        default:
            std::cout << "⚠️ Unhandled expression node type: "
                      << astNodeTypeToString(expr->type) << std::endl;
            generateI32Const(body, 0);
            break;
    }
}
