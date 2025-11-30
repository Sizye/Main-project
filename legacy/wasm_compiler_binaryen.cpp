#include "wasm_compiler.h"

#include <iostream>
#include <cstring>
#include <cmath>
#include <sstream>

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
// Helper: Convert ValueType to Binaryen Type
// ======================================================================

wasm::Type WasmCompiler::valueTypeToWasmType(ValueType vt) {
    switch (vt) {
        case ValueType::INTEGER:
        case ValueType::BOOLEAN:
            return wasm::Type::i32;
        case ValueType::REAL:
            return wasm::Type::f64;
        default:
            return wasm::Type::i32;
    }
}

// ======================================================================
// Public API
// ======================================================================

bool WasmCompiler::compile(std::shared_ptr<ASTNode> program,
                           const std::string& filename) {
    std::cout << "🚀 COMPILING TO WASM (Binaryen): " << filename << std::endl;

    if (!collectFunctions(program)) {
        std::cerr << "❌ No routines found (need at least main)" << std::endl;
        return false;
    }

    // Setup memory
    setupMemory();

    // Create all functions
    for (auto& F : funcs) {
        resetLocals();
        addParametersToLocals(F);
        
        auto locals = analyzeLocalVariables(F);
        wasm::Function* func = new wasm::Function();
        func->name = F.name;
        func->sig = wasm::Signature(
            wasm::Type(F.paramTypes),
            wasm::Type(F.resultTypes.empty() ? wasm::Type::none : F.resultTypes[0])
        );
        func->vars = locals;
        
        // Generate function body
        std::vector<wasm::Expression*> bodyExprs;
        
        // Initialize global variables (only in main function)
        if (F.name == "main") {
            for (auto& [varName, gv] : globalVars) {
                wasm::Expression* initExpr = nullptr;
                if (gv.initializer) {
                    initExpr = generateExpression(gv.initializer, F);
                    ValueType sourceType = getExpressionType(gv.initializer, F);
                    ValueType targetType = (gv.type == wasm::Type::f64) ? ValueType::REAL : ValueType::INTEGER;
                    if (sourceType != targetType) {
                        initExpr = emitTypeConversion(initExpr, sourceType, targetType);
                    }
                } else {
                    if (gv.type == wasm::Type::f64) {
                        initExpr = emitF64Const(0.0);
                    } else {
                        initExpr = emitI32Const(0);
                    }
                }
                if (initExpr) {
                    bodyExprs.push_back(emitLocalSet(varName, initExpr));
                }
            }
        }
        
        wasm::Expression* funcBody = generateFunctionBody(F);
        if (funcBody) {
            if (!bodyExprs.empty()) {
                bodyExprs.push_back(funcBody);
                func->body = builder.makeBlock("", bodyExprs);
            } else {
                func->body = funcBody;
            }
        } else if (!bodyExprs.empty()) {
            func->body = builder.makeBlock("", bodyExprs);
        } else {
            // Default return
            if (!F.resultTypes.empty()) {
                if (F.resultTypes[0] == wasm::Type::f64) {
                    func->body = builder.makeReturn(emitF64Const(0.0));
                } else {
                    func->body = builder.makeReturn(emitI32Const(0));
                }
            } else {
                func->body = builder.makeNop();
            }
        }
        
        module->addFunction(func);
    }
    
    // Export main function
    if (funcIndexByName.count("main")) {
        module->addExport(wasm::Export("main", wasm::Export::Function, funcIndexByName["main"]));
    }
    
    // Optimize module
    wasm::PassRunner passRunner(module.get());
    passRunner.addDefaultOptimizationPasses();
    passRunner.run();
    
    // Write to file
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::cerr << "❌ Cannot open " << filename << " for writing\n";
        return false;
    }
    
    wasm::ModuleWriter writer;
    writer.setBinary(true);
    writer.write(*module, out);
    out.close();
    
    std::cout << "✅ WROTE WASM module using Binaryen\n";
    std::cout << "💡 You can run it with: wasmtime --invoke main " << filename << "\n";
    return true;
}

// ======================================================================
// Collect routines and signatures
// ======================================================================

bool WasmCompiler::collectFunctions(std::shared_ptr<ASTNode> program) {
    funcs.clear();
    funcIndexByName.clear();
    globalVars.clear();
    globalArrays.clear();
    globalRecordVariables.clear();
    recordVariables.clear();
    globalMemoryOffset = 0;

    collectRecordTypes(program);
    if (!program || program->type != ASTNodeType::PROGRAM) return false;

    // First pass: Collect global variables
    for (auto& n : program->children) {
        if (!n) continue;
        
        std::shared_ptr<ASTNode> varDecl = nullptr;
        if (n->type == ASTNodeType::VAR_DECL) {
            varDecl = n;
        } else if (n->children.size() > 0) {
            auto child = n->children[0];
            if (child && child->type == ASTNodeType::VAR_DECL) {
                varDecl = child;
            }
        }
        
        if (varDecl) {
            GlobalVarInfo gv;
            gv.name = varDecl->value;
            gv.memoryOffset = globalMemoryOffset;
            
            if (varDecl->children.size() > 0 && varDecl->children[0]) {
                auto typeNode = varDecl->children[0];
                if (typeNode->type == ASTNodeType::PRIMITIVE_TYPE) {
                    gv.type = mapPrimitiveToWasm(typeNode->value);
                    gv.size = (gv.type == wasm::Type::f64) ? 8 : 4;
                } else if (typeNode->type == ASTNodeType::ARRAY_TYPE) {
                    auto [elemType, elemTypeName, size] = analyzeArrayType(typeNode);
                    gv.type = elemType;
                    int elemSize = (elemType == wasm::Type::f64) ? 8 : 4;
                    if (recordTypes.find(elemTypeName) != recordTypes.end()) {
                        elemSize = recordTypes[elemTypeName].totalSize;
                    }
                    gv.size = size * elemSize;
                    
                    ArrayInfo arrInfo;
                    arrInfo.elemType = elemType;
                    arrInfo.elemTypeName = elemTypeName;
                    arrInfo.size = size;
                    arrInfo.baseOffset = gv.memoryOffset;
                    globalArrays[gv.name] = arrInfo;
                } else if (typeNode->type == ASTNodeType::USER_TYPE) {
                    auto it = recordTypes.find(typeNode->value);
                    if (it != recordTypes.end()) {
                        gv.type = wasm::Type::i32;
                        gv.size = it->second.totalSize;
                        
                        RecordVarInfo recVar;
                        recVar.recordType = typeNode->value;
                        recVar.baseOffset = gv.memoryOffset;
                        recVar.size = gv.size;
                        globalRecordVariables[gv.name] = recVar;
                    } else {
                        gv.type = wasm::Type::i32;
                        gv.size = 4;
                    }
                } else {
                    gv.type = wasm::Type::i32;
                    gv.size = 4;
                }
            } else {
                gv.type = wasm::Type::i32;
                gv.size = 4;
            }
            
            if (varDecl->children.size() >= 2 && varDecl->children[1]) {
                gv.initializer = varDecl->children[1];
            } else {
                gv.initializer = nullptr;
            }
            
            globalVars[gv.name] = gv;
            globalMemoryOffset += gv.size;
        }
    }

    // Second pass: Collect functions
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
            wasm::Type wt = wasm::Type::i32;
            for (auto& pc : p->children) {
                if (!pc) continue;
                if (pc->type == ASTNodeType::PRIMITIVE_TYPE) {
                    wt = mapPrimitiveToWasm(pc->value);
                } else if (pc->type == ASTNodeType::USER_TYPE) {
                    wt = wasm::Type::i32;
                }
            }
            F.paramTypes.push_back(wt);
        }
    }

    if (retType) {
        F.resultTypes.push_back(mapPrimitiveToWasm(retType->value));
    } else {
        F.resultTypes.push_back(wasm::Type::i32);
    }
}

wasm::Type WasmCompiler::mapPrimitiveToWasm(const std::string& tname) {
    if (tname == "integer" || tname == "boolean") return wasm::Type::i32;
    if (tname == "real") return wasm::Type::f64;
    return wasm::Type::i32;
}

