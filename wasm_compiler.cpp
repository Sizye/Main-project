#include "wasm_compiler.h"

#include <iostream>
#include <cstring>
#include <cmath>

// ========== Debug helper ==========
static const char* tname(ASTNodeType t) {
    switch (t) {
        case ASTNodeType::PROGRAM: return "PROGRAM";
        case ASTNodeType::ROUTINE_DECL: return "ROUTINE_DECL";
        case ASTNodeType::PARAMETER_LIST: return "PARAMETER_LIST";
        case ASTNodeType::PARAMETER: return "PARAMETER";
        case ASTNodeType::PRIMITIVE_TYPE: return "PRIMITIVE_TYPE";
        case ASTNodeType::USER_TYPE: return "USER_TYPE";
        case ASTNodeType::BODY: return "BODY";
        case ASTNodeType::VAR_DECL: return "VAR_DECL";
        case ASTNodeType::ASSIGNMENT: return "ASSIGNMENT";
        case ASTNodeType::IF_STMT: return "IF_STMT";
        case ASTNodeType::WHILE_LOOP: return "WHILE_LOOP";
        case ASTNodeType::FOR_LOOP: return "FOR_LOOP";
        case ASTNodeType::RETURN_STMT: return "RETURN_STMT";
        case ASTNodeType::BINARY_OP: return "BINARY_OP";
        case ASTNodeType::UNARY_OP: return "UNARY_OP";
        case ASTNodeType::LITERAL_INT: return "LITERAL_INT";
        case ASTNodeType::LITERAL_BOOL: return "LITERAL_BOOL";
        case ASTNodeType::LITERAL_REAL: return "LITERAL_REAL";
        case ASTNodeType::IDENTIFIER: return "IDENTIFIER";
        case ASTNodeType::ROUTINE_CALL: return "ROUTINE_CALL";
        case ASTNodeType::ARGUMENT_LIST: return "ARGUMENT_LIST";
        case ASTNodeType::RANGE: return "RANGE";
        default: return "OTHER";
    }
}

// ======================================================================
// Public API
// ======================================================================

bool WasmCompiler::compile(std::shared_ptr<ASTNode> program,
                           const std::string& filename) {
    std::cout << "🚀 COMPILING TO WASM: " << filename << std::endl;

    if (!collectFunctions(program)) {
        std::cerr << "❌ No routines found (need at least main)" << std::endl;
        return false;
    }

    std::vector<uint8_t> mod;

    const uint8_t header[8] = {0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00};
    mod.insert(mod.end(), header, header + 8);

    auto typeSec = buildTypeSection();
    auto funcSec = buildFunctionSection();
    auto memorySec = buildMemorySection();
    auto expSec  = buildExportSection();
    auto codeSec = buildCodeSection();

    mod.insert(mod.end(), typeSec.begin(), typeSec.end());
    mod.insert(mod.end(), funcSec.begin(), funcSec.end());
    mod.insert(mod.end(), memorySec.begin(), memorySec.end());
    mod.insert(mod.end(), expSec.begin(),  expSec.end());
    mod.insert(mod.end(), codeSec.begin(), codeSec.end());

    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::cerr << "❌ Cannot open " << filename << " for writing\n";
        return false;
    }
    out.write(reinterpret_cast<const char*>(mod.data()), mod.size());
    out.close();

    std::cout << "✅ WROTE WASM module (" << mod.size() << " bytes)\n";
    std::cout << "💡 You can run it with: wasmtime --invoke main " << filename << "\n";
    return true;
}

// ======================================================================
// Collect routines and signatures
// ======================================================================

bool WasmCompiler::collectFunctions(std::shared_ptr<ASTNode> program) {
    funcs.clear();
    funcIndexByName.clear();
    globalMemoryOffset = 0;

    collectRecordTypes(program);
    if (!program || program->type != ASTNodeType::PROGRAM) return false;

    for (auto& n : program->children) {
        if (!n) continue;
        if (n->type == ASTNodeType::ROUTINE_DECL) {
            FuncInfo F;
            F.name = n->value;
            F.node = n;
            F.typeIndex = 0;
            F.funcIndex = 0;
            analyzeFunctionSignature(F);
            funcs.push_back(std::move(F));
        }
    }

    if (funcs.empty()) return false;

    for (uint32_t i = 0; i < funcs.size(); ++i) {
        funcs[i].typeIndex = i;
        funcs[i].funcIndex = i;
        funcIndexByName[funcs[i].name] = i;
    }

    if (!funcIndexByName.count("main")) {
        std::cerr << "❌ main routine not found\n";
        return false;
    }

    std::cout << "✅ Collected " << funcs.size() << " routines\n";
    return true;
}

void WasmCompiler::analyzeFunctionSignature(FuncInfo& F) {
    F.paramTypes.clear();
    F.resultTypes.clear();

    std::shared_ptr<ASTNode> params = nullptr;
    std::shared_ptr<ASTNode> retType = nullptr;

    for (auto& ch : F.node->children) {
        if (!ch) continue;
        if (ch->type == ASTNodeType::PARAMETER_LIST) params = ch;
        else if (ch->type == ASTNodeType::PRIMITIVE_TYPE ||
                 ch->type == ASTNodeType::USER_TYPE) retType = ch;
    }

    if (params) {
        for (auto& p : params->children) {
            if (!p || p->type != ASTNodeType::PARAMETER) continue;
            uint8_t wt = 0x7f;
            for (auto& pc : p->children) {
                if (!pc) continue;
                if (pc->type == ASTNodeType::PRIMITIVE_TYPE) {
                    wt = mapPrimitiveToWasm(pc->value);
                } else if (pc->type == ASTNodeType::USER_TYPE) {
                    wt = 0x7f;
                }
            }
            F.paramTypes.push_back(wt);
        }
    }

    if (retType) {
        F.resultTypes.push_back(mapPrimitiveToWasm(retType->value));
    } else {
        F.resultTypes.push_back(0x7f);
    }
}

uint8_t WasmCompiler::mapPrimitiveToWasm(const std::string& tname) {
    if (tname == "integer" || tname == "boolean") return 0x7f;
    if (tname == "real") return 0x7c;
    return 0x7f;
}

// ======================================================================
// Encoders
// ======================================================================

void WasmCompiler::writeLeb128(std::vector<uint8_t>& buf, uint32_t v) {
    do {
        uint8_t b = v & 0x7f;
        v >>= 7;
        if (v) b |= 0x80;
        buf.push_back(b);
    } while (v);
}