// ======================================================================
// Per-function codegen utilities
// ======================================================================

void WasmCompiler::resetLocals() {
    localVarIndices.clear();
    arrayInfos.clear();
    recordVariables = globalRecordVariables;
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
        std::string paramName = p->value;
        localVarIndices[paramName] = idx++;
        
        for (auto& pc : p->children) {
            if (!pc) continue;
            if (pc->type == ASTNodeType::ARRAY_TYPE) {
                auto [elemType, elemTypeName, size] = analyzeArrayType(pc);
                
                ArrayInfo arrInfo;
                arrInfo.elemType = elemType;
                arrInfo.elemTypeName = elemTypeName;
                arrInfo.size = size;
                arrInfo.baseOffset = 0;
                arrayInfos[paramName] = arrInfo;
                break;
            }
        }
    }
    nextLocalIndex = idx;
}

void WasmCompiler::collectAllVariableDeclarations(std::shared_ptr<ASTNode> node,
                                                   std::vector<std::shared_ptr<ASTNode>>& varDecls,
                                                   bool insideBody) {
    if (!node) return;
    
    bool nowInsideBody = insideBody || (node->type == ASTNodeType::BODY);
    
    if (node->type == ASTNodeType::VAR_DECL && nowInsideBody) {
        varDecls.push_back(node);
    }
    
    if (node->type == ASTNodeType::RECORD_TYPE || node->type == ASTNodeType::TYPE_DECL) {
        return;
    }
    
    for (auto& child : node->children) {
        collectAllVariableDeclarations(child, varDecls, nowInsideBody);
    }
}

std::vector<wasm::Type> WasmCompiler::analyzeLocalVariables(const FuncInfo& F) {
    std::vector<wasm::Type> locals;

    std::shared_ptr<ASTNode> bodyNode = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
    }
    if (!bodyNode) {
        locals.push_back(wasm::Type::i32);
        locals.push_back(wasm::Type::i32);
        return locals;
    }

    std::vector<std::shared_ptr<ASTNode>> allVarDecls;
    collectAllVariableDeclarations(bodyNode, allVarDecls, true);

    for (auto& s : allVarDecls) {
        if (!s || s->type != ASTNodeType::VAR_DECL) continue;
        const std::string& name = s->value;
        if (localVarIndices.count(name)) continue;
        
        if (s->children.size() >= 1 && s->children[0]) {
            auto typeNode = s->children[0];
            if (typeNode->type == ASTNodeType::USER_TYPE) {
                auto it = recordTypes.find(typeNode->value);
                if (it != recordTypes.end()) {
                    RecordVarInfo recVar;
                    recVar.recordType = typeNode->value;
                    recVar.size = it->second.totalSize;
                    recVar.baseOffset = globalMemoryOffset;
                    
                    recordVariables[name] = recVar;
                    globalMemoryOffset += recVar.size;
                    
                    localVarIndices[name] = nextLocalIndex++;
                    locals.push_back(wasm::Type::i32);
                    continue;
                }
            }
            if (typeNode->type == ASTNodeType::ARRAY_TYPE) {
                auto [elemType, elemTypeName, size] = analyzeArrayType(typeNode);
                
                localVarIndices[name] = nextLocalIndex++;
                
                ArrayInfo arrInfo;
                arrInfo.elemType = elemType;
                arrInfo.elemTypeName = elemTypeName;
                arrInfo.size = size;
                arrInfo.baseOffset = globalMemoryOffset;
                arrayInfos[name] = arrInfo;
                
                int elemSize = 4;
                if (elemType == wasm::Type::f64) {
                    elemSize = 8;
                } else if (recordTypes.find(elemTypeName) != recordTypes.end()) {
                    elemSize = recordTypes[elemTypeName].totalSize;
                }
                
                globalMemoryOffset += size * elemSize;
                locals.push_back(wasm::Type::i32);
            } else {
                localVarIndices[name] = nextLocalIndex++;
                wasm::Type wt = wasm::Type::i32;
                if (typeNode->type == ASTNodeType::PRIMITIVE_TYPE) {
                    wt = mapPrimitiveToWasm(typeNode->value);
                }
                locals.push_back(wt);
            }
        } else {
            localVarIndices[name] = nextLocalIndex++;
            locals.push_back(wasm::Type::i32);
        }
    }

    locals.push_back(wasm::Type::i32);
    locals.push_back(wasm::Type::i32);
    nextLocalIndex += 2;
    
    return locals;
}

void WasmCompiler::generateVarDeclaration(std::vector<wasm::Expression*>& body,
                                          std::shared_ptr<ASTNode> decl,
                                          const FuncInfo& F) {
    if (!decl) return;
    const std::string& name = decl->value;
    if (decl->children.size() < 2 || !decl->children[1]) return;
    
    auto initializer = decl->children[1];
    wasm::Expression* initExpr = generateExpression(initializer, F);
    
    ValueType sourceType = getExpressionType(initializer, F);
    ValueType targetType = ValueType::UNKNOWN;
    if (!decl->children.empty() && decl->children[0]) {
        auto typeNode = decl->children[0];
        if (typeNode->type == ASTNodeType::PRIMITIVE_TYPE) {
            if (typeNode->value == "integer") targetType = ValueType::INTEGER;
            else if (typeNode->value == "real") targetType = ValueType::REAL;
            else if (typeNode->value == "boolean") targetType = ValueType::BOOLEAN;
        }
    }
    
    if (targetType != ValueType::UNKNOWN && sourceType != targetType) {
        initExpr = emitTypeConversion(initExpr, sourceType, targetType);
    }
    
    body.push_back(emitLocalSet(name, initExpr));
}

wasm::Expression* WasmCompiler::generateFunctionBody(const FuncInfo& F) {
    std::shared_ptr<ASTNode> bodyNode = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
    }
    if (!bodyNode) return nullptr;

    std::vector<wasm::Expression*> bodyExprs;
    bool hasReturn = false;
    
    for (auto& s : bodyNode->children) {
        if (!s) continue;
        switch (s->type) {
            case ASTNodeType::VAR_DECL: {
                std::vector<wasm::Expression*> varBody;
                generateVarDeclaration(varBody, s, F);
                bodyExprs.insert(bodyExprs.end(), varBody.begin(), varBody.end());
                break;
            }
            case ASTNodeType::ASSIGNMENT:
                bodyExprs.push_back(generateAssignment(s, F));
                break;
            case ASTNodeType::IF_STMT:
                bodyExprs.push_back(generateIfStatement(s, F));
                break;
            case ASTNodeType::WHILE_LOOP:
                bodyExprs.push_back(generateWhileLoop(s, F));
                break;
            case ASTNodeType::FOR_LOOP:
                bodyExprs.push_back(generateForLoop(s, F));
                break;
            case ASTNodeType::RETURN_STMT:
                bodyExprs.push_back(generateReturn(s, F));
                hasReturn = true;
                break;
            case ASTNodeType::ROUTINE_CALL:
                bodyExprs.push_back(builder.makeDrop(generateCall(s, F)));
                break;
            case ASTNodeType::PRINT_STMT:
                bodyExprs.push_back(generatePrintStatement(s, F));
                break;
            default:
                std::cout << "  ⚠️ Unhandled stmt in "
                          << F.name << ": " << tname(s->type) << "\n";
                break;
        }
    }
    
    if (bodyExprs.empty()) {
        return nullptr;
    }
    
    if (bodyExprs.size() == 1) {
        return bodyExprs[0];
    }
    
    return builder.makeBlock("", bodyExprs);
}

// ======================================================================
// Statements
// ======================================================================

wasm::Expression* WasmCompiler::generateAssignment(std::shared_ptr<ASTNode> a,
                                                   const FuncInfo& F) {
    if (!a || a->children.size() != 2) return builder.makeNop();
    auto lhs = a->children[0];
    auto rhs = a->children[1];
    if (!lhs || !rhs) return builder.makeNop();
    
    ValueType targetType = getExpressionType(lhs, F);
    ValueType sourceType = getExpressionType(rhs, F);
    
    if (!validateAssignmentConversion(sourceType, targetType, "assignment")) {
        std::cerr << "❌ Type error: Cannot assign " << (int)sourceType << " to " << (int)targetType << std::endl;
        return builder.makeNop();
    }
    
    wasm::Expression* rhsExpr = generateExpression(rhs, F);
    rhsExpr = emitTypeConversion(rhsExpr, sourceType, targetType);
    
    if (lhs->type == ASTNodeType::IDENTIFIER) {
        return emitLocalSet(lhs->value, rhsExpr);
    } else if (lhs->type == ASTNodeType::ARRAY_ACCESS) {
        wasm::Expression* addrExpr = generateArrayAssignment(lhs, nullptr, F);
        wasm::Type elemType = wasm::Type::i32;
        auto arrayRef = lhs->children[0];
        if (arrayRef->type == ASTNodeType::IDENTIFIER) {
            auto it = arrayInfos.find(arrayRef->value);
            if (it != arrayInfos.end()) elemType = it->second.elemType;
        }
        
        if (elemType == wasm::Type::f64) {
            return builder.makeStore(8, 0, 0, addrExpr, rhsExpr, wasm::Type::f64);
        } else {
            return builder.makeStore(4, 0, 0, addrExpr, rhsExpr, wasm::Type::i32);
        }
    } else if (lhs->type == ASTNodeType::MEMBER_ACCESS) {
        wasm::Expression* addrExpr = generateMemberAssignment(lhs, nullptr, F);
        wasm::Type fieldType = wasm::Type::i32;
        auto base = lhs->children[0];
        std::string fieldName = lhs->value;
        if (base->type == ASTNodeType::IDENTIFIER) {
            auto recordIt = recordVariables.find(base->value);
            if (recordIt != recordVariables.end()) {
                auto recordTypeIt = recordTypes.find(recordIt->second.recordType);
                if (recordTypeIt != recordTypes.end()) {
                    for (const auto& field : recordTypeIt->second.fields) {
                        if (field.first == fieldName) {
                            fieldType = field.second.first;
                            break;
                        }
                    }
                }
            }
        }
        
        if (fieldType == wasm::Type::f64) {
            return builder.makeStore(8, 0, 0, addrExpr, rhsExpr, wasm::Type::f64);
        } else {
            return builder.makeStore(4, 0, 0, addrExpr, rhsExpr, wasm::Type::i32);
        }
    } else {
        std::cout << "⚠️ Only simple identifier, array, and member assignments supported\n";
        return builder.makeDrop(rhsExpr);
    }
}

wasm::Expression* WasmCompiler::generateIfStatement(std::shared_ptr<ASTNode> ifs,
                                                     const FuncInfo& F) {
    if (!ifs || ifs->children.size() < 2) return builder.makeNop();
    auto cond = ifs->children[0];
    auto thenB = ifs->children[1];
    std::shared_ptr<ASTNode> elseB =
        (ifs->children.size() > 2) ? ifs->children[2] : nullptr;

    wasm::Expression* condExpr = generateExpression(cond, F);
    
    std::vector<wasm::Expression*> thenExprs;
    if (thenB && thenB->type == ASTNodeType::BODY) {
        for (auto& s : thenB->children) {
            if (!s) continue;
            switch (s->type) {
                case ASTNodeType::ASSIGNMENT: thenExprs.push_back(generateAssignment(s, F)); break;
                case ASTNodeType::IF_STMT: thenExprs.push_back(generateIfStatement(s, F)); break;
                case ASTNodeType::WHILE_LOOP: thenExprs.push_back(generateWhileLoop(s, F)); break;
                case ASTNodeType::FOR_LOOP: thenExprs.push_back(generateForLoop(s, F)); break;
                case ASTNodeType::RETURN_STMT: thenExprs.push_back(generateReturn(s, F)); break;
                case ASTNodeType::VAR_DECL: {
                    std::vector<wasm::Expression*> varBody;
                    generateVarDeclaration(varBody, s, F);
                    thenExprs.insert(thenExprs.end(), varBody.begin(), varBody.end());
                    break;
                }
                default: break;
            }
        }
    }
    
    wasm::Expression* thenBlock = thenExprs.empty() ? builder.makeNop() :
        (thenExprs.size() == 1 ? thenExprs[0] : builder.makeBlock("", thenExprs));
    
    if (elseB) {
        std::vector<wasm::Expression*> elseExprs;
        if (elseB->type == ASTNodeType::BODY) {
            for (auto& s : elseB->children) {
                if (!s) continue;
                switch (s->type) {
                    case ASTNodeType::ASSIGNMENT: elseExprs.push_back(generateAssignment(s, F)); break;
                    case ASTNodeType::IF_STMT: elseExprs.push_back(generateIfStatement(s, F)); break;
                    case ASTNodeType::WHILE_LOOP: elseExprs.push_back(generateWhileLoop(s, F)); break;
                    case ASTNodeType::FOR_LOOP: elseExprs.push_back(generateForLoop(s, F)); break;
                    case ASTNodeType::RETURN_STMT: elseExprs.push_back(generateReturn(s, F)); break;
                    case ASTNodeType::VAR_DECL: {
                        std::vector<wasm::Expression*> varBody;
                        generateVarDeclaration(varBody, s, F);
                        elseExprs.insert(elseExprs.end(), varBody.begin(), varBody.end());
                        break;
                    }
                    default: break;
                }
            }
        }
        wasm::Expression* elseBlock = elseExprs.empty() ? builder.makeNop() :
            (elseExprs.size() == 1 ? elseExprs[0] : builder.makeBlock("", elseExprs));
        
        return builder.makeIf(condExpr, thenBlock, elseBlock);
    } else {
        return builder.makeIf(condExpr, thenBlock);
    }
}

wasm::Expression* WasmCompiler::generateWhileLoop(std::shared_ptr<ASTNode> w,
                                                   const FuncInfo& F) {
    if (!w || w->children.size() < 2) return builder.makeNop();
    auto cond = w->children[0];
    auto loopB = w->children[1];

    std::string loopName = "loop";
    wasm::Expression* condExpr = generateExpression(cond, F);
    
    std::vector<wasm::Expression*> bodyExprs;
    if (loopB && loopB->type == ASTNodeType::BODY) {
        for (auto& s : loopB->children) {
            if (!s) continue;
            switch (s->type) {
                case ASTNodeType::ASSIGNMENT: bodyExprs.push_back(generateAssignment(s, F)); break;
                case ASTNodeType::IF_STMT: bodyExprs.push_back(generateIfStatement(s, F)); break;
                case ASTNodeType::WHILE_LOOP: bodyExprs.push_back(generateWhileLoop(s, F)); break;
                case ASTNodeType::FOR_LOOP: bodyExprs.push_back(generateForLoop(s, F)); break;
                case ASTNodeType::RETURN_STMT: bodyExprs.push_back(generateReturn(s, F)); break;
                case ASTNodeType::VAR_DECL: {
                    std::vector<wasm::Expression*> varBody;
                    generateVarDeclaration(varBody, s, F);
                    bodyExprs.insert(bodyExprs.end(), varBody.begin(), varBody.end());
                    break;
                }
                default: break;
            }
        }
    }
    
    wasm::Expression* bodyBlock = bodyExprs.empty() ? builder.makeNop() :
        (bodyExprs.size() == 1 ? bodyExprs[0] : builder.makeBlock("", bodyExprs));
    
    // Create: block (loop (if (not cond) (br 1)) body (br 0))
    wasm::Expression* ifBreak = builder.makeIf(
        builder.makeUnary(wasm::EqZInt32, condExpr),
        builder.makeBreak("block", nullptr, nullptr)
    );
    
    std::vector<wasm::Expression*> loopBody;
    loopBody.push_back(ifBreak);
    loopBody.push_back(bodyBlock);
    loopBody.push_back(builder.makeBreak(loopName, nullptr, nullptr));
    
    wasm::Expression* loop = builder.makeLoop(loopName, builder.makeBlock("", loopBody));
    return builder.makeBlock("block", {loop});
}