void WasmCompiler::writeString(std::vector<uint8_t>& buf, const std::string& s) {
    writeLeb128(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

// ======================================================================
// Sections
// ======================================================================

std::vector<uint8_t> WasmCompiler::buildTypeSection() {
    std::vector<uint8_t> payload;

    writeLeb128(payload, static_cast<uint32_t>(funcs.size()));
    for (auto& F : funcs) {
        payload.push_back(0x60);
        writeLeb128(payload, static_cast<uint32_t>(F.paramTypes.size()));
        for (auto t : F.paramTypes) payload.push_back(t);
        writeLeb128(payload, static_cast<uint32_t>(F.resultTypes.size()));
        for (auto t : F.resultTypes) payload.push_back(t);
    }

    std::vector<uint8_t> sec;
    sec.push_back(0x01);
    writeLeb128(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
    return sec;
}

std::vector<uint8_t> WasmCompiler::buildFunctionSection() {
    std::vector<uint8_t> payload;
    writeLeb128(payload, static_cast<uint32_t>(funcs.size()));
    for (auto& F : funcs) {
        writeLeb128(payload, F.typeIndex);
    }

    std::vector<uint8_t> sec;
    sec.push_back(0x03);
    writeLeb128(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
    return sec;
}

std::vector<uint8_t> WasmCompiler::buildExportSection() {
    std::vector<uint8_t> payload;

    writeLeb128(payload, 1);
    writeString(payload, "main");
    payload.push_back(0x00);
    writeLeb128(payload, funcIndexByName["main"]);

    std::vector<uint8_t> sec;
    sec.push_back(0x07);
    writeLeb128(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
    return sec;
}

std::vector<uint8_t> WasmCompiler::buildCodeSection() {
    std::vector<uint8_t> payload;

    writeLeb128(payload, static_cast<uint32_t>(funcs.size()));

    for (auto& F : funcs) {
        std::vector<uint8_t> body;

        resetLocals();  // This now also clears array info
        addParametersToLocals(F);

        auto localsHeader = analyzeLocalVariables(F);
        body.insert(body.end(), localsHeader.begin(), localsHeader.end());

        generateLocalInitializers(body, F);

        generateFunctionBody(body, F);

        if (!F.resultTypes.empty()) {
            if (F.resultTypes[0] == 0x7f) emitI32Const(body, 0);
            else                          emitF64Const(body, 0.0);
            body.push_back(0x0f);
        }

        body.push_back(0x0b);

        std::vector<uint8_t> entry;
        writeLeb128(entry, static_cast<uint32_t>(body.size()));
        entry.insert(entry.end(), body.begin(), body.end());

        payload.insert(payload.end(), entry.begin(), entry.end());
    }

    std::vector<uint8_t> sec;
    sec.push_back(0x0a);
    writeLeb128(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
    return sec;
}

// ======================================================================
// Per-function codegen utilities
// ======================================================================

void WasmCompiler::resetLocals() {
    localVarIndices.clear();
    arrayInfos.clear();  // Clear array info for each function
    recordVariables.clear();
    nextLocalIndex = 0;
}

void WasmCompiler::addParametersToLocals(const FuncInfo& F) {
    std::shared_ptr<ASTNode> params = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::PARAMETER_LIST) { params = ch; break; }
    }
    if (!params) return;
    int idx = 0;
    for (auto& p : params->children) {
        if (!p || p->type != ASTNodeType::PARAMETER) continue;
        localVarIndices[p->value] = idx++;
    }
    nextLocalIndex = idx;
}

std::vector<uint8_t> WasmCompiler::analyzeLocalVariables(const FuncInfo& F) {
    std::vector<uint8_t> buf;
    std::vector<std::pair<uint32_t,uint8_t>> locals;

    std::shared_ptr<ASTNode> bodyNode = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
    }
    if (!bodyNode) {
        writeLeb128(buf, 0);
        return buf;
    }

    for (auto& s : bodyNode->children) {
        if (!s || s->type != ASTNodeType::VAR_DECL) continue;
        const std::string& name = s->value;
        if (localVarIndices.count(name)) continue;
        
        // Check if it's an array & record declaration
        if (s->children.size() >= 1 && s->children[0]) {
            auto typeNode = s->children[0];
            if (typeNode->type == ASTNodeType::USER_TYPE) {
                auto it = recordTypes.find(typeNode->value);
                if (it != recordTypes.end()) {
                    // It's a record type!
                    RecordVarInfo recVar;
                    recVar.recordType = typeNode->value;
                    recVar.size = it->second.totalSize;
                    recVar.baseOffset = globalMemoryOffset;
                    
                    recordVariables[name] = recVar;
                    globalMemoryOffset += recVar.size;
                    
                    // Store base address in local variable
                    localVarIndices[name] = nextLocalIndex++;
                    locals.push_back({1, 0x7f}); // base address as i32
                    
                    continue;
                    }
                }
            if (typeNode->type == ASTNodeType::ARRAY_TYPE) {
                auto [elemType, elemTypeName, size] = analyzeArrayType(typeNode);
                
                // For arrays, we store the base address as a local variable
                localVarIndices[name] = nextLocalIndex++;
                
                // Register array info for later access
                ArrayInfo arrInfo;
                arrInfo.elemType = elemType;
                arrInfo.elemTypeName = elemTypeName;
                arrInfo.size = size;
                arrInfo.baseOffset = globalMemoryOffset;
                arrayInfos[name] = arrInfo;
                
                // Calculate element size based on type
                int elemSize = 4; // default for i32
                if (elemType == 0x7c) {
                    elemSize = 8; // f64
                } else if (recordTypes.find(elemTypeName) != recordTypes.end()) {
                    // It's a record type - use record size
                    elemSize = recordTypes[elemTypeName].totalSize;
                }
                
                globalMemoryOffset += size * elemSize;
                
                // Add the local variable to track the array base address
                locals.push_back({1, 0x7f}); // base address is stored as i32 offset
            } else {
                // Regular variable
                localVarIndices[name] = nextLocalIndex++;
                uint8_t wt = 0x7f;
                if (typeNode->type == ASTNodeType::PRIMITIVE_TYPE) {
                    wt = mapPrimitiveToWasm(typeNode->value);
                } else if (typeNode->type == ASTNodeType::USER_TYPE) {
                    wt = 0x7f; // default to i32
                }
                locals.push_back({1, wt});
            }
        } else {
            // No explicit type, default to i32
            localVarIndices[name] = nextLocalIndex++;
            locals.push_back({1, 0x7f});
        }
    }

    writeLeb128(buf, static_cast<uint32_t>(locals.size()));
    for (auto& p : locals) {
        writeLeb128(buf, p.first);
        buf.push_back(p.second);
    }
    return buf;
}

void WasmCompiler::generateLocalInitializers(std::vector<uint8_t>& body, const FuncInfo& F) {
    std::shared_ptr<ASTNode> bodyNode = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
    }
    if (!bodyNode) return;

    for (auto& s : bodyNode->children) {
        if (!s || s->type != ASTNodeType::VAR_DECL) continue;
        const std::string& name = s->value;
        auto it = localVarIndices.find(name);
        if (it == localVarIndices.end()) continue;

        if (s->children.size() >= 2 && s->children[1]) {
            generateExpression(body, s->children[1], F);
            body.push_back(0x21);
            writeLeb128(body, static_cast<uint32_t>(it->second));
        }
    }
}

void WasmCompiler::generateFunctionBody(std::vector<uint8_t>& body, const FuncInfo& F) {
    std::shared_ptr<ASTNode> bodyNode = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
    }
    if (!bodyNode) return;

    for (auto& s : bodyNode->children) {
        if (!s) continue;
        switch (s->type) {
            case ASTNodeType::VAR_DECL:
                break;
            case ASTNodeType::ASSIGNMENT:
                generateAssignment(body, s, F);
                break;
            case ASTNodeType::IF_STMT:
                generateIfStatement(body, s, F);
                break;
            case ASTNodeType::WHILE_LOOP:
                generateWhileLoop(body, s, F);
                break;
            case ASTNodeType::FOR_LOOP:
                generateForLoop(body, s, F);
                break;
            case ASTNodeType::RETURN_STMT:
                generateReturn(body, s, F);
                break;
            default:
                std::cout << "  ⚠️ Unhandled stmt in "
                          << F.name << ": " << tname(s->type) << "\n";
                break;
        }
    }
}

// ======================================================================
// Statements
// ======================================================================

void WasmCompiler::generateAssignment(std::vector<uint8_t>& body,
                                      std::shared_ptr<ASTNode> a,
                                      const FuncInfo& F) {
    (void)F;
    if (!a || a->children.size() != 2) return;
    auto lhs = a->children[0];
    auto rhs = a->children[1];
    if (!lhs || !rhs) return;
    
    if (lhs->type == ASTNodeType::IDENTIFIER) {
        // Simple variable assignment
        generateExpression(body, rhs, F);
        emitLocalSet(body, lhs->value);
    } else if (lhs->type == ASTNodeType::ARRAY_ACCESS) {
        // Array element assignment
        generateArrayAssignment(body, lhs, rhs, F);
    } else if (lhs->type == ASTNodeType::MEMBER_ACCESS) {
        // Record field assignment
        generateMemberAssignment(body, lhs, rhs, F);
    } else {
        std::cout << "⚠️ Only simple identifier, array, and member assignments supported\n";
        generateExpression(body, rhs, F);
    }
}

void WasmCompiler::generateArrayAssignment(std::vector<uint8_t>& body,
                                           std::shared_ptr<ASTNode> arrayAccess,
                                           std::shared_ptr<ASTNode> rhs,
                                           const FuncInfo& F) {
    if (!arrayAccess || arrayAccess->children.size() != 2) {
        std::cout << "⚠️ Malformed array assignment\n";
        generateExpression(body, rhs, F);
        return;
    }
    
    auto arrayRef = arrayAccess->children[0];  // identifier OR member access
    auto indexExpr = arrayAccess->children[1]; // index expression
    
    if (!arrayRef || !indexExpr) return;
    
    std::string arrayName;
    ArrayInfo arrayInfo;
    
    if (arrayRef->type == ASTNodeType::IDENTIFIER) {
        // Simple array variable
        arrayName = arrayRef->value;
        auto it = arrayInfos.find(arrayName);
        if (it == arrayInfos.end()) {
            std::cout << "⚠️ Unknown array: " << arrayName << "\n";
            generateExpression(body, rhs, F);
            return;
        }
        arrayInfo = it->second;
        
        // Get the base address of the array
        emitLocalGet(body, arrayName);
        
    } else if (arrayRef->type == ASTNodeType::MEMBER_ACCESS) {
        // Array field inside a record
        auto [baseAddr, elemType, size] = resolveArrayMember(body, arrayRef, F);
        if (baseAddr == -1) {
            generateExpression(body, rhs, F);
            return;
        }
        
        // The base address is already on the stack from resolveArrayMember
        arrayInfo.elemType = elemType;
        arrayInfo.size = size;
        
    } else {
        std::cout << "⚠️ Array assignment on unsupported node type: " << tname(arrayRef->type) << "\n";
        generateExpression(body, rhs, F);
        return;
    }
    
    // Calculate the index * element_size
    generateExpression(body, indexExpr, F);
    
    // Multiply index by element size (4 for i32, 8 for f64)
    int elemSize = (arrayInfo.elemType == 0x7c) ? 8 : 4;
    if (elemSize != 1) {
        emitI32Const(body, elemSize);
        body.push_back(0x6c); // i32.mul
    }
    
    // Add to base address
    body.push_back(0x6a); // i32.add
    
    // Generate the value to store
    generateExpression(body, rhs, F);
    
    // Store the value to memory
    if (arrayInfo.elemType == 0x7c) {
        // f64 store
        body.push_back(0x39); // f64.store
        body.push_back(0x00); // align
        body.push_back(0x00); // offset
    } else {
        // i32 store
        body.push_back(0x36); // i32.store
        body.push_back(0x00); // align
        body.push_back(0x00); // offset
    }
}

void WasmCompiler::generateIfStatement(std::vector<uint8_t>& body,
                                       std::shared_ptr<ASTNode> ifs,
                                       const FuncInfo& F) {
    if (!ifs || ifs->children.size() < 2) return;
    auto cond = ifs->children[0];
    auto thenB = ifs->children[1];
    std::shared_ptr<ASTNode> elseB =
        (ifs->children.size() > 2) ? ifs->children[2] : nullptr;

    generateExpression(body, cond, F);
    body.push_back(0x04);
    body.push_back(0x40);
    if (thenB && thenB->type == ASTNodeType::BODY) {
        for (auto& s : thenB->children) {
            if (!s) continue;
            switch (s->type) {
                case ASTNodeType::ASSIGNMENT: generateAssignment(body, s, F); break;
                case ASTNodeType::IF_STMT:     generateIfStatement(body, s, F); break;
                case ASTNodeType::WHILE_LOOP:  generateWhileLoop(body, s, F); break;
                case ASTNodeType::FOR_LOOP:    generateForLoop(body, s, F); break;
                case ASTNodeType::RETURN_STMT: generateReturn(body, s, F); break;
                case ASTNodeType::VAR_DECL:    break;
                default:
                    std::cout << "  ⚠️ Unhandled THEN stmt: " << tname(s->type) << "\n";
                    break;
            }
        }
    }
    if (elseB) {
        body.push_back(0x05);
        if (elseB->type == ASTNodeType::BODY) {
            for (auto& s : elseB->children) {
                if (!s) continue;
                switch (s->type) {
                    case ASTNodeType::ASSIGNMENT: generateAssignment(body, s, F); break;
                    case ASTNodeType::IF_STMT:     generateIfStatement(body, s, F); break;
                    case ASTNodeType::WHILE_LOOP:  generateWhileLoop(body, s, F); break;
                    case ASTNodeType::FOR_LOOP:    generateForLoop(body, s, F); break;
                    case ASTNodeType::RETURN_STMT: generateReturn(body, s, F); break;
                    case ASTNodeType::VAR_DECL:    break;
                    default:
                        std::cout << "  ⚠️ Unhandled ELSE stmt: " << tname(s->type) << "\n";
                        break;
                }
            }
        }
    }
    body.push_back(0x0b);
}

void WasmCompiler::generateWhileLoop(std::vector<uint8_t>& body,
                                     std::shared_ptr<ASTNode> w,
                                     const FuncInfo& F) {
    if (!w || w->children.size() < 2) return;
    auto cond = w->children[0];
    auto loopB = w->children[1];

    body.push_back(0x02);
    body.push_back(0x40);
    body.push_back(0x03);
    body.push_back(0x40);

    generateExpression(body, cond, F);
    body.push_back(0x45);
    body.push_back(0x0d);
    body.push_back(0x01);

    if (loopB && loopB->type == ASTNodeType::BODY) {
        for (auto& s : loopB->children) {
            if (!s) continue;
            switch (s->type) {
                case ASTNodeType::ASSIGNMENT: generateAssignment(body, s, F); break;
                case ASTNodeType::IF_STMT:     generateIfStatement(body, s, F); break;
                case ASTNodeType::WHILE_LOOP:  generateWhileLoop(body, s, F); break;
                case ASTNodeType::FOR_LOOP:    generateForLoop(body, s, F); break;
                case ASTNodeType::RETURN_STMT: generateReturn(body, s, F); break;
                case ASTNodeType::VAR_DECL:    break;
                default:
                    std::cout << "  ⚠️ Unhandled WHILE stmt: " << tname(s->type) << "\n";
                    break;
            }
        }
    }

    body.push_back(0x0c);
    body.push_back(0x00);
    body.push_back(0x0b);
    body.push_back(0x0b);
}

void WasmCompiler::generateForLoop(std::vector<uint8_t>& body,
                                   std::shared_ptr<ASTNode> forNode,
                                   const FuncInfo& F) {
    if (!forNode) {
        std::cout << "⚠️ Malformed FOR_LOOP node\n";
        return;
    }

    // In your AST: FOR_LOOP.value holds the loop var name, children contain RANGE, BODY, and optional IDENTIFIER(\"reverse\")
    const std::string iv = forNode->value;

    // Find needed children regardless of order
    std::shared_ptr<ASTNode> rangeNode = nullptr;
    std::shared_ptr<ASTNode> loopBody  = nullptr;
    bool isReverse = false;

    for (auto& ch : forNode->children) {
        if (!ch) continue;
        if (ch->type == ASTNodeType::RANGE) {
            rangeNode = ch;
        } else if (ch->type == ASTNodeType::BODY) {
            loopBody = ch;
        } else if (ch->type == ASTNodeType::IDENTIFIER && ch->value == "reverse") {
            isReverse = true;
        }
    }

    if (!rangeNode || !loopBody) {
        std::cout << "⚠️ Malformed FOR_LOOP node (missing RANGE or BODY)\n";
        return;
    }

    // Resolve loop var
    auto it = localVarIndices.find(iv);
    if (it == localVarIndices.end()) {
        std::cout << "⚠️ Loop variable not declared as local: " << iv << "\n";
        return;
    }
    uint32_t ivIdx = static_cast<uint32_t>(it->second);

    // Extract start..end
    std::shared_ptr<ASTNode> startExpr = nullptr, endExpr = nullptr;
    if (rangeNode->children.size() >= 2) {
        startExpr = rangeNode->children[0];
        endExpr   = rangeNode->children[1];
    }
    if (!endExpr) {
        std::cout << "⚠️ FOR_LOOP missing range end\n";
        return;
    }

    // i := start (default 0)
    if (!startExpr) {
        emitI32Const(body, 0);
    } else {
        generateExpression(body, startExpr, F);
    }
    body.push_back(0x21);
    writeLeb128(body, ivIdx);

    // block/loop
    body.push_back(0x02);
    body.push_back(0x40);
    body.push_back(0x03);
    body.push_back(0x40);

    // Cond: forward break if i > end, reverse break if i < end
    body.push_back(0x20);
    writeLeb128(body, ivIdx);
    generateExpression(body, endExpr, F);
    body.push_back(isReverse ? 0x48 : 0x4a);
    body.push_back(0x0d);
    body.push_back(0x01);

    // Body
    for (auto& s : loopBody->children) {
        if (!s) continue;
        switch (s->type) {
            case ASTNodeType::ASSIGNMENT: generateAssignment(body, s, F); break;
            case ASTNodeType::IF_STMT:     generateIfStatement(body, s, F); break;
            case ASTNodeType::WHILE_LOOP:  generateWhileLoop(body, s, F); break;
            case ASTNodeType::FOR_LOOP:    generateForLoop(body, s, F); break;
            case ASTNodeType::RETURN_STMT: generateReturn(body, s, F); break;
            case ASTNodeType::VAR_DECL:    break;
            default:
                std::cout << "  ⚠️ Unhandled FOR body stmt: " << tname(s->type) << "\n";
                break;
        }
    }

    // Step i := i ± 1
    body.push_back(0x20);
    writeLeb128(body, ivIdx);
    body.push_back(0x41);
    writeLeb128(body, 1);
    body.push_back(isReverse ? 0x6b : 0x6a);
    body.push_back(0x21);
    writeLeb128(body, ivIdx);

    // backedge and close
    body.push_back(0x0c);
    body.push_back(0x00);
    body.push_back(0x0b);
    body.push_back(0x0b);
}

void WasmCompiler::generateReturn(std::vector<uint8_t>& body,
                                  std::shared_ptr<ASTNode> r,
                                  const FuncInfo& F) {
    if (!r) return;
    if (!F.resultTypes.empty()) {
        if (!r->children.empty() && r->children[0]) {
            generateExpression(body, r->children[0], F);
        } else {
            if (F.resultTypes[0] == 0x7f) emitI32Const(body, 0);
            else                          emitF64Const(body, 0.0);
        }
    }
    body.push_back(0x0f);
}

// ======================================================================
// Expressions
// ======================================================================

void WasmCompiler::generateExpression(std::vector<uint8_t>& body,
                                      std::shared_ptr<ASTNode> e,
                                      const FuncInfo& F) {
    if (!e) return;
    switch (e->type) {
        case ASTNodeType::LITERAL_INT: {
            try { emitI32Const(body, std::stoi(e->value)); }
            catch (...) { emitI32Const(body, 0); }
            break;
        }
        case ASTNodeType::LITERAL_BOOL:
            emitI32Const(body, e->value == "true" ? 1 : 0);
            break;
        case ASTNodeType::LITERAL_REAL: {
            try { emitF64Const(body, std::stod(e->value)); }
            catch (...) { emitF64Const(body, 0.0); }
            break;
        }
        case ASTNodeType::IDENTIFIER:
            emitLocalGet(body, e->value);
            break;
        case ASTNodeType::ARRAY_ACCESS:
            generateArrayAccess(body, e, F);
            break;
        case ASTNodeType::MEMBER_ACCESS:
            generateMemberAccess(body, e, F);
            break;
        case ASTNodeType::BINARY_OP:
            generateBinaryOp(body, e, F);
            break;
        case ASTNodeType::UNARY_OP: {
            const std::string& op = e->value;
            if (op == "not" && !e->children.empty()) {
                generateExpression(body, e->children[0], F);
                body.push_back(0x45);
            } else {
                emitI32Const(body, 0);
            }
            break;
        }
        case ASTNodeType::ROUTINE_CALL:
            generateCall(body, e, F);
            break;
        case ASTNodeType::PRINT_STMT:
            // For now, just ignore print statements
            std::cout << "  📝 PRINT_STMT ignored in WASM output\n";
            break;
        default:
            std::cout << "  ⚠️ Unhandled expr: " << tname(e->type) << "\n";
            emitI32Const(body, 0);
            break;
    }
}

void WasmCompiler::generateBinaryOp(std::vector<uint8_t>& body,
                                    std::shared_ptr<ASTNode> bin,
                                    const FuncInfo& F) {
    (void)F;
    if (!bin || bin->children.size() != 2) { emitI32Const(body, 0); return; }
    auto L = bin->children[0];
    auto R = bin->children[1];
    generateExpression(body, L, F);
    generateExpression(body, R, F);

    const std::string& op = bin->value;
    if      (op == "+")  body.push_back(0x6a);
    else if (op == "-")  body.push_back(0x6b);
    else if (op == "*")  body.push_back(0x6c);
    else if (op == "/")  body.push_back(0x6d);
    else if (op == "%")  body.push_back(0x6f);
    else if (op == "and") body.push_back(0x71);
    else if (op == "or")  body.push_back(0x72);
    else if (op == "xor") body.push_back(0x73);
    else if (op == "<")   body.push_back(0x48);
    else if (op == "<=")  body.push_back(0x4c);
    else if (op == ">")   body.push_back(0x4a);
    else if (op == ">=")  body.push_back(0x4e);
    else if (op == "=")   body.push_back(0x46);
    else if (op == "/=")  body.push_back(0x47);
    else {
        std::cout << "  ⚠️ Unhandled binop: " << op << "\n";
    }
}

void WasmCompiler::generateCall(std::vector<uint8_t>& body,
                                std::shared_ptr<ASTNode> call,
                                const FuncInfo& F) {
    (void)F;
    std::vector<std::shared_ptr<ASTNode>> args;

    for (auto& ch : call->children) {
        if (!ch) continue;
        if (ch->type == ASTNodeType::ARGUMENT_LIST) {
            for (auto& a : ch->children) if (a) args.push_back(a);
        } else {
            args.push_back(ch);
        }
    }

    for (auto& a : args) generateExpression(body, a, F);

    auto it = funcIndexByName.find(call->value);
    if (it == funcIndexByName.end()) {
        std::cout << "  ⚠️ Unknown callee: " << call->value << " (push 0)\n";
        emitI32Const(body, 0);
        return;
    }
    body.push_back(0x10);
    writeLeb128(body, it->second);
}

// ======================================================================
// Emit helpers
// ======================================================================

void WasmCompiler::emitI32Const(std::vector<uint8_t>& body, int v) {
    body.push_back(0x41);
    writeLeb128(body, static_cast<uint32_t>(v));
}

void WasmCompiler::emitF64Const(std::vector<uint8_t>& body, double d) {
    body.push_back(0x44);
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(double));
    for (int i = 0; i < 8; ++i) {
        body.push_back(static_cast<uint8_t>(bits & 0xff));
        bits >>= 8;
    }
}