wasm::Expression* WasmCompiler::generateForLoop(std::shared_ptr<ASTNode> forNode,
                                                const FuncInfo& F) {
    if (!forNode) {
        std::cout << "⚠️ Malformed FOR_LOOP node\n";
        return builder.makeNop();
    }

    const std::string iv = forNode->value;

    std::shared_ptr<ASTNode> rangeNode = nullptr;
    std::shared_ptr<ASTNode> loopBody = nullptr;
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
        return builder.makeNop();
    }

    auto it = localVarIndices.find(iv);
    if (it == localVarIndices.end()) {
        std::cout << "⚠️ Loop variable not declared as local: " << iv << "\n";
        return builder.makeNop();
    }
    uint32_t ivIdx = static_cast<uint32_t>(it->second);

    std::shared_ptr<ASTNode> startExpr = nullptr, endExpr = nullptr;
    if (rangeNode->children.size() >= 2) {
        startExpr = rangeNode->children[0];
        endExpr = rangeNode->children[1];
    }
    if (!endExpr) {
        std::cout << "⚠️ FOR_LOOP missing range end\n";
        return builder.makeNop();
    }

    wasm::Expression* startVal = startExpr ? generateExpression(startExpr, F) : emitI32Const(0);
    wasm::Expression* endVal = generateExpression(endExpr, F);
    
    // Initialize loop variable
    wasm::Expression* init = emitLocalSet(iv, startVal);
    
    // Loop condition and body
    std::vector<wasm::Expression*> bodyExprs;
    if (loopBody && loopBody->type == ASTNodeType::BODY) {
        for (auto& s : loopBody->children) {
            if (!s) continue;
            switch (s->type) {
                case ASTNodeType::ASSIGNMENT: bodyExprs.push_back(generateAssignment(s, F)); break;
                case ASTNodeType::IF_STMT: bodyExprs.push_back(generateIfStatement(s, F)); break;
                case ASTNodeType::WHILE_LOOP: bodyExprs.push_back(generateWhileLoop(s, F)); break;
                case ASTNodeType::FOR_LOOP: bodyExprs.push_back(generateForLoop(s, F)); break;
                case ASTNodeType::RETURN_STMT: bodyExprs.push_back(generateReturn(s, F)); break;
                case ASTNodeType::VAR_DECL: {
                    std::vector<wasm::Expression*> varBody;
                    generateVarDeclaration(varBody, s, F);
                    bodyExprs.insert(bodyExprs.end(), varBody.begin(), varBody.end());
                    break;
                }
                default: break;
            }
        }
    }
    
    wasm::Expression* bodyBlock = bodyExprs.empty() ? builder.makeNop() :
        (bodyExprs.size() == 1 ? bodyExprs[0] : builder.makeBlock("", bodyExprs));
    
    // Step: i := i ± 1
    wasm::Expression* step = builder.makeLocalSet(
        ivIdx,
        builder.makeBinary(
            isReverse ? wasm::SubInt32 : wasm::AddInt32,
            emitLocalGet(iv),
            emitI32Const(1)
        )
    );
    
    // Condition: forward break if i > end, reverse break if i < end
    wasm::Expression* cond = builder.makeBinary(
        isReverse ? wasm::LtSInt32 : wasm::GtSInt32,
        emitLocalGet(iv),
        endVal
    );
    
    std::vector<wasm::Expression*> loopExprs;
    loopExprs.push_back(builder.makeIf(cond, builder.makeBreak("loop", nullptr, nullptr)));
    loopExprs.push_back(bodyBlock);
    loopExprs.push_back(step);
    loopExprs.push_back(builder.makeBreak("loop", nullptr, nullptr));
    
    wasm::Expression* loop = builder.makeLoop("loop", builder.makeBlock("", loopExprs));
    return builder.makeBlock("", {init, loop});
}

wasm::Expression* WasmCompiler::generateReturn(std::shared_ptr<ASTNode> r,
                                               const FuncInfo& F) {
    if (!r) return builder.makeNop();
    if (!F.resultTypes.empty()) {
        wasm::Type expectedWasmType = F.resultTypes[0];
        ValueType expectedType = ValueType::INTEGER;
        if (expectedWasmType == wasm::Type::f64) {
            expectedType = ValueType::REAL;
        } else {
            std::shared_ptr<ASTNode> retType = nullptr;
            for (auto& ch : F.node->children) {
                if (ch && (ch->type == ASTNodeType::PRIMITIVE_TYPE || 
                           ch->type == ASTNodeType::USER_TYPE)) {
                    retType = ch;
                    break;
                }
            }
            if (retType && retType->type == ASTNodeType::PRIMITIVE_TYPE) {
                if (retType->value == "boolean") expectedType = ValueType::BOOLEAN;
                else expectedType = ValueType::INTEGER;
            }
        }
        
        if (!r->children.empty() && r->children[0]) {
            auto returnExpr = r->children[0];
            ValueType actualType = getExpressionType(returnExpr, F);
            wasm::Expression* retExpr = generateExpression(returnExpr, F);
            if (actualType != expectedType) {
                retExpr = emitTypeConversion(retExpr, actualType, expectedType);
            }
            return builder.makeReturn(retExpr);
        } else {
            if (expectedType == ValueType::REAL) {
                return builder.makeReturn(emitF64Const(0.0));
            } else {
                return builder.makeReturn(emitI32Const(0));
            }
        }
    }
    return builder.makeReturn();
}

// ======================================================================
// Expressions
// ======================================================================

wasm::Expression* WasmCompiler::generateExpression(std::shared_ptr<ASTNode> e,
                                                   const FuncInfo& F) {
    if (!e) return builder.makeNop();
    switch (e->type) {
        case ASTNodeType::LITERAL_INT: {
            try {
                return emitI32Const(std::stoi(e->value));
            } catch (...) {
                return emitI32Const(0);
            }
        }
        case ASTNodeType::LITERAL_BOOL:
            return emitI32Const(e->value == "true" ? 1 : 0);
        case ASTNodeType::LITERAL_REAL: {
            try {
                return emitF64Const(std::stod(e->value));
            } catch (...) {
                return emitF64Const(0.0);
            }
        }
        case ASTNodeType::IDENTIFIER:
            return emitLocalGet(e->value);
        case ASTNodeType::ARRAY_ACCESS:
            return generateArrayAccess(e, F);
        case ASTNodeType::MEMBER_ACCESS:
            return generateMemberAccess(e, F);
        case ASTNodeType::BINARY_OP:
            return generateBinaryOp(e, F);
        case ASTNodeType::UNARY_OP: {
            const std::string& op = e->value;
            if (e->children.empty()) {
                return emitI32Const(0);
            }
            
            if (op == "not") {
                return builder.makeUnary(wasm::EqZInt32, generateExpression(e->children[0], F));
            } else if (op == "-") {
                ValueType exprType = getExpressionType(e->children[0], F);
                wasm::Expression* operand = generateExpression(e->children[0], F);
                if (exprType == ValueType::REAL) {
                    return builder.makeBinary(wasm::SubFloat64, emitF64Const(0.0), operand);
                } else {
                    return builder.makeBinary(wasm::SubInt32, emitI32Const(0), operand);
                }
            } else if (op == "+") {
                return generateExpression(e->children[0], F);
            } else {
                return emitI32Const(0);
            }
        }
        case ASTNodeType::ROUTINE_CALL:
            return generateCall(e, F);
        case ASTNodeType::PRINT_STMT:
            return generatePrintStatement(e, F);
        case ASTNodeType::SIZE_EXPRESSION: {
            if (!e->children.empty() && e->children[0]) {
                auto arrayRef = e->children[0];
                if (arrayRef->type == ASTNodeType::IDENTIFIER) {
                    std::string arrayName = arrayRef->value;
                    auto it = arrayInfos.find(arrayName);
                    if (it != arrayInfos.end()) {
                        return emitI32Const(it->second.size);
                    } else {
                        auto globalIt = globalArrays.find(arrayName);
                        if (globalIt != globalArrays.end()) {
                            return emitI32Const(globalIt->second.size);
                        } else {
                            std::cout << "  ⚠️ Unknown array in size(): " << arrayName << "\n";
                            return emitI32Const(0);
                        }
                    }
                } else {
                    std::cout << "  ⚠️ size() only supports simple array identifiers\n";
                    return emitI32Const(0);
                }
            } else {
                return emitI32Const(0);
            }
        }
        default:
            std::cout << "  ⚠️ Unhandled expr: " << tname(e->type) << "\n";
            return emitI32Const(0);
    }
}

wasm::Expression* WasmCompiler::generateBinaryOp(std::shared_ptr<ASTNode> bin,
                                                 const FuncInfo& F) {
    if (!bin || bin->children.size() != 2) {
        return emitI32Const(0);
    }
    
    auto L = bin->children[0];
    auto R = bin->children[1];
    
    ValueType leftType = getExpressionType(L, F);
    ValueType rightType = getExpressionType(R, F);
    ValueType resultType = ValueType::INTEGER;
    
    if (leftType == ValueType::REAL || rightType == ValueType::REAL) {
        resultType = ValueType::REAL;
    } else if (leftType == ValueType::INTEGER || rightType == ValueType::INTEGER) {
        resultType = ValueType::INTEGER;
    } else {
        resultType = ValueType::BOOLEAN;
    }
    
    wasm::Expression* leftExpr = generateExpression(L, F);
    if (leftType != resultType) {
        leftExpr = emitTypeConversion(leftExpr, leftType, resultType);
    }
    
    wasm::Expression* rightExpr = generateExpression(R, F);
    if (rightType != resultType) {
        rightExpr = emitTypeConversion(rightExpr, rightType, resultType);
    }
    
    const std::string& op = bin->value;
    
    if (resultType == ValueType::REAL) {
        if (op == "+") return builder.makeBinary(wasm::AddFloat64, leftExpr, rightExpr);
        else if (op == "-") return builder.makeBinary(wasm::SubFloat64, leftExpr, rightExpr);
        else if (op == "*") return builder.makeBinary(wasm::MulFloat64, leftExpr, rightExpr);
        else if (op == "/") return builder.makeBinary(wasm::DivFloat64, leftExpr, rightExpr);
        else if (op == "<") return builder.makeBinary(wasm::LtFloat64, leftExpr, rightExpr);
        else if (op == "<=") return builder.makeBinary(wasm::LeFloat64, leftExpr, rightExpr);
        else if (op == ">") return builder.makeBinary(wasm::GtFloat64, leftExpr, rightExpr);
        else if (op == ">=") return builder.makeBinary(wasm::GeFloat64, leftExpr, rightExpr);
        else if (op == "=") return builder.makeBinary(wasm::EqFloat64, leftExpr, rightExpr);
        else if (op == "/=") return builder.makeBinary(wasm::NeFloat64, leftExpr, rightExpr);
        else {
            std::cout << "  ⚠️ Unhandled real binop: " << op << "\n";
            return builder.makeBinary(wasm::AddFloat64, leftExpr, rightExpr);
        }
    } else {
        if (op == "+") return builder.makeBinary(wasm::AddInt32, leftExpr, rightExpr);
        else if (op == "-") return builder.makeBinary(wasm::SubInt32, leftExpr, rightExpr);
        else if (op == "*") return builder.makeBinary(wasm::MulInt32, leftExpr, rightExpr);
        else if (op == "/") return builder.makeBinary(wasm::DivSInt32, leftExpr, rightExpr);
        else if (op == "%") return builder.makeBinary(wasm::RemSInt32, leftExpr, rightExpr);
        else if (op == "and") return builder.makeBinary(wasm::AndInt32, leftExpr, rightExpr);
        else if (op == "or") return builder.makeBinary(wasm::OrInt32, leftExpr, rightExpr);
        else if (op == "xor") return builder.makeBinary(wasm::XorInt32, leftExpr, rightExpr);
        else if (op == "<") return builder.makeBinary(wasm::LtSInt32, leftExpr, rightExpr);
        else if (op == "<=") return builder.makeBinary(wasm::LeSInt32, leftExpr, rightExpr);
        else if (op == ">") return builder.makeBinary(wasm::GtSInt32, leftExpr, rightExpr);
        else if (op == ">=") return builder.makeBinary(wasm::GeSInt32, leftExpr, rightExpr);
        else if (op == "=") return builder.makeBinary(wasm::EqInt32, leftExpr, rightExpr);
        else if (op == "/=") return builder.makeBinary(wasm::NeInt32, leftExpr, rightExpr);
        else {
            std::cout << "  ⚠️ Unhandled binop: " << op << "\n";
            return emitI32Const(0);
        }
    }
}

wasm::Expression* WasmCompiler::generateCall(std::shared_ptr<ASTNode> call,
                                             const FuncInfo& F) {
    std::vector<wasm::Expression*> args;

    for (auto& ch : call->children) {
        if (!ch) continue;
        if (ch->type == ASTNodeType::ARGUMENT_LIST) {
            for (auto& a : ch->children) if (a) args.push_back(generateExpression(a, F));
        } else {
            args.push_back(generateExpression(ch, F));
        }
    }

    auto it = funcIndexByName.find(call->value);
    if (it == funcIndexByName.end()) {
        std::cout << "  ⚠️ Unknown callee: " << call->value << " (push 0)\n";
        return emitI32Const(0);
    }
    
    return builder.makeCall(call->value, args, wasm::Type::none);
}

// ======================================================================
// Emit helpers
// ======================================================================

wasm::Expression* WasmCompiler::emitI32Const(int v) {
    return builder.makeConst(wasm::Literal(v));
}

wasm::Expression* WasmCompiler::emitF64Const(double d) {
    return builder.makeConst(wasm::Literal(d));
}

wasm::Expression* WasmCompiler::emitLocalGet(const std::string& name) {
    auto localIt = localVarIndices.find(name);
    if (localIt != localVarIndices.end()) {
        return builder.makeLocalGet(localIt->second, wasm::Type::i32);
    }
    
    auto globalIt = globalVars.find(name);
    if (globalIt != globalVars.end()) {
        wasm::Expression* addr = builder.makeConst(wasm::Literal(globalIt->second.memoryOffset));
        if (globalIt->second.type == wasm::Type::f64) {
            return builder.makeLoad(8, false, 0, 0, addr, wasm::Type::f64);
        } else {
            return builder.makeLoad(4, false, 0, 0, addr, wasm::Type::i32);
        }
    }
    
    auto arrayIt = globalArrays.find(name);
    if (arrayIt != globalArrays.end()) {
        return builder.makeConst(wasm::Literal(arrayIt->second.baseOffset));
    }
    
    std::cout << "  ⚠️ Unknown variable get: " << name << " (use 0)\n";
    return emitI32Const(0);
}

wasm::Expression* WasmCompiler::emitRecordBaseAddress(const std::string& name) {
    auto recordIt = recordVariables.find(name);
    if (recordIt != recordVariables.end()) {
        return builder.makeConst(wasm::Literal(recordIt->second.baseOffset));
    }
    return emitLocalGet(name);
}

wasm::Expression* WasmCompiler::emitLocalSet(const std::string& name, wasm::Expression* value) {
    auto localIt = localVarIndices.find(name);
    if (localIt != localVarIndices.end()) {
        return builder.makeLocalSet(localIt->second, value);
    }
    
    auto globalIt = globalVars.find(name);
    if (globalIt != globalVars.end()) {
        wasm::Expression* addr = builder.makeConst(wasm::Literal(globalIt->second.memoryOffset));
        if (globalIt->second.type == wasm::Type::f64) {
            return builder.makeStore(8, 0, 0, addr, value, wasm::Type::f64);
        } else {
            return builder.makeStore(4, 0, 0, addr, value, wasm::Type::i32);
        }
    }
    
    std::cout << "  ⚠️ Unknown variable set: " << name << " (dropping value)\n";
    return builder.makeDrop(value);
}