void WasmCompiler::emitLocalGet(std::vector<uint8_t>& body, const std::string& name) {
    auto it = localVarIndices.find(name);
    if (it == localVarIndices.end()) {
        std::cout << "  ⚠️ Unknown local get: " << name << " (use 0)\n";
        emitI32Const(body, 0);
        return;
    }
    body.push_back(0x20);
    writeLeb128(body, static_cast<uint32_t>(it->second));
}

void WasmCompiler::emitLocalSet(std::vector<uint8_t>& body, const std::string& name) {
    auto it = localVarIndices.find(name);
    if (it == localVarIndices.end()) {
        std::cout << "  ⚠️ Unknown local set: " << name << "\n";
        return;
    }
    body.push_back(0x21);
    writeLeb128(body, static_cast<uint32_t>(it->second));
}


// ======================================================================
// Array and Type Analysis
// ======================================================================

std::tuple<uint8_t, std::string, int> WasmCompiler::analyzeArrayType(std::shared_ptr<ASTNode> arrayTypeNode) {
    if (!arrayTypeNode || arrayTypeNode->type != ASTNodeType::ARRAY_TYPE) {
        return {0x7f, "integer", 0};  // default to i32
    }
    
    uint8_t elemType = 0x7f;
    std::string typeName = "integer";
    int size = 0;
    
    for (auto& child : arrayTypeNode->children) {
        if (!child) continue;
        
        if (child->type == ASTNodeType::LITERAL_INT) {
            try {
                size = std::stoi(child->value);
            } catch (...) {
                size = 0;
            }
        } else if (child->type == ASTNodeType::PRIMITIVE_TYPE) {
            typeName = child->value;
            elemType = mapPrimitiveToWasm(typeName);
        } else if (child->type == ASTNodeType::USER_TYPE) {
            typeName = child->value;
            elemType = 0x7f;  // Records are stored as i32 base addresses
        }
    }
    
    return {elemType, typeName, size};
}