wasm::Expression* WasmCompiler::emitI32Load(uint32_t offset) {
    wasm::Expression* addr = builder.makeConst(wasm::Literal(static_cast<int32_t>(offset)));
    return builder.makeLoad(4, false, 0, 0, addr, wasm::Type::i32);
}

wasm::Expression* WasmCompiler::emitI32Store(uint32_t offset, wasm::Expression* value) {
    wasm::Expression* addr = builder.makeConst(wasm::Literal(static_cast<int32_t>(offset)));
    return builder.makeStore(4, 0, 0, addr, value, wasm::Type::i32);
}

wasm::Expression* WasmCompiler::emitF64Load(uint32_t offset) {
    wasm::Expression* addr = builder.makeConst(wasm::Literal(static_cast<int32_t>(offset)));
    return builder.makeLoad(8, false, 0, 0, addr, wasm::Type::f64);
}

wasm::Expression* WasmCompiler::emitF64Store(uint32_t offset, wasm::Expression* value) {
    wasm::Expression* addr = builder.makeConst(wasm::Literal(static_cast<int32_t>(offset)));
    return builder.makeStore(8, 0, 0, addr, value, wasm::Type::f64);
}

void WasmCompiler::setupMemory() {
    int totalMemoryNeeded = globalMemoryOffset;
    int totalMemoryPages = (totalMemoryNeeded + 65535) / 65536;
    
    if (totalMemoryPages == 0) totalMemoryPages = 1;
    if (totalMemoryPages > 1024) totalMemoryPages = 1024;
    
    std::cout << "📊 Total memory needed: " << totalMemoryNeeded 
              << " bytes (" << totalMemoryPages << " pages)" << std::endl;
    
    module->memory.initial = totalMemoryPages;
    module->memory.max = totalMemoryPages;
}

// ======================================================================
// Array and Type Analysis
// ======================================================================

std::tuple<wasm::Type, std::string, int> WasmCompiler::analyzeArrayType(std::shared_ptr<ASTNode> arrayTypeNode) {
    if (!arrayTypeNode || arrayTypeNode->type != ASTNodeType::ARRAY_TYPE) {
        return {wasm::Type::i32, "integer", 0};
    }
    
    wasm::Type elemType = wasm::Type::i32;
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
            elemType = wasm::Type::i32;
        }
    }
    
    return {elemType, typeName, size};
}

wasm::Type WasmCompiler::getArrayType(std::shared_ptr<ASTNode> arrayTypeNode) {
    auto [elemType, elemTypeName, _] = analyzeArrayType(arrayTypeNode);
    return elemType;
}

// ======================================================================
// Array and Member Access Generation
// ======================================================================

wasm::Expression* WasmCompiler::generateArrayAccess(std::shared_ptr<ASTNode> arrayAccess,
                                                    const FuncInfo& F) {
    if (!arrayAccess || arrayAccess->children.size() != 2) {
        std::cout << "⚠️ Malformed array access\n";
        return emitI32Const(0);
    }
    
    auto arrayRef = arrayAccess->children[0];
    auto indexExpr = arrayAccess->children[1];
    
    if (!arrayRef || !indexExpr) {
        return emitI32Const(0);
    }
    
    if (arrayRef->type == ASTNodeType::IDENTIFIER) {
        return generateSimpleArrayAccess(arrayRef, indexExpr, F);
    } else if (arrayRef->type == ASTNodeType::MEMBER_ACCESS) {
        return generateMemberArrayAccess(arrayRef, indexExpr, F);
    } else if (arrayRef->type == ASTNodeType::ARRAY_ACCESS) {
        std::cout << "⚠️ Multi-dimensional arrays not yet supported\n";
        return emitI32Const(0);
    } else {
        std::cout << "⚠️ Array access on unsupported node type: " << tname(arrayRef->type) << "\n";
        return emitI32Const(0);
    }
}

wasm::Expression* WasmCompiler::generateSimpleArrayAccess(std::shared_ptr<ASTNode> arrayRef,
                                                          std::shared_ptr<ASTNode> indexExpr,
                                                          const FuncInfo& F) {
    std::string arrayName = arrayRef->value;
    ArrayInfo arrayInfo;
    bool isGlobal = false;
    
    auto it = arrayInfos.find(arrayName);
    if (it != arrayInfos.end()) {
        arrayInfo = it->second;
    } else {
        auto globalIt = globalArrays.find(arrayName);
        if (globalIt != globalArrays.end()) {
            arrayInfo = globalIt->second;
            isGlobal = true;
        } else {
            std::cout << "⚠️ Unknown array: " << arrayName << "\n";
            return emitI32Const(0);
        }
    }
    
    wasm::Expression* baseAddr = isGlobal ? 
        builder.makeConst(wasm::Literal(arrayInfo.baseOffset)) :
        emitLocalGet(arrayName);
    
    wasm::Expression* index = generateExpression(indexExpr, F);
    
    int elemSize = (arrayInfo.elemType == wasm::Type::f64) ? 8 : 4;
    if (recordTypes.find(arrayInfo.elemTypeName) != recordTypes.end()) {
        elemSize = recordTypes[arrayInfo.elemTypeName].totalSize;
    }
    
    wasm::Expression* offset = builder.makeBinary(
        wasm::AddInt32,
        baseAddr,
        builder.makeBinary(
            wasm::MulInt32,
            index,
            builder.makeConst(wasm::Literal(elemSize))
        )
    );
    
    if (arrayInfo.elemType == wasm::Type::f64) {
        return builder.makeLoad(8, false, 0, 0, offset, wasm::Type::f64);
    } else {
        return builder.makeLoad(4, false, 0, 0, offset, wasm::Type::i32);
    }
}

wasm::Expression* WasmCompiler::generateMemberArrayAccess(std::shared_ptr<ASTNode> memberAccess,
                                                           std::shared_ptr<ASTNode> indexExpr,
                                                           const FuncInfo& F) {
    if (!memberAccess || memberAccess->children.size() < 1) {
        std::cout << "⚠️ Malformed member array access\n";
        return emitI32Const(0);
    }
    
    auto base = memberAccess->children[0];
    std::string fieldName = memberAccess->value;
    
    if (!base) {
        return emitI32Const(0);
    }
    
    if (base->type == ASTNodeType::IDENTIFIER) {
        std::string recordName = base->value;
        auto recordIt = recordVariables.find(recordName);
        if (recordIt == recordVariables.end()) {
            std::cout << "⚠️ Unknown record: " << recordName << "\n";
            return emitI32Const(0);
        }
        
        wasm::Expression* recordBase = emitRecordBaseAddress(recordName);
        
        auto recordTypeIt = recordTypes.find(recordIt->second.recordType);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Unknown record type: " << recordIt->second.recordType << "\n";
            return emitI32Const(0);
        }
        
        int fieldOffset = -1;
        wasm::Type elemType = wasm::Type::i32;
        
        for (const auto& field : recordTypeIt->second.fields) {
            if (field.first == fieldName) {
                fieldOffset = field.second.second;
                elemType = field.second.first;
                break;
            }
        }
        
        if (fieldOffset == -1) {
            std::cout << "⚠️ Unknown array field: " << fieldName << "\n";
            return emitI32Const(0);
        }
        
        wasm::Expression* fieldBase = builder.makeBinary(
            wasm::AddInt32,
            recordBase,
            builder.makeConst(wasm::Literal(fieldOffset))
        );
        
        wasm::Expression* index = generateExpression(indexExpr, F);
        int elemSize = (elemType == wasm::Type::f64) ? 8 : 4;
        
        wasm::Expression* addr = builder.makeBinary(
            wasm::AddInt32,
            fieldBase,
            builder.makeBinary(
                wasm::MulInt32,
                index,
                builder.makeConst(wasm::Literal(elemSize))
            )
        );
        
        if (elemType == wasm::Type::f64) {
            return builder.makeLoad(8, false, 0, 0, addr, wasm::Type::f64);
        } else {
            return builder.makeLoad(4, false, 0, 0, addr, wasm::Type::i32);
        }
    } else {
        std::cout << "⚠️ Unsupported base for member array access: " << tname(base->type) << "\n";
        return emitI32Const(0);
    }
}

wasm::Expression* WasmCompiler::generateArrayAssignment(std::shared_ptr<ASTNode> arrayAccess,
                                                        wasm::Expression* rhs,
                                                        const FuncInfo& F) {
    if (!arrayAccess || arrayAccess->children.size() != 2) {
        std::cout << "⚠️ Malformed array assignment\n";
        return builder.makeNop();
    }
    
    auto arrayRef = arrayAccess->children[0];
    auto indexExpr = arrayAccess->children[1];
    
    if (!arrayRef || !indexExpr) return builder.makeNop();
    
    std::string arrayName;
    ArrayInfo arrayInfo;
    
    if (arrayRef->type == ASTNodeType::IDENTIFIER) {
        arrayName = arrayRef->value;
        auto it = arrayInfos.find(arrayName);
        if (it == arrayInfos.end()) {
            auto globalIt = globalArrays.find(arrayName);
            if (globalIt == globalArrays.end()) {
                std::cout << "⚠️ Unknown array: " << arrayName << "\n";
                return builder.makeNop();
            }
            arrayInfo = globalIt->second;
        } else {
            arrayInfo = it->second;
        }
    } else {
        std::cout << "⚠️ Array assignment on unsupported node type\n";
        return builder.makeNop();
    }
    
    wasm::Expression* baseAddr = arrayInfos.count(arrayName) ? 
        emitLocalGet(arrayName) :
        builder.makeConst(wasm::Literal(arrayInfo.baseOffset));
    
    wasm::Expression* index = generateExpression(indexExpr, F);
    
    int elemSize = (arrayInfo.elemType == wasm::Type::f64) ? 8 : 4;
    if (recordTypes.find(arrayInfo.elemTypeName) != recordTypes.end()) {
        elemSize = recordTypes[arrayInfo.elemTypeName].totalSize;
    }
    
    wasm::Expression* addr = builder.makeBinary(
        wasm::AddInt32,
        baseAddr,
        builder.makeBinary(
            wasm::MulInt32,
            index,
            builder.makeConst(wasm::Literal(elemSize))
        )
    );
    
    if (rhs) {
        if (arrayInfo.elemType == wasm::Type::f64) {
            return builder.makeStore(8, 0, 0, addr, rhs, wasm::Type::f64);
        } else {
            return builder.makeStore(4, 0, 0, addr, rhs, wasm::Type::i32);
        }
    }
    
    return addr;
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
            
            auto recordBody = n->children[0]->children[0];
            if (recordBody) {
                for (auto& field : recordBody->children) {
                    if (!field || field->type != ASTNodeType::VAR_DECL) continue;
                    
                    std::string fieldName = field->value;
                    auto [fieldType, fieldSize] = analyzeFieldType(field);
                    
                    rec.fields.push_back({fieldName, {fieldType, rec.totalSize}});
                    
                    if (field->children.size() > 0 && field->children[0] &&
                        field->children[0]->type == ASTNodeType::ARRAY_TYPE) {
                        auto [elemType, elemTypeName, arrSize] = analyzeArrayType(field->children[0]);
                        rec.arrayFieldElementTypes[fieldName] = elemTypeName;
                    }
                    
                    rec.totalSize += fieldSize;
                }
            }
            
            recordTypes[rec.name] = rec;
            std::cout << "📋 Record '" << rec.name << "': " 
                      << rec.totalSize << " bytes" << std::endl;
        }
    }
}

std::pair<wasm::Type, int> WasmCompiler::analyzeFieldType(std::shared_ptr<ASTNode> fieldDecl) {
    if (!fieldDecl || fieldDecl->children.empty()) return {wasm::Type::i32, 4};
    
    auto typeNode = fieldDecl->children[0];
    
    if (typeNode->type == ASTNodeType::PRIMITIVE_TYPE) {
        if (typeNode->value == "integer" || typeNode->value == "boolean") {
            return {wasm::Type::i32, 4};
        } else if (typeNode->value == "real") {
            return {wasm::Type::f64, 8};
        }
    } 
    else if (typeNode->type == ASTNodeType::ARRAY_TYPE) {
        auto [elemType, _, size] = analyzeArrayType(typeNode);
        int elemSize = (elemType == wasm::Type::f64) ? 8 : 4;
        return {elemType, size * elemSize};
    }
    else if (typeNode->type == ASTNodeType::USER_TYPE) {
        auto it = recordTypes.find(typeNode->value);
        if (it != recordTypes.end()) {
            return {wasm::Type::i32, it->second.totalSize};
        }
    }
    
    return {wasm::Type::i32, 4};
}

wasm::Expression* WasmCompiler::generateMemberAccess(std::shared_ptr<ASTNode> memberAccess,
                                                    const FuncInfo& F) {
    if (!memberAccess || memberAccess->children.size() < 1) {
        std::cout << "⚠️ Malformed member access\n";
        return emitI32Const(0);
    }
    
    auto base = memberAccess->children[0];
    std::string fieldName = memberAccess->value;
    
    if (!base) {
        std::cout << "⚠️ Member access with null base\n";
        return emitI32Const(0);
    }
    
    wasm::Type fieldType = wasm::Type::i32;
    
    if (base->type == ASTNodeType::IDENTIFIER) {
        std::string recordName = base->value;
        auto recordIt = recordVariables.find(recordName);
        if (recordIt == recordVariables.end()) {
            std::cout << "⚠️ Unknown record variable: " << recordName << "\n";
            return emitI32Const(0);
        }
        
        auto recordTypeIt = recordTypes.find(recordIt->second.recordType);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Unknown record type: " << recordIt->second.recordType << "\n";
            return emitI32Const(0);
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
            return emitI32Const(0);
        }
        
        wasm::Expression* recordBase = emitRecordBaseAddress(recordName);
        wasm::Expression* addr = builder.makeBinary(
            wasm::AddInt32,
            recordBase,
            builder.makeConst(wasm::Literal(fieldOffset))
        );
        
        if (fieldType == wasm::Type::f64) {
            return builder.makeLoad(8, false, 0, 0, addr, wasm::Type::f64);
        } else {
            return builder.makeLoad(4, false, 0, 0, addr, wasm::Type::i32);
        }
    } else {
        std::cout << "⚠️ Member access on unsupported base type: " << tname(base->type) << "\n";
        return emitI32Const(0);
    }
}

wasm::Expression* WasmCompiler::generateMemberAssignment(std::shared_ptr<ASTNode> memberAccess,
                                                         wasm::Expression* rhs,
                                                         const FuncInfo& F) {
    if (!memberAccess || memberAccess->children.size() < 1) {
        std::cout << "⚠️ Malformed member assignment\n";
        return builder.makeNop();
    }
    
    wasm::Type fieldType = wasm::Type::i32;
    auto base = memberAccess->children[0];
    std::string fieldName = memberAccess->value;
    
    if (base->type == ASTNodeType::IDENTIFIER) {
        std::string recordName = base->value;
        auto recordIt = recordVariables.find(recordName);
        if (recordIt == recordVariables.end()) {
            std::cout << "⚠️ Unknown record variable: " << recordName << "\n";
            return builder.makeNop();
        }
        
        auto recordTypeIt = recordTypes.find(recordIt->second.recordType);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Unknown record type: " << recordIt->second.recordType << "\n";
            return builder.makeNop();
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
            return builder.makeNop();
        }
        
        wasm::Expression* recordBase = emitRecordBaseAddress(recordName);
        wasm::Expression* addr = builder.makeBinary(
            wasm::AddInt32,
            recordBase,
            builder.makeConst(wasm::Literal(fieldOffset))
        );
        
        if (rhs) {
            if (fieldType == wasm::Type::f64) {
                return builder.makeStore(8, 0, 0, addr, rhs, wasm::Type::f64);
            } else {
                return builder.makeStore(4, 0, 0, addr, rhs, wasm::Type::i32);
            }
        }
        
        return addr;
    } else {
        std::cout << "⚠️ Member assignment on unsupported base type: " << tname(base->type) << "\n";
        return builder.makeNop();
    }
}