uint8_t WasmCompiler::getArrayType(std::shared_ptr<ASTNode> arrayTypeNode) {
    auto [elemType, elemTypeName, _] = analyzeArrayType(arrayTypeNode);
    return elemType;
}


// ======================================================================
// Array and Member Access Generation
// ======================================================================

void WasmCompiler::generateArrayAccessForRecord(std::vector<uint8_t>& body,
                                                std::shared_ptr<ASTNode> arrayAccess,
                                                const FuncInfo& F) {
    if (!arrayAccess || arrayAccess->children.size() != 2) {
        std::cout << "⚠️ Malformed array access for record\n";
        emitI32Const(body, 0);
        return;
    }
    
    auto arrayRef = arrayAccess->children[0];
    auto indexExpr = arrayAccess->children[1];
    
    if (!arrayRef || !indexExpr || arrayRef->type != ASTNodeType::IDENTIFIER) {
        emitI32Const(body, 0);
        return;
    }
    
    std::string arrayName = arrayRef->value;
    auto it = arrayInfos.find(arrayName);
    if (it == arrayInfos.end()) {
        std::cout << "⚠️ Unknown array: " << arrayName << "\n";
        emitI32Const(body, 0);
        return;
    }
    
    // Get base address
    emitLocalGet(body, arrayName);
    
    // Calculate index * element_size
    generateExpression(body, indexExpr, F);
    
    // Use proper element size based on the actual type
    int elemSize = 4; // default
    if (it->second.elemType == 0x7c) {
        elemSize = 8; // f64
    } else if (recordTypes.find(it->second.elemTypeName) != recordTypes.end()) {
        // It's a record type - use record size
        elemSize = recordTypes[it->second.elemTypeName].totalSize;
    }
    
    emitI32Const(body, elemSize);
    body.push_back(0x6c); // i32.mul
    
    // Add to base address
    body.push_back(0x6a); // i32.add
    
    // Result: address of array[index] is now on stack
}

std::tuple<int, uint8_t, int> WasmCompiler::resolveArrayMember(std::vector<uint8_t>& body,
                                                               std::shared_ptr<ASTNode> memberAccess,
                                                               const FuncInfo& F) {
    if (!memberAccess || memberAccess->children.size() < 1) {
        return {-1, 0x7f, 0};
    }
    
    auto base = memberAccess->children[0];
    std::string fieldName = memberAccess->value;
    
    if (!base || base->type != ASTNodeType::IDENTIFIER) {
        std::cout << "⚠️ Nested member access not supported\n";
        return {-1, 0x7f, 0};
    }
    
    std::string recordName = base->value;
    auto recordIt = recordVariables.find(recordName);
    if (recordIt == recordVariables.end()) {
        std::cout << "⚠️ Unknown record variable: " << recordName << "\n";
        return {-1, 0x7f, 0};
    }
    
    // Find field info
    auto recordTypeIt = recordTypes.find(recordIt->second.recordType);
    if (recordTypeIt == recordTypes.end()) {
        std::cout << "⚠️ Unknown record type: " << recordIt->second.recordType << "\n";
        return {-1, 0x7f, 0};
    }
    
    int fieldOffset = -1;
    uint8_t fieldType = 0x7f;
    int fieldSize = 0;
    
    for (const auto& field : recordTypeIt->second.fields) {
        if (field.first == fieldName) {
            fieldOffset = field.second.second;
            fieldType = field.second.first;
            
            // Calculate field size based on type
            if (fieldType == 0x7c) fieldSize = 8;  // f64
            else fieldSize = 4;                    // i32
            break;
        }
    }
    
    if (fieldOffset == -1) {
        std::cout << "⚠️ Unknown field '" << fieldName << "' in record '" 
                  << recordIt->second.recordType << "'\n";
        return {-1, 0x7f, 0};
    }
    
    // Calculate address: record_base + field_offset
    emitLocalGet(body, recordName);     // Base address
    emitI32Const(body, fieldOffset);    // Field offset
    body.push_back(0x6a);               // i32.add
    
    // Return the calculated address, element type, and size
    return {0, fieldType, fieldSize};
}