// Placeholder implementations for complex array/member access resolution
std::tuple<int, wasm::Type, int> WasmCompiler::resolveArrayMember(std::shared_ptr<ASTNode> memberAccess,
                                                                   const FuncInfo& F) {
    return {-1, wasm::Type::i32, 0};
}

wasm::Expression* WasmCompiler::generateArrayAccessForRecord(std::shared_ptr<ASTNode> arrayAccess,
                                                              const FuncInfo& F) {
    return emitI32Const(0);
}

std::tuple<int, wasm::Type, int> WasmCompiler::resolveArrayAccessMember(std::shared_ptr<ASTNode> arrayAccess,
                                                                       const std::string& fieldName,
                                                                       const FuncInfo& F) {
    return {-1, wasm::Type::i32, 0};
}

// ======================================================================
// Type System
// ======================================================================

ValueType WasmCompiler::getExpressionType(std::shared_ptr<ASTNode> expr, const FuncInfo& F) {
    if (!expr) return ValueType::UNKNOWN;
    
    switch (expr->type) {
        case ASTNodeType::LITERAL_INT:
            return ValueType::INTEGER;
        case ASTNodeType::LITERAL_REAL:
            return ValueType::REAL;
        case ASTNodeType::LITERAL_BOOL:
            return ValueType::BOOLEAN;
        case ASTNodeType::IDENTIFIER: {
            std::string varName = expr->value;
            
            auto globalIt = globalVars.find(varName);
            if (globalIt != globalVars.end()) {
                if (globalIt->second.type == wasm::Type::f64) return ValueType::REAL;
                return ValueType::INTEGER;
            }
            
            if (arrayInfos.find(varName) != arrayInfos.end() ||
                recordVariables.find(varName) != recordVariables.end()) {
                return ValueType::INTEGER;
            }
            
            return ValueType::INTEGER;
        }
        case ASTNodeType::BINARY_OP: {
            if (expr->children.size() < 2) return ValueType::INTEGER;
            
            const std::string& op = expr->value;
            bool isComparison = (op == "<" || op == "<=" || op == ">" || op == ">=" || 
                                op == "=" || op == "/=");
            
            if (isComparison) {
                return ValueType::BOOLEAN;
            }
            
            ValueType leftType = getExpressionType(expr->children[0], F);
            ValueType rightType = getExpressionType(expr->children[1], F);
            
            if (leftType == ValueType::REAL || rightType == ValueType::REAL) {
                return ValueType::REAL;
            } else if (leftType == ValueType::INTEGER || rightType == ValueType::INTEGER) {
                return ValueType::INTEGER;
            } else {
                return ValueType::BOOLEAN;
            }
        }
        case ASTNodeType::UNARY_OP: {
            if (expr->children.empty()) return ValueType::INTEGER;
            return getExpressionType(expr->children[0], F);
        }
        case ASTNodeType::ROUTINE_CALL: {
            auto funcIt = funcIndexByName.find(expr->value);
            if (funcIt != funcIndexByName.end() && funcIt->second < funcs.size()) {
                auto& calledFunc = funcs[funcIt->second];
                if (!calledFunc.resultTypes.empty()) {
                    wasm::Type wasmType = calledFunc.resultTypes[0];
                    if (wasmType == wasm::Type::f64) return ValueType::REAL;
                    return ValueType::INTEGER;
                }
            }
            return ValueType::INTEGER;
        }
        case ASTNodeType::ARRAY_ACCESS: {
            if (expr->children.size() > 0 && expr->children[0]) {
                auto arrayRef = expr->children[0];
                if (arrayRef->type == ASTNodeType::IDENTIFIER) {
                    auto arrayIt = arrayInfos.find(arrayRef->value);
                    if (arrayIt != arrayInfos.end()) {
                        if (arrayIt->second.elemType == wasm::Type::f64) return ValueType::REAL;
                        return ValueType::INTEGER;
                    }
                }
            }
            return ValueType::INTEGER;
        }
        case ASTNodeType::MEMBER_ACCESS: {
            if (expr->children.size() > 0 && expr->children[0]) {
                auto base = expr->children[0];
                std::string fieldName = expr->value;
                
                if (base->type == ASTNodeType::IDENTIFIER) {
                    auto recordIt = recordVariables.find(base->value);
                    if (recordIt != recordVariables.end()) {
                        auto recordTypeIt = recordTypes.find(recordIt->second.recordType);
                        if (recordTypeIt != recordTypes.end()) {
                            for (const auto& field : recordTypeIt->second.fields) {
                                if (field.first == fieldName) {
                                    if (field.second.first == wasm::Type::f64) return ValueType::REAL;
                                    return ValueType::INTEGER;
                                }
                            }
                        }
                    }
                }
            }
            return ValueType::INTEGER;
        }
        case ASTNodeType::SIZE_EXPRESSION:
            return ValueType::INTEGER;
        default:
            return ValueType::INTEGER;
    }
}

wasm::Expression* WasmCompiler::emitTypeConversion(wasm::Expression* expr, ValueType fromType, ValueType toType) {
    if (fromType == toType) return expr;
    
    if (fromType == ValueType::INTEGER && toType == ValueType::REAL) {
        return builder.makeUnary(wasm::ConvertSInt32ToFloat64, expr);
    }
    else if (fromType == ValueType::REAL && toType == ValueType::INTEGER) {
        wasm::Expression* addHalf = builder.makeBinary(wasm::AddFloat64, expr, emitF64Const(0.5));
        return builder.makeUnary(wasm::TruncateFloat64ToSInt32, addHalf);
    }
    else if (fromType == ValueType::INTEGER && toType == ValueType::BOOLEAN) {
        return builder.makeBinary(wasm::NeInt32, expr, emitI32Const(0));
    }
    else if (fromType == ValueType::REAL && toType == ValueType::BOOLEAN) {
        std::cerr << "❌ INTERNAL ERROR: REAL → BOOLEAN conversion should be illegal!" << std::endl;
        return builder.makeUnreachable();
    }
    else if (fromType == ValueType::BOOLEAN && toType == ValueType::INTEGER) {
        return expr; // No conversion needed
    }
    else if (fromType == ValueType::BOOLEAN && toType == ValueType::REAL) {
        return builder.makeUnary(wasm::ConvertSInt32ToFloat64, expr);
    }
    
    return expr;
}

bool WasmCompiler::validateAssignmentConversion(ValueType fromType, ValueType toType, const std::string& context) {
    if (fromType == toType) {
        return true;
    }
    
    if (fromType == ValueType::REAL && toType == ValueType::BOOLEAN) {
        std::cerr << "❌ Type error in " << context << ": Cannot assign real to boolean (illegal conversion)" << std::endl;
        return false;
    }
    
    return true;
}

wasm::Expression* WasmCompiler::generatePrintStatement(std::shared_ptr<ASTNode> printStmt,
                                                      const FuncInfo& F) {
    if (!printStmt || printStmt->children.empty()) return builder.makeNop();
    
    auto exprList = printStmt->children[0];
    if (!exprList || exprList->type != ASTNodeType::EXPRESSION_LIST) return builder.makeNop();
    
    std::vector<wasm::Expression*> drops;
    for (auto& expr : exprList->children) {
        if (!expr) continue;
        
        if (expr->type == ASTNodeType::LITERAL_STRING) {
            std::cout << "  📝 PRINT: \"" << expr->value << "\"\n";
        } else {
            wasm::Expression* exprVal = generateExpression(expr, F);
            drops.push_back(builder.makeDrop(exprVal));
        }
    }
    
    if (drops.empty()) return builder.makeNop();
    if (drops.size() == 1) return drops[0];
    return builder.makeBlock("", drops);
}