void WasmCompiler::generateMemberAccess(std::vector<uint8_t>& body,
                                        std::shared_ptr<ASTNode> memberAccess,
                                        const FuncInfo& F) {
    if (!memberAccess || memberAccess->children.size() < 1) {
        std::cout << "⚠️ Malformed member access\n";
        emitI32Const(body, 0);
        return;
    }
    
    auto base = memberAccess->children[0];
    std::string fieldName = memberAccess->value;
    
    if (!base || base->type != ASTNodeType::IDENTIFIER) {
        std::cout << "⚠️ Member access on non-identifier\n";
        emitI32Const(body, 0);
        return;
    }
    
    std::string recordName = base->value;
    auto recordIt = recordVariables.find(recordName);
    if (recordIt == recordVariables.end()) {
        std::cout << "⚠️ Unknown record variable: " << recordName << "\n";
        emitI32Const(body, 0);
        return;
    }
    
    // Find field offset
    auto recordTypeIt = recordTypes.find(recordIt->second.recordType);
    if (recordTypeIt == recordTypes.end()) {
        std::cout << "⚠️ Unknown record type: " << recordIt->second.recordType << "\n";
        emitI32Const(body, 0);
        return;
    }
    
    int fieldOffset = -1;
    uint8_t fieldType = 0x7f;
    
    for (const auto& field : recordTypeIt->second.fields) {
        if (field.first == fieldName) {
            fieldOffset = field.second.second;
            fieldType = field.second.first;
            break;
        }
    }
    
    if (fieldOffset == -1) {
        std::cout << "⚠️ Unknown field '" << fieldName << "' in record '" 
                  << recordIt->second.recordType << "'\n";
        emitI32Const(body, 0);
        return;
    }
    
    // Calculate address: record_base + field_offset
    emitLocalGet(body, recordName);     // Base address
    emitI32Const(body, fieldOffset);    // Field offset
    body.push_back(0x6a);               // i32.add
    
    // Load the field value
    if (fieldType == 0x7c) {
        body.push_back(0x2c); // f64.load
        body.push_back(0x03); // 8-byte align
    } else {
        body.push_back(0x28); // i32.load  
        body.push_back(0x02); // 4-byte align
    }
    body.push_back(0x00); // offset 0 (already included in calculation)
}

// ======================================================================
// Memory Operation Helpers
// ======================================================================

void WasmCompiler::emitI32Load(std::vector<uint8_t>& body, uint32_t offset) {
    body.push_back(0x28); // i32.load
    body.push_back(0x02); // align (2^2 = 4-byte alignment)
    writeLeb128(body, offset);
}

void WasmCompiler::emitI32Store(std::vector<uint8_t>& body, uint32_t offset) {
    body.push_back(0x36); // i32.store
    body.push_back(0x02); // align (2^2 = 4-byte alignment)
    writeLeb128(body, offset);
}

void WasmCompiler::emitF64Load(std::vector<uint8_t>& body, uint32_t offset) {
    body.push_back(0x2c); // f64.load
    body.push_back(0x03); // align (2^3 = 8-byte alignment)
    writeLeb128(body, offset);
}

void WasmCompiler::emitF64Store(std::vector<uint8_t>& body, uint32_t offset) {
    body.push_back(0x39); // f64.store
    body.push_back(0x03); // align (2^3 = 8-byte alignment)
    writeLeb128(body, offset);
}

std::vector<uint8_t> WasmCompiler::buildMemorySection() {
    std::vector<uint8_t> payload;
    
    // Use globalMemoryOffset for total memory calculation
    int totalMemoryNeeded = globalMemoryOffset;
    int totalMemoryPages = (totalMemoryNeeded + 65535) / 65536;
    
    if (totalMemoryPages == 0) totalMemoryPages = 1;
    if (totalMemoryPages > 1024) totalMemoryPages = 1024; // 64MB max
    
    std::cout << "📊 Total memory needed: " << totalMemoryNeeded 
              << " bytes (" << totalMemoryPages << " pages)" << std::endl;
    
    writeLeb128(payload, 1);
    payload.push_back(0x00);
    writeLeb128(payload, static_cast<uint32_t>(totalMemoryPages));
    
    std::vector<uint8_t> sec;
    sec.push_back(0x05);
    writeLeb128(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
    return sec;
}

// ======================================================================
// Records
// ======================================================================

void WasmCompiler::collectRecordTypes(std::shared_ptr<ASTNode> program) {
    for (auto& n : program->children) {
        if (!n || n->type != ASTNodeType::TYPE_DECL) continue;
        
        if (n->children.size() > 0 && 
            n->children[0]->type == ASTNodeType::RECORD_TYPE) {
            
            RecordInfo rec;
            rec.name = n->value;
            rec.totalSize = 0;
            
            auto recordBody = n->children[0]->children[0]; // BODY node
            if (recordBody) {
                for (auto& field : recordBody->children) {
                    if (!field || field->type != ASTNodeType::VAR_DECL) continue;
                    
                    std::string fieldName = field->value;
                    auto [fieldType, fieldSize] = analyzeFieldType(field);
                    
                    rec.fields.push_back({fieldName, {fieldType, rec.totalSize}});
                    rec.totalSize += fieldSize;
                }
            }
            
            recordTypes[rec.name] = rec;
            std::cout << "📋 Record '" << rec.name << "': " 
                      << rec.totalSize << " bytes" << std::endl;
        }
    }
}


std::pair<uint8_t, int> WasmCompiler::analyzeFieldType(std::shared_ptr<ASTNode> fieldDecl) {
    if (!fieldDecl || fieldDecl->children.empty()) return {0x7f, 4};
    
    auto typeNode = fieldDecl->children[0];
    
    if (typeNode->type == ASTNodeType::PRIMITIVE_TYPE) {
        if (typeNode->value == "integer" || typeNode->value == "boolean") {
            return {0x7f, 4}; // i32 = 4 bytes
        } else if (typeNode->value == "real") {
            return {0x7c, 8}; // f64 = 8 bytes
        }
    } 
    else if (typeNode->type == ASTNodeType::ARRAY_TYPE) {
        auto [elemType,_, size] = analyzeArrayType(typeNode);
        int elemSize = (elemType == 0x7c) ? 8 : 4;
        return {elemType, size * elemSize};
    }
    else if (typeNode->type == ASTNodeType::USER_TYPE) {
        // Handle nested records
        auto it = recordTypes.find(typeNode->value);
        if (it != recordTypes.end()) {
            return {0x7f, it->second.totalSize}; // Treat as i32 for base address
        }
    }
    
    return {0x7f, 4}; // default
}

void WasmCompiler::generateMemberAssignment(std::vector<uint8_t>& body,
                                            std::shared_ptr<ASTNode> memberAccess,
                                            std::shared_ptr<ASTNode> rhs,
                                            const FuncInfo& F) {
    if (!memberAccess || memberAccess->children.size() < 1) {
        std::cout << "⚠️ Malformed member assignment\n";
        generateExpression(body, rhs, F);
        return;
    }
    
    uint8_t fieldType = 0x7f;
    auto base = memberAccess->children[0];
    std::string fieldName = memberAccess->value;
    
    // Handle both IDENTIFIER and ARRAY_ACCESS as base
    if (base->type == ASTNodeType::IDENTIFIER) {
        // Simple record variable: record.field
        std::string recordName = base->value;
        auto recordIt = recordVariables.find(recordName);
        if (recordIt == recordVariables.end()) {
            std::cout << "⚠️ Unknown record variable: " << recordName << "\n";
            generateExpression(body, rhs, F);
            return;
        }
        
        // Find field offset
        auto recordTypeIt = recordTypes.find(recordIt->second.recordType);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Unknown record type: " << recordIt->second.recordType << "\n";
            generateExpression(body, rhs, F);
            return;
        }
        
        int fieldOffset = -1;
        
        for (const auto& field : recordTypeIt->second.fields) {
            if (field.first == fieldName) {
                fieldOffset = field.second.second;
                fieldType = field.second.first;
                break;
            }
        }
        
        if (fieldOffset == -1) {
            std::cout << "⚠️ Unknown field '" << fieldName << "' in record '" 
                      << recordIt->second.recordType << "'\n";
            generateExpression(body, rhs, F);
            return;
        }
        
        // Calculate address: record_base + field_offset
        emitLocalGet(body, recordName);     // Base address
        emitI32Const(body, fieldOffset);    // Field offset
        body.push_back(0x6a);               // i32.add
        
    } else if (base->type == ASTNodeType::ARRAY_ACCESS) {
        // Array of records: array[index].field
        auto arrayRef = base->children[0];
        auto indexExpr = base->children[1];
        
        if (!arrayRef || !indexExpr || arrayRef->type != ASTNodeType::IDENTIFIER) {
            std::cout << "⚠️ Malformed array access in member assignment\n";
            generateExpression(body, rhs, F);
            return;
        }
        
        std::string arrayName = arrayRef->value;
        
        // Find array info to get the record type
        auto arrayIt = arrayInfos.find(arrayName);
        if (arrayIt == arrayInfos.end()) {
            std::cout << "⚠️ Unknown array: " << arrayName << "\n";
            generateExpression(body, rhs, F);
            return;
        }
        
        std::string recordTypeName = arrayIt->second.elemTypeName;
        
        // Verify it's actually a record type
        auto recordTypeIt = recordTypes.find(recordTypeName);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Array element type '" << recordTypeName << "' is not a record type\n";
            generateExpression(body, rhs, F);
            return;
        }
        
        // Find field offset within the record
        int fieldOffset = -1;
        
        for (const auto& field : recordTypeIt->second.fields) {
            if (field.first == fieldName) {
                fieldOffset = field.second.second;
                fieldType = field.second.first;
                break;
            }
        }
        
        if (fieldOffset == -1) {
            std::cout << "⚠️ Unknown field '" << fieldName << "' in record '" 
                      << recordTypeName << "'\n";
            generateExpression(body, rhs, F);
            return;
        }
        
        // Calculate address: array[index] + field_offset
        // First get array base address
        emitLocalGet(body, arrayName);
        
        // Calculate index * record_size
        generateExpression(body, indexExpr, F);
        
        // Multiply by record size
        int recordSize = recordTypeIt->second.totalSize;
        emitI32Const(body, recordSize);
        body.push_back(0x6c); // i32.mul
        
        // Add to base address to get array[index] address
        body.push_back(0x6a); // i32.add
        
        // Now add field offset
        emitI32Const(body, fieldOffset);
        body.push_back(0x6a); // i32.add
        
    } else {
        std::cout << "⚠️ Member assignment on unsupported base type: " << tname(base->type) << "\n";
        generateExpression(body, rhs, F);
        return;
    }
    
    // Generate the value to store
    generateExpression(body, rhs, F);
    
    // Store the value to memory
    if (fieldType == 0x7c) {
        body.push_back(0x39); // f64.store
        body.push_back(0x03); // 8-byte align
    } else {
        body.push_back(0x36); // i32.store  
        body.push_back(0x02); // 4-byte align
    }
    body.push_back(0x00); // offset 0 (already included in calculation)
}
void WasmCompiler::generateArrayAccess(std::vector<uint8_t>& body,
                                       std::shared_ptr<ASTNode> arrayAccess,
                                       const FuncInfo& F) {
    if (!arrayAccess || arrayAccess->children.size() != 2) {
        std::cout << "⚠️ Malformed array access\n";
        emitI32Const(body, 0);
        return;
    }
    
    auto arrayRef = arrayAccess->children[0];  // identifier OR member access
    auto indexExpr = arrayAccess->children[1]; // index expression
    
    if (!arrayRef || !indexExpr) {
        emitI32Const(body, 0);
        return;
    }
    
    std::string arrayName;
    ArrayInfo arrayInfo;
    
    if (arrayRef->type == ASTNodeType::IDENTIFIER) {
        // Simple array variable
        arrayName = arrayRef->value;
        auto it = arrayInfos.find(arrayName);
        if (it == arrayInfos.end()) {
            std::cout << "⚠️ Unknown array: " << arrayName << "\n";
            emitI32Const(body, 0);
            return;
        }
        arrayInfo = it->second;
        
        // Get the base address of the array
        emitLocalGet(body, arrayName);
        
    } else if (arrayRef->type == ASTNodeType::MEMBER_ACCESS) {
        // Array field inside a record - NEW CODE!
        auto [baseAddr, elemType, size] = resolveArrayMember(body, arrayRef, F);
        if (baseAddr == -1) {
            emitI32Const(body, 0);
            return;
        }
        
        // The base address is already on the stack from resolveArrayMember
        arrayInfo.elemType = elemType;
        arrayInfo.size = size;
        
    } else {
        std::cout << "⚠️ Array access on unsupported node type: " << tname(arrayRef->type) << "\n";
        emitI32Const(body, 0);
        return;
    }
    
    // Calculate the index * element_size
    generateExpression(body, indexExpr, F);
    
    // Multiply index by element size (4 for i32, 8 for f64)
    int elemSize = (arrayInfo.elemType == 0x7c) ? 8 : 4;
    if (elemSize != 1) {
        emitI32Const(body, elemSize);
        body.push_back(0x6c); // i32.mul
    }
    
    // Add to base address
    body.push_back(0x6a); // i32.add
    
    // Load the value from memory
    if (arrayInfo.elemType == 0x7c) {
        // f64 load
        body.push_back(0x2c); // f64.load
        body.push_back(0x00); // align
        body.push_back(0x00); // offset
    } else {
        // i32 load
        body.push_back(0x28); // i32.load
        body.push_back(0x00); // align
        body.push_back(0x00); // offset
    }
}