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
    
    // Track print statement usage - check if any function uses print statements
    hasPrintStatements = false;
    for (auto& F : funcs) {
        std::shared_ptr<ASTNode> bodyNode = nullptr;
        for (auto& ch : F.node->children) {
            if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
        }
        if (bodyNode) {
            // Recursively check for print statements in the body
            std::function<bool(std::shared_ptr<ASTNode>)> checkForPrint = [&](std::shared_ptr<ASTNode> node) -> bool {
                if (!node) return false;
                if (node->type == ASTNodeType::PRINT_STMT) return true;
                for (auto& child : node->children) {
                    if (checkForPrint(child)) return true;
                }
                return false;
            };
            if (checkForPrint(bodyNode)) {
                hasPrintStatements = true;
                break;
            }
        }
    }
    
    // Only add print imports if print statements are actually used
    if (hasPrintStatements) {
        addPrintImports();
    }

    // Create all functions
    for (auto& F : funcs) {
        resetLocals();
        addParametersToLocals(F);
        
        auto locals = analyzeLocalVariables(F);
        wasm::Function* func = new wasm::Function();
        func->name = F.name;
        // Create function signature
        wasm::Signature sig;
        sig.params = F.paramTypes;
        // For return type: if empty, use none; otherwise use the first result type
        // Binaryen expects sig.results to be a Type (which can be a tuple for multiple returns)
        // For a single return, we use the type directly
        if (F.resultTypes.empty()) {
            sig.results = wasm::Type::none;
        } else {
            sig.results = F.resultTypes[0];
        }
        func->type = wasm::Type(sig, wasm::NonNullable, wasm::Exact);
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
                wasm::Block* outerBlock = builder.makeBlock("", bodyExprs);
                // If the function body ends with a return, the outer block should have the return type
                // The return statement provides the value, so the block type is the return type
                wasm::Type outerBlockType;
                if (!F.resultTypes.empty()) {
                    // Function has a return type - outer block should match it
                    outerBlockType = F.resultTypes[0];
                } else {
                    // No return type - outer block is none (void)
                    outerBlockType = wasm::Type::none;
                }
                outerBlock->finalize(outerBlockType);
                func->body = outerBlock;
            } else {
                func->body = funcBody;
            }
        } else if (!bodyExprs.empty()) {
            wasm::Block* outerBlock = builder.makeBlock("", bodyExprs);
            if (!F.resultTypes.empty()) {
                outerBlock->finalize(F.resultTypes[0]);
            } else {
                outerBlock->finalize(wasm::Type::none);
            }
            func->body = outerBlock;
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
        wasm::Export* exp = new wasm::Export("main", wasm::ExternalKind::Function, wasm::Name("main"));
        module->addExport(exp);
    }
    
    // Optimize module
    wasm::PassOptions options;
    wasm::PassRunner passRunner(module.get(), options);
    // Add optimization passes - skip the ones that cause validation errors with record returns
    passRunner.add("merge-blocks");
    passRunner.add("remove-unused-brs");
    passRunner.add("remove-unused-names");
    passRunner.add("vacuum");
    passRunner.add("reorder-locals");
    passRunner.add("coalesce-locals");
    // Skip these as they cause "expected i32 but nothing on stack" validation errors:
    // - "simplify-locals" - causes validation error when functions return records
    // - "optimize-instructions" - causes validation error when functions return records
    // - "code-folding" - causes validation error when functions return records
    // - "inlining" - might also cause issues
    // passRunner.add("simplify-locals");
    // passRunner.add("optimize-instructions");
    // passRunner.add("code-folding");
    // passRunner.add("inlining");
    passRunner.run();
    
    // Write to file
    wasm::ModuleWriter writer(options);
    writer.setBinary(true);
    writer.write(*module, filename);
    
    std::cout << "✅ WROTE WASM module using Binaryen\n";
    std::cout << "💡 You can run it with: wasmtime --invoke main " << filename << "\n";
    return true;
}

// ======================================================================
// Collect routines and signatures
// ======================================================================

// Helper to collect type definitions (including local type aliases)
void collectTypeDefinitionsRecursive(std::shared_ptr<ASTNode> node, 
                                      std::unordered_map<std::string, std::shared_ptr<ASTNode>>& typeDefs) {
    if (!node) return;
    if (node->type == ASTNodeType::TYPE_DECL) {
        std::string typeName = node->value;
        if (node->children.size() > 0 && node->children[0]) {
            typeDefs[typeName] = node->children[0];
        }
    }
    for (auto& child : node->children) {
        collectTypeDefinitionsRecursive(child, typeDefs);
    }
}

bool WasmCompiler::collectFunctions(std::shared_ptr<ASTNode> program) {
    funcs.clear();
    funcIndexByName.clear();
    globalVars.clear();
    globalArrays.clear();
    globalRecordVariables.clear();
    recordVariables.clear();
    typeDefinitions.clear();
    globalMemoryOffset = 0;

    collectRecordTypes(program);
    collectTypeDefinitionsRecursive(program, typeDefinitions);
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
                 ch->type == ASTNodeType::USER_TYPE ||
                 ch->type == ASTNodeType::ARRAY_TYPE) retType = ch;
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
        if (retType->type == ASTNodeType::USER_TYPE) {
            // User types (records) are returned as i32 (pointer)
            F.resultTypes.push_back(wasm::Type::i32);
        } else if (retType->type == ASTNodeType::ARRAY_TYPE) {
            // Arrays are returned as i32 (pointer)
            F.resultTypes.push_back(wasm::Type::i32);
        } else {
            F.resultTypes.push_back(mapPrimitiveToWasm(retType->value));
        }
    } else {
        // No return type - function returns void (none)
        // Don't add any result types
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
    localVarTypes.clear();  // Clear local variable types
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
        localVarIndices[paramName] = idx;
        
        wasm::Type paramType = wasm::Type::i32;
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
                paramType = wasm::Type::i32;  // Arrays are passed as i32 (pointer)
                break;
            } else if (pc->type == ASTNodeType::PRIMITIVE_TYPE) {
                paramType = mapPrimitiveToWasm(pc->value);
            } else if (pc->type == ASTNodeType::USER_TYPE) {
                paramType = wasm::Type::i32;  // Records are passed as i32 (pointer)
            }
        }
        localVarTypes[paramName] = paramType;
        idx++;
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

// Helper to collect all loop variables from FOR_LOOP nodes
void WasmCompiler::collectAllLoopVariables(std::shared_ptr<ASTNode> node,
                                            std::set<std::string>& loopVars,
                                            bool insideBody) {
    if (!node) return;
    
    bool nowInsideBody = insideBody || (node->type == ASTNodeType::BODY);
    
    if (node->type == ASTNodeType::FOR_LOOP && nowInsideBody) {
        const std::string& loopVar = node->value;
        if (!loopVar.empty()) {
            std::cout << "🔍 Found loop variable: " << loopVar << std::endl;
            loopVars.insert(loopVar);
        }
    }
    
    if (node->type == ASTNodeType::RECORD_TYPE || node->type == ASTNodeType::TYPE_DECL) {
        return;
    }
    
    for (auto& child : node->children) {
        collectAllLoopVariables(child, loopVars, nowInsideBody);
    }
}

std::vector<wasm::Type> WasmCompiler::analyzeLocalVariables(const FuncInfo& F) {
    std::vector<wasm::Type> locals;
    std::cout << "🔍 analyzeLocalVariables: Starting analysis for function " << F.name << std::endl;

    std::shared_ptr<ASTNode> bodyNode = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
    }
    if (!bodyNode) {
        std::cout << "⚠️ No body node found, adding 2 temp locals" << std::endl;
        locals.push_back(wasm::Type::i32);
        locals.push_back(wasm::Type::i32);
        return locals;
    }

    std::vector<std::shared_ptr<ASTNode>> allVarDecls;
    collectAllVariableDeclarations(bodyNode, allVarDecls, true);
    std::cout << "📊 Found " << allVarDecls.size() << " variable declarations" << std::endl;

    // Collect all loop variables
    std::set<std::string> loopVars;
    collectAllLoopVariables(bodyNode, loopVars, true);
    std::cout << "📊 Found " << loopVars.size() << " loop variables: ";
    for (const auto& lv : loopVars) {
        std::cout << lv << " ";
    }
    std::cout << std::endl;

    // First, register all loop variables as locals (they're always i32)
    for (const auto& loopVar : loopVars) {
        if (localVarIndices.count(loopVar)) {
            std::cout << "  ⚠️ Loop variable " << loopVar << " already registered, skipping" << std::endl;
            continue;
        }
        std::cout << "  ✅ Registering loop variable: " << loopVar << " as local index " << nextLocalIndex << std::endl;
        localVarIndices[loopVar] = nextLocalIndex++;
        localVarTypes[loopVar] = wasm::Type::i32;
        locals.push_back(wasm::Type::i32);
    }

    for (auto& s : allVarDecls) {
        if (!s || s->type != ASTNodeType::VAR_DECL) continue;
        const std::string& name = s->value;
        if (localVarIndices.count(name)) continue;
        
        if (s->children.size() >= 1 && s->children[0]) {
            auto typeNode = s->children[0];
            if (typeNode->type == ASTNodeType::USER_TYPE) {
                // First check if it's a record type
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
                // Check if it's a type alias (e.g., "type myId is real")
                auto resolvedType = resolveTypeAlias(typeNode->value);
                if (resolvedType && resolvedType->type == ASTNodeType::PRIMITIVE_TYPE) {
                    // It's a type alias to a primitive type
                    localVarIndices[name] = nextLocalIndex++;
                    wasm::Type wt = mapPrimitiveToWasm(resolvedType->value);
                    localVarTypes[name] = wt;
                    locals.push_back(wt);
                    continue;
                }
                // Unknown USER_TYPE - treat as i32
                localVarIndices[name] = nextLocalIndex++;
                localVarTypes[name] = wasm::Type::i32;
                locals.push_back(wasm::Type::i32);
                continue;
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
                localVarTypes[name] = wt;  // Track the type
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
    
    // Check if it's a record variable (stored in memory, local variable holds address)
    auto recordIt = recordVariables.find(name);
    if (recordIt != recordVariables.end()) {
        // Initialize the local variable with the record's memory address
        wasm::Expression* addr = builder.makeConst(wasm::Literal(recordIt->second.baseOffset));
        body.push_back(emitLocalSet(name, addr));
        return;
    }
    
    // Regular variable declaration - initialize to 0 if no initializer
    wasm::Expression* initExpr = nullptr;
    if (decl->children.size() >= 2 && decl->children[1]) {
        // Has initializer
        auto initializer = decl->children[1];
        initExpr = generateExpression(initializer, F);
    } else {
        // No initializer - initialize to 0 based on type
        wasm::Type varType = wasm::Type::i32;
        if (!decl->children.empty() && decl->children[0]) {
            auto typeNode = decl->children[0];
            if (typeNode->type == ASTNodeType::PRIMITIVE_TYPE) {
                varType = mapPrimitiveToWasm(typeNode->value);
            } else if (typeNode->type == ASTNodeType::USER_TYPE) {
                auto resolvedType = resolveTypeAlias(typeNode->value);
                if (resolvedType && resolvedType->type == ASTNodeType::PRIMITIVE_TYPE) {
                    varType = mapPrimitiveToWasm(resolvedType->value);
                }
            }
        }
        if (varType == wasm::Type::f64) {
            initExpr = emitF64Const(0.0);
        } else {
            initExpr = emitI32Const(0);
        }
        std::cout << "  🔧 Variable '" << name << "' has no initializer, initializing to 0" << std::endl;
    }
    
    if (!initExpr) {
        std::cout << "  ⚠️ Failed to create initializer for variable '" << name << "'" << std::endl;
        return;
    }
    
    // Determine source and target types for conversion
    ValueType sourceType = ValueType::INTEGER; // Default
    if (decl->children.size() >= 2 && decl->children[1]) {
        sourceType = getExpressionType(decl->children[1], F);
    } else {
        // No initializer - source type matches target type (both 0)
        if (initExpr->type == wasm::Type::f64) {
            sourceType = ValueType::REAL;
        } else {
            sourceType = ValueType::INTEGER;
        }
    }
    ValueType targetType = ValueType::UNKNOWN;
    if (!decl->children.empty() && decl->children[0]) {
        auto typeNode = decl->children[0];
        if (typeNode->type == ASTNodeType::PRIMITIVE_TYPE) {
            if (typeNode->value == "integer") targetType = ValueType::INTEGER;
            else if (typeNode->value == "real") targetType = ValueType::REAL;
            else if (typeNode->value == "boolean") targetType = ValueType::BOOLEAN;
        } else if (typeNode->type == ASTNodeType::USER_TYPE) {
            // Resolve type alias (e.g., "myId" -> "real")
            auto resolvedType = resolveTypeAlias(typeNode->value);
            if (resolvedType && resolvedType->type == ASTNodeType::PRIMITIVE_TYPE) {
                if (resolvedType->value == "integer") targetType = ValueType::INTEGER;
                else if (resolvedType->value == "real") targetType = ValueType::REAL;
                else if (resolvedType->value == "boolean") targetType = ValueType::BOOLEAN;
            }
        }
    }
    
    if (targetType != ValueType::UNKNOWN && sourceType != targetType) {
        initExpr = emitTypeConversion(initExpr, sourceType, targetType);
    }
    
    body.push_back(emitLocalSet(name, initExpr));
}

// Helper function to inline record copy blocks
void inlineRecordCopyBlock(wasm::Expression* expr, std::vector<wasm::Expression*>& bodyExprs) {
    if (expr->is<wasm::Block>()) {
        wasm::Block* block = expr->cast<wasm::Block>();
        if (block->name == "copy_record" || block->name == "__record_copy") {
            // Inline all expressions from the copy block
            for (auto* e : block->list) {
                bodyExprs.push_back(e);
            }
            return;
        }
    }
    // Not a copy block, add as is
    bodyExprs.push_back(expr);
}

wasm::Expression* WasmCompiler::generateFunctionBody(const FuncInfo& F) {
    std::cout << "🔧 Generating function body for: " << F.name << std::endl;
    std::shared_ptr<ASTNode> bodyNode = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
    }
    if (!bodyNode) {
        std::cout << "⚠️ No body node found for " << F.name << std::endl;
        return nullptr;
    }

    std::vector<wasm::Expression*> bodyExprs;
    bool hasReturn = false;
    
    std::cout << "🔧 Function " << F.name << " has " << bodyNode->children.size() << " body items" << std::endl;
    for (auto& s : bodyNode->children) {
        if (!s) continue;
        std::cout << "🔧 Processing statement type: " << tname(s->type) << std::endl;
        switch (s->type) {
            case ASTNodeType::VAR_DECL: {
                std::vector<wasm::Expression*> varBody;
                const std::string& varName = s->value;
                auto recordIt = recordVariables.find(varName);
                if (recordIt != recordVariables.end()) {
                    // Record variable - initialize local with address (whether or not it has an initializer)
                    // For records, the "initializer" in the AST is not used - we always initialize with the address
                    wasm::Expression* addr = builder.makeConst(wasm::Literal(recordIt->second.baseOffset));
                    varBody.push_back(emitLocalSet(varName, addr));
                    std::cout << "🔧 Initializing record variable '" << varName 
                              << "' with address " << recordIt->second.baseOffset << std::endl;
                } else {
                    generateVarDeclaration(varBody, s, F);
                }
                bodyExprs.insert(bodyExprs.end(), varBody.begin(), varBody.end());
                break;
            }
            case ASTNodeType::ASSIGNMENT: {
                wasm::Expression* assignExpr = generateAssignment(s, F);
                // Use helper to inline record copy blocks
                // Record copy blocks should have type none (they're just side effects)
                if (assignExpr->type == wasm::Type::none || assignExpr->type == wasm::Type::unreachable) {
                    inlineRecordCopyBlock(assignExpr, bodyExprs);
                } else {
                    // Assignment returned a value - drop it (we only care about side effects)
                    inlineRecordCopyBlock(builder.makeDrop(assignExpr), bodyExprs);
                }
                break;
            }
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
            case ASTNodeType::TYPE_DECL:
                // Type declarations inside function bodies don't generate code
                // They're handled during semantic analysis
                break;
            case ASTNodeType::IDENTIFIER:
            case ASTNodeType::BINARY_OP:
            case ASTNodeType::UNARY_OP:
            case ASTNodeType::LITERAL_INT:
            case ASTNodeType::LITERAL_REAL:
            case ASTNodeType::LITERAL_BOOL:
            case ASTNodeType::LITERAL_STRING:
            case ASTNodeType::ARRAY_ACCESS:
            case ASTNodeType::MEMBER_ACCESS:
                // Standalone expressions as statements - evaluate and drop the result
                // This handles cases like: e; or a = 12; (though these shouldn't normally appear)
                bodyExprs.push_back(builder.makeDrop(generateExpression(s, F)));
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
    
    // Create a block for the function body
    // If the last statement is a return, the block type should be unreachable
    // Otherwise, the block type should match the function return type
    wasm::Type blockType;
    if (hasReturn) {
        // Block ends with return - the block itself should have the return type
        // not unreachable, because the return statement provides the value
        if (!F.resultTypes.empty()) {
            blockType = F.resultTypes[0];
        } else {
            blockType = wasm::Type::unreachable;
        }
    } else if (!F.resultTypes.empty()) {
        // Function falls through - block type matches return type
        blockType = F.resultTypes[0];
    } else {
        // No return type - block is none (void)
        blockType = wasm::Type::none;
    }
    wasm::Block* bodyBlock = builder.makeBlock("", bodyExprs);
    bodyBlock->finalize(blockType);
    return bodyBlock;
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
    
    std::string lhsName = (lhs->type == ASTNodeType::IDENTIFIER) ? lhs->value : "?";
    std::cout << "🔧 generateAssignment: lhs=" << lhsName 
              << " (type=" << tname(lhs->type) << "), rhs type=" << tname(rhs->type) << std::endl;
    
    ValueType targetType = getExpressionType(lhs, F);
    ValueType sourceType = getExpressionType(rhs, F);
    
    if (!validateAssignmentConversion(sourceType, targetType, "assignment")) {
        std::cerr << "❌ Type error: Cannot assign " << (int)sourceType << " to " << (int)targetType << std::endl;
        return builder.makeNop();
    }
    
    wasm::Expression* rhsExpr = generateExpression(rhs, F);
    
    // Ensure rhsExpr is properly finalized if it's a function call
    if (rhs->type == ASTNodeType::ROUTINE_CALL) {
        if (rhsExpr) {
            // Finalize the call to ensure it's properly typed
            rhsExpr->finalize();
        }
    }
    
    // Check if we're assigning a record return value to a record variable
    if (lhs->type == ASTNodeType::IDENTIFIER) {
        std::string lhsName = lhs->value;
        auto recordIt = recordVariables.find(lhsName);
        
        // If lhs is a record variable and rhs is a function call returning a record
        if (recordIt != recordVariables.end() && rhs->type == ASTNodeType::ROUTINE_CALL) {
            // Get the function return type
            auto funcIt = funcIndexByName.find(rhs->value);
            if (funcIt != funcIndexByName.end() && funcIt->second < funcs.size()) {
                auto& calledFunc = funcs[funcIt->second];
                if (!calledFunc.resultTypes.empty() && calledFunc.resultTypes[0] == wasm::Type::i32) {
                    // Function returns a record (i32 pointer)
                    // We need to copy the record data from source to destination
                    auto recordTypeIt = recordTypes.find(recordIt->second.recordType);
                    if (recordTypeIt != recordTypes.end()) {
                        int recordSize = recordTypeIt->second.totalSize;
                        
                        // Get source address (function return value - pointer to record)
                        // The function call returns i32, which we use as the source address
                        // We need to store it in a temp local to avoid stack issues when using it multiple times
                        // The temp locals are the last 2 locals added in analyzeLocalVariables
                        // Calculate: temp local index = num params + num regular locals
                        // Since nextLocalIndex = num params + num regular locals + 2, we use nextLocalIndex - 2
                        wasm::Index tempLocalIndex = nextLocalIndex - 2;
                        
                        // Debug: verify the calculation
                        std::cout << "  🔧 Using temp local index " << tempLocalIndex 
                                  << " (nextLocalIndex=" << nextLocalIndex 
                                  << ", num params=" << F.paramTypes.size() << ")" << std::endl;
                        
                        // Store the function call result in the temp local
                        // This ensures the function is called once and its result is reused
                        // Make sure rhsExpr (the function call) is properly finalized and returns i32
                        if (!rhsExpr || rhsExpr->type != wasm::Type::i32) {
                            std::cout << "  ❌ Error: Function call result is not i32! Type: " 
                                      << (rhsExpr ? rhsExpr->type : wasm::Type::none) << std::endl;
                            return builder.makeNop();
                        }
                        
                        // Get destination address (local record variable)
                        // For local record variables, the local variable holds the address
                        // We need to use the local variable value (which is the address), not the constant base offset
                        wasm::Expression* dstAddr;
                        auto localIt = localVarIndices.find(lhsName);
                        auto recordIt2 = recordVariables.find(lhsName);
                        if (localIt != localVarIndices.end() && recordIt2 != recordVariables.end()) {
                            // It's a local record variable - get address from local variable
                            dstAddr = builder.makeLocalGet(localIt->second, wasm::Type::i32);
                        } else {
                            dstAddr = emitRecordBaseAddress(lhsName);
                        }
                        
                        // Copy record byte by byte (or in chunks)
                        // Store the function call result in a temp local first to ensure it's properly evaluated
                        std::vector<wasm::Expression*> copyExprs;
                        // Store the function call result in temp local - this ensures it's evaluated once
                        wasm::Expression* storeSrc = builder.makeLocalSet(tempLocalIndex, rhsExpr);
                        copyExprs.push_back(storeSrc);
                        wasm::Expression* srcAddr = builder.makeLocalGet(tempLocalIndex, wasm::Type::i32);
                        // Copy in 4-byte chunks
                        for (int offset = 0; offset < recordSize; offset += 4) {
                            int bytesToCopy = std::min(4, recordSize - offset);
                            // Use the stored local value
                            wasm::Expression* srcOffset = builder.makeBinary(
                                wasm::AddInt32, srcAddr, builder.makeConst(wasm::Literal(offset))
                            );
                            wasm::Expression* dstOffset = builder.makeBinary(
                                wasm::AddInt32, dstAddr, builder.makeConst(wasm::Literal(offset))
                            );
                            
                            wasm::Expression* value = builder.makeLoad(
                                bytesToCopy, false, 0, 0, srcOffset, 
                                bytesToCopy == 8 ? wasm::Type::f64 : wasm::Type::i32, 
                                wasm::Name("memory")
                            );
                            copyExprs.push_back(builder.makeStore(
                                bytesToCopy, 0, 0, dstOffset, value,
                                bytesToCopy == 8 ? wasm::Type::f64 : wasm::Type::i32,
                                wasm::Name("memory")
                            ));
                        }
                        
                        if (copyExprs.empty()) {
                            return builder.makeNop();
                        } else {
                            // Instead of creating a block, return the first expression
                            // and mark that we need to inline the rest
                            // This avoids Binaryen optimizer issues with nested blocks
                            // We'll handle the inlining in generateFunctionBody
                            // Ensure the copy block is properly finalized with type none
                            // This is a side-effect-only block (copying memory)
                            wasm::Block* copyBlock = builder.makeBlock("copy_record", copyExprs);
                            copyBlock->finalize(wasm::Type::none);
                            // Make sure all expressions in the block are properly finalized
                            for (auto* expr : copyBlock->list) {
                                if (expr && expr->type == wasm::Type::none) {
                                    expr->finalize();
                                }
                            }
                            return copyBlock;
                        }
                    }
                }
            }
        }
        
        rhsExpr = emitTypeConversion(rhsExpr, sourceType, targetType);
        return emitLocalSet(lhs->value, rhsExpr);
    } else if (lhs->type == ASTNodeType::ARRAY_ACCESS) {
        wasm::Type elemType = wasm::Type::i32;
        auto arrayRef = lhs->children[0];
        
        // Determine element type based on array reference
        if (arrayRef->type == ASTNodeType::IDENTIFIER) {
            auto it = arrayInfos.find(arrayRef->value);
            if (it != arrayInfos.end()) {
                elemType = it->second.elemType;
            } else {
                auto globalIt = globalArrays.find(arrayRef->value);
                if (globalIt != globalArrays.end()) {
                    elemType = globalIt->second.elemType;
                }
            }
        } else if (arrayRef->type == ASTNodeType::MEMBER_ACCESS) {
            // Handle nested array access: employees[1].name[32]
            // Get element type from the array field
            if (arrayRef->children.size() > 0 && arrayRef->children[0]) {
                auto memberBase = arrayRef->children[0];
                if (memberBase->type == ASTNodeType::ARRAY_ACCESS && 
                    memberBase->children.size() > 0 && memberBase->children[0]) {
                    auto arrayVar = memberBase->children[0];
                    if (arrayVar->type == ASTNodeType::IDENTIFIER) {
                        std::string arrayName = arrayVar->value;
                        ArrayInfo arrayInfo;
                        auto arrayIt = arrayInfos.find(arrayName);
                        if (arrayIt == arrayInfos.end()) {
                            auto globalArrayIt = globalArrays.find(arrayName);
                            if (globalArrayIt != globalArrays.end()) {
                                arrayInfo = globalArrayIt->second;
                            } else {
                                std::cout << "⚠️ Unknown array in nested access: " << arrayName << "\n";
                            }
                        } else {
                            arrayInfo = arrayIt->second;
                        }
                        
                        // Check if array element is a record
                        auto recordTypeIt = recordTypes.find(arrayInfo.elemTypeName);
                        if (recordTypeIt != recordTypes.end()) {
                            std::string fieldName = arrayRef->value;
                            // Find the field and check if it's an array
                            auto arrayFieldIt = recordTypeIt->second.arrayFieldElementTypes.find(fieldName);
                            if (arrayFieldIt != recordTypeIt->second.arrayFieldElementTypes.end()) {
                                // Field is an array - get its element type
                                std::string elemTypeName = arrayFieldIt->second;
                                
                                // Handle primitive types
                                if (elemTypeName == "real") {
                                    elemType = wasm::Type::f64;
                                } else if (elemTypeName == "integer" || elemTypeName == "boolean") {
                                    elemType = wasm::Type::i32;
                                } else {
                                    // Check if it's a record type
                                    auto recordIt = recordTypes.find(elemTypeName);
                                    if (recordIt != recordTypes.end()) {
                                        elemType = wasm::Type::i32; // Records are stored as i32 pointers
                                    } else {
                                        // Check if it's a type alias
                                        auto resolvedType = resolveTypeAlias(elemTypeName);
                                        if (resolvedType && resolvedType->type == ASTNodeType::PRIMITIVE_TYPE) {
                                            if (resolvedType->value == "real") {
                                                elemType = wasm::Type::f64;
                                            } else {
                                                elemType = wasm::Type::i32;
                                            }
                                        } else {
                                            // Could be a nested array type - for nested arrays,
                                            // we'd need to parse the type string to get the innermost element type
                                            // For now, default to i32
                                            elemType = wasm::Type::i32; // Default fallback
                                        }
                                    }
                                }
                            } else {
                                std::cout << "⚠️ Field '" << fieldName << "' is not an array field in record '" 
                                          << arrayInfo.elemTypeName << "'\n";
                            }
                        } else {
                            std::cout << "⚠️ Array element type '" << arrayInfo.elemTypeName 
                                      << "' is not a record type\n";
                        }
                    }
                }
            }
        }
        
        // Convert RHS to match element type BEFORE calling generateArrayAssignment
        ValueType sourceType = getExpressionType(rhs, F);
        ValueType targetType = (elemType == wasm::Type::f64) ? ValueType::REAL : ValueType::INTEGER;
        if (sourceType != targetType) {
            rhsExpr = emitTypeConversion(rhsExpr, sourceType, targetType);
        }
        
        // Now call generateArrayAssignment with the converted RHS
        return generateArrayAssignment(lhs, rhsExpr, F);
    } else if (lhs->type == ASTNodeType::MEMBER_ACCESS) {
        // Member assignment: record.field := value or array[index].field := value
        wasm::Expression* rhsExpr = generateExpression(rhs, F);
        ValueType sourceType = getExpressionType(rhs, F);
        
        // Get the target field type
        ValueType targetType = ValueType::INTEGER; // default
        auto base = lhs->children[0];
        std::string fieldName = lhs->value;
        
        if (base && base->type == ASTNodeType::IDENTIFIER) {
            std::string recordName = base->value;
            auto recordIt = recordVariables.find(recordName);
            std::string recordTypeName;
            
            if (recordIt != recordVariables.end()) {
                recordTypeName = recordIt->second.recordType;
            } else {
                // Check if it's a parameter
                auto localIt = localVarIndices.find(recordName);
                if (localIt != localVarIndices.end()) {
                    // Find the parameter type
                    std::shared_ptr<ASTNode> params = nullptr;
                    for (auto& ch : F.node->children) {
                        if (ch && ch->type == ASTNodeType::PARAMETER_LIST) { params = ch; break; }
                    }
                    if (params) {
                        for (auto& p : params->children) {
                            if (p && p->type == ASTNodeType::PARAMETER && p->value == recordName) {
                                for (auto& pc : p->children) {
                                    if (pc && pc->type == ASTNodeType::USER_TYPE) {
                                        recordTypeName = pc->value;
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
            
            if (!recordTypeName.empty()) {
                auto recordTypeIt = recordTypes.find(recordTypeName);
                if (recordTypeIt != recordTypes.end()) {
                    for (const auto& field : recordTypeIt->second.fields) {
                        if (field.first == fieldName) {
                            wasm::Type fieldWasmType = field.second.first;
                            if (fieldWasmType == wasm::Type::f64) {
                                targetType = ValueType::REAL;
                            } else if (fieldWasmType == wasm::Type::i32) {
                                // Could be integer or boolean - check field name or use context
                                targetType = ValueType::INTEGER;
                            }
                            break;
                        }
                    }
                }
            }
        } else if (base && base->type == ASTNodeType::ARRAY_ACCESS) {
            // Handle array[index].field := value
            // Similar logic but for array elements
            if (base->children.size() >= 1 && base->children[0]) {
                std::string arrayName = base->children[0]->value;
                auto arrayIt = arrayInfos.find(arrayName);
                if (arrayIt != arrayInfos.end() && arrayIt->second.elemTypeName != "") {
                    // Array of records - find the field type
                    auto recordTypeIt = recordTypes.find(arrayIt->second.elemTypeName);
                    if (recordTypeIt != recordTypes.end()) {
                        for (const auto& field : recordTypeIt->second.fields) {
                            if (field.first == fieldName) {
                                wasm::Type fieldWasmType = field.second.first;
                                if (fieldWasmType == wasm::Type::f64) {
                                    targetType = ValueType::REAL;
                                } else {
                                    targetType = ValueType::INTEGER;
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        // Convert RHS to match field type
        if (sourceType != targetType) {
            rhsExpr = emitTypeConversion(rhsExpr, sourceType, targetType);
        }
        
        return generateMemberAssignment(lhs, rhsExpr, F);
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
    // Ensure condition is boolean (i32) - comparisons should already return i32
    ValueType condType = getExpressionType(cond, F);
    if (condType != ValueType::BOOLEAN && condType != ValueType::INTEGER) {
        // Convert to boolean if needed (shouldn't happen for valid conditions)
        condExpr = emitTypeConversion(condExpr, condType, ValueType::BOOLEAN);
    }
    
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

// Static counter for unique while loop names
static int whileLoopCounter = 0;

wasm::Expression* WasmCompiler::generateWhileLoop(std::shared_ptr<ASTNode> w,
                                     const FuncInfo& F) {
    std::cout << "🔧 generateWhileLoop: Starting while loop generation" << std::endl;
    if (!w || w->children.size() < 2) {
        std::cout << "⚠️ Malformed WHILE_LOOP node" << std::endl;
        return builder.makeNop();
    }
    
    // Generate unique names for this while loop
    int loopId = whileLoopCounter++;
    std::string blockName = "while_block_" + std::to_string(loopId);
    std::string loopName = "while_loop_" + std::to_string(loopId);
    std::cout << "  📊 Generated unique names: block='" << blockName 
              << "', loop='" << loopName << "'" << std::endl;
    
    auto cond = w->children[0];
    auto loopB = w->children[1];
    
    std::cout << "  🔧 Processing condition..." << std::endl;
    wasm::Expression* condExpr = generateExpression(cond, F);
    ValueType condType = getExpressionType(cond, F);
    std::cout << "  📊 Condition expression type: " << (int)condType << std::endl;
    
    // Ensure condition is boolean (i32)
    if (condType != ValueType::BOOLEAN && condType != ValueType::INTEGER) {
        std::cout << "  ⚠️ Converting condition to boolean" << std::endl;
        condExpr = emitTypeConversion(condExpr, condType, ValueType::BOOLEAN);
    }
    
    std::vector<wasm::Expression*> bodyExprs;
    if (loopB && loopB->type == ASTNodeType::BODY) {
        std::cout << "  🔧 Processing loop body with " << loopB->children.size() << " statements" << std::endl;
        for (auto& s : loopB->children) {
            if (!s) continue;
            std::cout << "    🔧 Processing statement type: " << tname(s->type) << std::endl;
            switch (s->type) {
                case ASTNodeType::ASSIGNMENT: 
                    bodyExprs.push_back(generateAssignment(s, F)); 
                    std::cout << "      ✅ Added assignment to body" << std::endl;
                    break;
                case ASTNodeType::IF_STMT: 
                    bodyExprs.push_back(generateIfStatement(s, F)); 
                    break;
                case ASTNodeType::WHILE_LOOP: 
                    std::cout << "      🔧 Found nested WHILE_LOOP, generating..." << std::endl;
                    bodyExprs.push_back(generateWhileLoop(s, F)); 
                    std::cout << "      ✅ Added nested while loop to body" << std::endl;
                    break;
                case ASTNodeType::FOR_LOOP: 
                    bodyExprs.push_back(generateForLoop(s, F)); 
                    break;
                case ASTNodeType::RETURN_STMT: 
                    bodyExprs.push_back(generateReturn(s, F)); 
                    break;
                case ASTNodeType::VAR_DECL: {
                    std::vector<wasm::Expression*> varBody;
                    generateVarDeclaration(varBody, s, F);
                    bodyExprs.insert(bodyExprs.end(), varBody.begin(), varBody.end());
                    break;
                }
                default: 
                    std::cout << "      ⚠️ Unhandled statement type in while body: " << tname(s->type) << std::endl;
                    break;
            }
        }
    } else {
        std::cout << "  ⚠️ No valid loop body found" << std::endl;
    }
    
    std::cout << "  📊 Loop body has " << bodyExprs.size() << " expressions" << std::endl;
    wasm::Expression* bodyBlock = bodyExprs.empty() ? builder.makeNop() :
        (bodyExprs.size() == 1 ? bodyExprs[0] : builder.makeBlock("", bodyExprs));
    
    // Create: block (loop (if (not cond) (br block)) body (br loop))
    // The condition check: if condition is false (not cond), break out of the block
    std::cout << "  🔧 Creating condition check: if (not cond) break to '" << blockName << "'" << std::endl;
    wasm::Expression* ifBreak = builder.makeIf(
        builder.makeUnary(wasm::EqZInt32, condExpr),
        builder.makeBreak(blockName, nullptr, nullptr)
    );
    std::cout << "  ✅ Created condition break expression" << std::endl;
    
    std::vector<wasm::Expression*> loopBody;
    loopBody.push_back(ifBreak);
    std::cout << "  ✅ Added condition check to loop body" << std::endl;
    loopBody.push_back(bodyBlock);
    std::cout << "  ✅ Added body block to loop body" << std::endl;
    loopBody.push_back(builder.makeBreak(loopName, nullptr, nullptr));
    std::cout << "  ✅ Added continue break to loop body (breaks to '" << loopName << "')" << std::endl;
    
    wasm::Expression* loop = builder.makeLoop(loopName, builder.makeBlock("", loopBody));
    std::cout << "  ✅ Created loop with name '" << loopName << "'" << std::endl;
    wasm::Expression* result = builder.makeBlock(blockName, {loop});
    std::cout << "✅ generateWhileLoop: Completed while loop (block='" << blockName 
              << "', loop='" << loopName << "')" << std::endl;
    return result;
}

wasm::Expression* WasmCompiler::generateForLoop(std::shared_ptr<ASTNode> forNode,
                                   const FuncInfo& F) {
    std::cout << "🔧 generateForLoop: Starting for loop generation" << std::endl;
    if (!forNode) {
        std::cout << "⚠️ Malformed FOR_LOOP node\n";
        return builder.makeNop();
    }

    const std::string iv = forNode->value;
    std::cout << "🔧 generateForLoop: Loop variable name = '" << iv << "'" << std::endl;

    std::shared_ptr<ASTNode> rangeNode = nullptr;
    std::shared_ptr<ASTNode> loopBody = nullptr;
    bool isReverse = false;

    for (auto& ch : forNode->children) {
        if (!ch) continue;
        if (ch->type == ASTNodeType::RANGE) {
            rangeNode = ch;
            std::cout << "  ✅ Found RANGE node" << std::endl;
        } else if (ch->type == ASTNodeType::BODY) {
            loopBody = ch;
            std::cout << "  ✅ Found BODY node with " << ch->children.size() << " children" << std::endl;
        } else if (ch->type == ASTNodeType::IDENTIFIER && ch->value == "reverse") {
            isReverse = true;
            std::cout << "  ✅ Found reverse flag" << std::endl;
        }
    }

    if (!rangeNode || !loopBody) {
        std::cout << "⚠️ Malformed FOR_LOOP node (missing RANGE or BODY)\n";
        return builder.makeNop();
    }

    std::cout << "🔍 Checking if loop variable '" << iv << "' is in localVarIndices..." << std::endl;
    std::cout << "  📊 Current localVarIndices size: " << localVarIndices.size() << std::endl;
    for (const auto& [name, idx] : localVarIndices) {
        std::cout << "    - " << name << " -> " << idx << std::endl;
    }
    
    auto it = localVarIndices.find(iv);
    if (it == localVarIndices.end()) {
        std::cout << "❌ Loop variable not declared as local: " << iv << "\n";
        std::cout << "  Available locals: ";
        for (const auto& [name, idx] : localVarIndices) {
            std::cout << name << " ";
        }
        std::cout << std::endl;
        return builder.makeNop();
    }
    uint32_t ivIdx = static_cast<uint32_t>(it->second);
    std::cout << "✅ Loop variable '" << iv << "' found at local index " << ivIdx << std::endl;

    std::shared_ptr<ASTNode> startExpr = nullptr, endExpr = nullptr;
    if (rangeNode->children.size() >= 2) {
        startExpr = rangeNode->children[0];
        endExpr = rangeNode->children[1];
        std::cout << "  📊 Range: startExpr type=" << tname(startExpr->type) 
                  << ", endExpr type=" << tname(endExpr->type) << std::endl;
    }
    if (!endExpr) {
        std::cout << "⚠️ FOR_LOOP missing range end\n";
        return builder.makeNop();
    }

    wasm::Expression* startVal = startExpr ? generateExpression(startExpr, F) : emitI32Const(0);
    wasm::Expression* endVal = generateExpression(endExpr, F);
    std::cout << "  📊 Generated startVal (type=" << startVal->type 
              << "), endVal (type=" << endVal->type << ")" << std::endl;
    
    // Initialize loop variable
    wasm::Expression* init = emitLocalSet(iv, startVal);
    std::cout << "  ✅ Created initialization expression for loop variable" << std::endl;
    
    // Loop condition and body
    std::vector<wasm::Expression*> bodyExprs;
    if (loopBody && loopBody->type == ASTNodeType::BODY) {
        std::cout << "  🔧 Processing loop body with " << loopBody->children.size() << " statements" << std::endl;
        for (auto& s : loopBody->children) {
            if (!s) continue;
            std::cout << "    🔧 Processing statement type: " << tname(s->type) << std::endl;
            switch (s->type) {
                case ASTNodeType::ASSIGNMENT: 
                    bodyExprs.push_back(generateAssignment(s, F)); 
                    std::cout << "      ✅ Added assignment to body" << std::endl;
                    break;
                case ASTNodeType::IF_STMT: 
                    bodyExprs.push_back(generateIfStatement(s, F)); 
                    break;
                case ASTNodeType::WHILE_LOOP: 
                    bodyExprs.push_back(generateWhileLoop(s, F)); 
                    break;
                case ASTNodeType::FOR_LOOP: 
                    std::cout << "      🔧 Found nested FOR_LOOP, generating..." << std::endl;
                    bodyExprs.push_back(generateForLoop(s, F)); 
                    std::cout << "      ✅ Added nested for loop to body" << std::endl;
                    break;
                case ASTNodeType::RETURN_STMT: 
                    bodyExprs.push_back(generateReturn(s, F)); 
                    break;
                case ASTNodeType::VAR_DECL: {
                    std::vector<wasm::Expression*> varBody;
                    generateVarDeclaration(varBody, s, F);
                    bodyExprs.insert(bodyExprs.end(), varBody.begin(), varBody.end());
                    break;
                }
                default: 
                    std::cout << "      ⚠️ Unhandled statement type in loop body: " << tname(s->type) << std::endl;
                    break;
            }
        }
    }
    
    std::cout << "  📊 Loop body has " << bodyExprs.size() << " expressions" << std::endl;
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
    std::cout << "  ✅ Created step expression (increment/decrement)" << std::endl;
    
    // Condition: forward break if i > end, reverse break if i < end
    // For forward loop (10..14): continue while i <= end, break when i > end
    //   - We want to execute when i = 10, 11, 12, 13, 14
    //   - So we break when i > 14 (i.e., i = 15)
    // For reverse loop (14..10): continue while i >= end, break when i < end
    //   - We want to execute when i = 14, 13, 12, 11, 10
    //   - So we break when i < 10 (i.e., i = 9)
    wasm::Expression* cond = builder.makeBinary(
        isReverse ? wasm::LtSInt32 : wasm::GtSInt32,
        emitLocalGet(iv),
        endVal
    );
    std::cout << "  ✅ Created condition expression (isReverse=" << isReverse 
              << ", breaks when i " << (isReverse ? "<" : ">") << " end)" << std::endl;
    
    // Wrap loop in a block so we can break out of it
    std::string blockName = "for_block_" + iv;
    std::string loopName = "for_loop_" + iv;
    std::cout << "  📊 Block name: " << blockName << ", Loop name: " << loopName << std::endl;
    
    std::vector<wasm::Expression*> loopExprs;
    // Check condition at START of iteration: break out of block if done
    loopExprs.push_back(builder.makeIf(cond, builder.makeBreak(blockName, nullptr, nullptr)));
    std::cout << "  ✅ Added condition check to loop" << std::endl;
    // Execute body
    loopExprs.push_back(bodyBlock);
    std::cout << "  ✅ Added body block to loop" << std::endl;
    // Step (increment/decrement)
    loopExprs.push_back(step);
    std::cout << "  ✅ Added step to loop" << std::endl;
    // Continue loop by breaking to loop name (this continues the loop)
    loopExprs.push_back(builder.makeBreak(loopName, nullptr, nullptr));
    std::cout << "  ✅ Added continue break to loop" << std::endl;
    
    wasm::Expression* loop = builder.makeLoop(loopName, builder.makeBlock("", loopExprs));
    wasm::Expression* result = builder.makeBlock(blockName, {init, loop});
    std::cout << "✅ generateForLoop: Completed for loop '" << iv << "'" << std::endl;
    return result;
}

wasm::Expression* WasmCompiler::generateReturn(std::shared_ptr<ASTNode> r,
                                  const FuncInfo& F) {
    if (!r) return builder.makeNop();
    if (!F.resultTypes.empty()) {
        wasm::Type expectedWasmType = F.resultTypes[0];
        ValueType expectedType = ValueType::INTEGER;
        bool isRecordOrArrayReturn = false;
        
        if (expectedWasmType == wasm::Type::f64) {
            expectedType = ValueType::REAL;
        } else {
            std::shared_ptr<ASTNode> retType = nullptr;
            for (auto& ch : F.node->children) {
                if (ch && (ch->type == ASTNodeType::PRIMITIVE_TYPE || 
                           ch->type == ASTNodeType::USER_TYPE ||
                           ch->type == ASTNodeType::ARRAY_TYPE)) {
                    retType = ch;
                    break;
                }
            }
            if (retType) {
                if (retType->type == ASTNodeType::USER_TYPE) {
                    // Returning a record - return its address (i32 pointer)
                    isRecordOrArrayReturn = true;
                } else if (retType->type == ASTNodeType::ARRAY_TYPE) {
                    // Returning an array - return its address (i32 pointer)
                    isRecordOrArrayReturn = true;
                } else if (retType->type == ASTNodeType::PRIMITIVE_TYPE) {
                    if (retType->value == "boolean") expectedType = ValueType::BOOLEAN;
                    else expectedType = ValueType::INTEGER;
                }
            }
        }
        
        if (!r->children.empty() && r->children[0]) {
            auto returnExpr = r->children[0];
            
            if (isRecordOrArrayReturn) {
                // For record/array returns, we need to return the address (i32)
                // If it's an identifier, use emitLocalGet which now handles records/arrays
                wasm::Expression* retExpr = generateExpression(returnExpr, F);
                // Ensure the return expression is explicitly i32 type
                // generateExpression should return i32 for records/arrays via emitLocalGet
                // The optimizer needs to see this as a single i32 value
                // Make sure the expression is finalized before creating the return
                if (retExpr->type == wasm::Type::none) {
                    // Expression hasn't been finalized - this shouldn't happen but be safe
                    retExpr->finalize();
                }
                // The return statement must return a single i32 value
                // Verify the return expression is i32
                if (retExpr->type != wasm::Type::i32) {
                    std::cout << "  ⚠️ Warning: Return expression type is " << retExpr->type 
                              << ", expected i32. Finalizing..." << std::endl;
                    retExpr->finalize();
                }
                wasm::Return* ret = builder.makeReturn(retExpr);
                // Finalize the return statement to ensure it's properly typed
                ret->finalize();
                std::cout << "  ✅ Return statement generated with type " << retExpr->type << std::endl;
                return ret;
            } else {
                ValueType actualType = getExpressionType(returnExpr, F);
                wasm::Expression* retExpr = generateExpression(returnExpr, F);
                if (actualType != expectedType) {
                    retExpr = emitTypeConversion(retExpr, actualType, expectedType);
                }
                return builder.makeReturn(retExpr);
            }
        } else {
            // No return expression provided but function expects a return value
            if (expectedType == ValueType::REAL) {
                return builder.makeReturn(emitF64Const(0.0));
            } else {
                return builder.makeReturn(emitI32Const(0));
            }
        }
    }
    // Function has no return type - return void
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
    
    const std::string& op = bin->value;
    bool isComparison = (op == "<" || op == "<=" || op == ">" || op == ">=" || 
                        op == "=" || op == "/=");
    
    ValueType resultType;
    if (isComparison) {
        // Comparisons always return boolean
        resultType = ValueType::BOOLEAN;
    } else {
        // For arithmetic operations, result type depends on operands
        resultType = ValueType::INTEGER;
    if (leftType == ValueType::REAL || rightType == ValueType::REAL) {
        resultType = ValueType::REAL;
    } else if (leftType == ValueType::INTEGER || rightType == ValueType::INTEGER) {
        resultType = ValueType::INTEGER;
    } else {
        resultType = ValueType::BOOLEAN;
        }
    }
    
    // For comparisons, we need to promote operands to the same type (both real or both integer)
    ValueType operandType = ValueType::INTEGER;
    if (leftType == ValueType::REAL || rightType == ValueType::REAL) {
        operandType = ValueType::REAL;
    }
    
    wasm::Expression* leftExpr = generateExpression(L, F);
    wasm::Expression* rightExpr = generateExpression(R, F);
    
    // For comparisons, convert operands to the same type (both real or both integer)
    if (isComparison) {
        if (leftType != operandType) {
            leftExpr = emitTypeConversion(leftExpr, leftType, operandType);
        }
        if (rightType != operandType) {
            rightExpr = emitTypeConversion(rightExpr, rightType, operandType);
        }
    } else {
        // For arithmetic operations, convert to result type
        if (leftType != resultType) {
            leftExpr = emitTypeConversion(leftExpr, leftType, resultType);
        }
    if (rightType != resultType) {
            rightExpr = emitTypeConversion(rightExpr, rightType, resultType);
        }
    }
    
    // Handle comparisons first (they return boolean regardless of operand types)
    if (isComparison) {
        if (operandType == ValueType::REAL) {
            if (op == "<") return builder.makeBinary(wasm::LtFloat64, leftExpr, rightExpr);
            else if (op == "<=") return builder.makeBinary(wasm::LeFloat64, leftExpr, rightExpr);
            else if (op == ">") return builder.makeBinary(wasm::GtFloat64, leftExpr, rightExpr);
            else if (op == ">=") return builder.makeBinary(wasm::GeFloat64, leftExpr, rightExpr);
            else if (op == "=") return builder.makeBinary(wasm::EqFloat64, leftExpr, rightExpr);
            else if (op == "/=") return builder.makeBinary(wasm::NeFloat64, leftExpr, rightExpr);
        } else {
            if (op == "<") return builder.makeBinary(wasm::LtSInt32, leftExpr, rightExpr);
            else if (op == "<=") return builder.makeBinary(wasm::LeSInt32, leftExpr, rightExpr);
            else if (op == ">") return builder.makeBinary(wasm::GtSInt32, leftExpr, rightExpr);
            else if (op == ">=") return builder.makeBinary(wasm::GeSInt32, leftExpr, rightExpr);
            else if (op == "=") return builder.makeBinary(wasm::EqInt32, leftExpr, rightExpr);
            else if (op == "/=") return builder.makeBinary(wasm::NeInt32, leftExpr, rightExpr);
        }
    }
    
    // Handle arithmetic operations
    if (resultType == ValueType::REAL) {
        if (op == "+") return builder.makeBinary(wasm::AddFloat64, leftExpr, rightExpr);
        else if (op == "-") return builder.makeBinary(wasm::SubFloat64, leftExpr, rightExpr);
        else if (op == "*") return builder.makeBinary(wasm::MulFloat64, leftExpr, rightExpr);
        else if (op == "/") return builder.makeBinary(wasm::DivFloat64, leftExpr, rightExpr);
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
    
    // Get return type from function signature
    wasm::Type returnType = wasm::Type::none;
    auto funcIt = funcIndexByName.find(call->value);
    if (funcIt != funcIndexByName.end() && funcIt->second < funcs.size()) {
        auto& calledFunc = funcs[funcIt->second];
        if (!calledFunc.resultTypes.empty()) {
            returnType = calledFunc.resultTypes[0];
        }
    }
    
    // If function has no return type, use none
    if (returnType == wasm::Type::none) {
        return builder.makeCall(call->value, args, wasm::Type::none);
    } else {
        // Ensure the call returns a single value of the correct type
        wasm::Call* callExpr = builder.makeCall(call->value, args, returnType);
        // Finalize the call to ensure it's properly typed
        callExpr->finalize();
        // Verify the call returns the expected type
        if (callExpr->type != returnType) {
            std::cout << "  ⚠️ Warning: Function call type is " << callExpr->type 
                      << ", expected " << returnType << std::endl;
        }
        std::cout << "  ✅ Function call generated: " << call->value 
                  << " returns " << returnType << std::endl;
        return callExpr;
    }
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
    // Check for record variables first (before checking localVarIndices)
    // Record variables are stored in memory, and the local variable holds the address
    auto recordIt = recordVariables.find(name);
    if (recordIt != recordVariables.end()) {
        // Check if it's also in localVarIndices (local record variable)
        auto localIt = localVarIndices.find(name);
        if (localIt != localVarIndices.end()) {
            // It's a local record variable - the local holds the address
            return builder.makeLocalGet(localIt->second, wasm::Type::i32);
        } else {
            // It's a global record variable - return the constant address
            return emitRecordBaseAddress(name);
        }
    }
    
    auto localIt = localVarIndices.find(name);
    if (localIt != localVarIndices.end()) {
        // Get the actual type of the local variable
        wasm::Type localType = wasm::Type::i32;  // Default to i32
        auto typeIt = localVarTypes.find(name);
        if (typeIt != localVarTypes.end()) {
            localType = typeIt->second;
        }
        return builder.makeLocalGet(localIt->second, localType);
    }
    
    auto globalIt = globalVars.find(name);
    if (globalIt != globalVars.end()) {
        wasm::Expression* addr = builder.makeConst(wasm::Literal(globalIt->second.memoryOffset));
        if (globalIt->second.type == wasm::Type::f64) {
            return builder.makeLoad(8, false, 0, 0, addr, wasm::Type::f64, wasm::Name("memory"));
        } else {
            return builder.makeLoad(4, false, 0, 0, addr, wasm::Type::i32, wasm::Name("memory"));
        }
    }
    
    // Check for arrays (both global and local)
    auto arrayIt = arrayInfos.find(name);
    if (arrayIt != arrayInfos.end()) {
        return builder.makeConst(wasm::Literal(arrayIt->second.baseOffset));
    }
    
    auto globalArrayIt = globalArrays.find(name);
    if (globalArrayIt != globalArrays.end()) {
        return builder.makeConst(wasm::Literal(globalArrayIt->second.baseOffset));
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
        // Ensure the value type matches the local variable type
        // Type conversion should have happened in generateAssignment, but we verify here
        auto typeIt = localVarTypes.find(name);
        if (typeIt != localVarTypes.end()) {
            wasm::Type expectedType = typeIt->second;
            // Binaryen will validate the type, but we ensure it's correct
            // The value should already be converted by generateAssignment
        }
        return builder.makeLocalSet(localIt->second, value);
    }
    
    auto globalIt = globalVars.find(name);
    if (globalIt != globalVars.end()) {
        wasm::Expression* addr = builder.makeConst(wasm::Literal(globalIt->second.memoryOffset));
        if (globalIt->second.type == wasm::Type::f64) {
            return builder.makeStore(8, 0, 0, addr, value, wasm::Type::f64, wasm::Name("memory"));
        } else {
            return builder.makeStore(4, 0, 0, addr, value, wasm::Type::i32, wasm::Name("memory"));
        }
    }
    
    std::cout << "  ⚠️ Unknown variable set: " << name << " (dropping value)\n";
    return builder.makeDrop(value);
}

wasm::Expression* WasmCompiler::emitI32Load(uint32_t offset) {
    wasm::Expression* addr = builder.makeConst(wasm::Literal(static_cast<int32_t>(offset)));
    return builder.makeLoad(4, false, 0, 0, addr, wasm::Type::i32, wasm::Name("memory"));
}

wasm::Expression* WasmCompiler::emitI32Store(uint32_t offset, wasm::Expression* value) {
    wasm::Expression* addr = builder.makeConst(wasm::Literal(static_cast<int32_t>(offset)));
    return builder.makeStore(4, 0, 0, addr, value, wasm::Type::i32, wasm::Name("memory"));
}

wasm::Expression* WasmCompiler::emitF64Load(uint32_t offset) {
    wasm::Expression* addr = builder.makeConst(wasm::Literal(static_cast<int32_t>(offset)));
    return builder.makeLoad(8, false, 0, 0, addr, wasm::Type::f64, wasm::Name("memory"));
}

wasm::Expression* WasmCompiler::emitF64Store(uint32_t offset, wasm::Expression* value) {
    wasm::Expression* addr = builder.makeConst(wasm::Literal(static_cast<int32_t>(offset)));
    return builder.makeStore(8, 0, 0, addr, value, wasm::Type::f64, wasm::Name("memory"));
}

void WasmCompiler::setupMemory() {
    int totalMemoryNeeded = globalMemoryOffset;
    int totalMemoryPages = (totalMemoryNeeded + 65535) / 65536;
    
    // Always create at least 1 page of memory (even if not used)
    if (totalMemoryPages == 0) totalMemoryPages = 1;
    if (totalMemoryPages > 1024) totalMemoryPages = 1024;
    
    std::cout << "📊 Total memory needed: " << totalMemoryNeeded 
              << " bytes (" << totalMemoryPages << " pages)" << std::endl;
    
    // Always setup memory (Binaryen requires it if we use load/store)
    if (module->memories.empty()) {
        auto mem = std::make_unique<wasm::Memory>();
        mem->name = wasm::Name("memory");
        mem->initial = totalMemoryPages;
        mem->max = totalMemoryPages;
        module->addMemory(std::move(mem));
    }
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
        return builder.makeLoad(8, false, 0, 0, offset, wasm::Type::f64, wasm::Name("memory"));
    } else {
        return builder.makeLoad(4, false, 0, 0, offset, wasm::Type::i32, wasm::Name("memory"));
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
            return builder.makeLoad(8, false, 0, 0, addr, wasm::Type::f64, wasm::Name("memory"));
    } else {
            return builder.makeLoad(4, false, 0, 0, addr, wasm::Type::i32, wasm::Name("memory"));
        }
    } else if (base->type == ASTNodeType::ARRAY_ACCESS) {
        // Handle array[index].field[index2] (e.g., asg[4].name[32])
        if (base->children.size() < 2) {
            std::cout << "⚠️ Malformed array access in member array access\n";
            return emitI32Const(0);
        }
        
        auto arrayVar = base->children[0];
        auto firstIndex = base->children[1];
        
        if (arrayVar->type != ASTNodeType::IDENTIFIER) {
            std::cout << "⚠️ Member array access: array variable is not identifier\n";
            return emitI32Const(0);
        }
        
        std::string arrayName = arrayVar->value;
        ArrayInfo arrayInfo;
        auto arrayIt = arrayInfos.find(arrayName);
        if (arrayIt == arrayInfos.end()) {
            auto globalArrayIt = globalArrays.find(arrayName);
            if (globalArrayIt == globalArrays.end()) {
                std::cout << "⚠️ Unknown array: " << arrayName << "\n";
                return emitI32Const(0);
            }
            arrayInfo = globalArrayIt->second;
        } else {
            arrayInfo = arrayIt->second;
        }
        
        // Check if array element is a record
        auto recordTypeIt = recordTypes.find(arrayInfo.elemTypeName);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Array element is not a record: " << arrayInfo.elemTypeName << "\n";
            return emitI32Const(0);
        }
        
        // Find the field
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
            std::cout << "⚠️ Unknown field: " << fieldName << "\n";
            return emitI32Const(0);
        }
        
        // Calculate address: arrayBase + (firstIndex * recordSize) + fieldOffset + (secondIndex * elemSize)
        wasm::Expression* arrayBase = builder.makeConst(wasm::Literal(arrayInfo.baseOffset));
        
        wasm::Expression* firstIdx = generateExpression(firstIndex, F);
        wasm::Expression* recordSize = builder.makeConst(wasm::Literal(recordTypeIt->second.totalSize));
        wasm::Expression* recordOffset = builder.makeBinary(wasm::MulInt32, firstIdx, recordSize);
        wasm::Expression* elementBase = builder.makeBinary(wasm::AddInt32, arrayBase, recordOffset);
        wasm::Expression* fieldBase = builder.makeBinary(wasm::AddInt32, elementBase, 
                                                          builder.makeConst(wasm::Literal(fieldOffset)));
        
        wasm::Expression* secondIdx = generateExpression(indexExpr, F);
        int elemSize = (elemType == wasm::Type::f64) ? 8 : 4;
        wasm::Expression* addr = builder.makeBinary(
            wasm::AddInt32,
            fieldBase,
            builder.makeBinary(wasm::MulInt32, secondIdx, builder.makeConst(wasm::Literal(elemSize)))
        );
        
        if (elemType == wasm::Type::f64) {
            return builder.makeLoad(8, false, 0, 0, addr, wasm::Type::f64, wasm::Name("memory"));
        } else {
            return builder.makeLoad(4, false, 0, 0, addr, wasm::Type::i32, wasm::Name("memory"));
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
            // If baseOffset is 0, it's likely a parameter - we'll use emitLocalGet instead
        }
    } else if (arrayRef->type == ASTNodeType::MEMBER_ACCESS) {
        // Handle array[index].field[index2] := value (e.g., asg[4].name[32] := 11234)
        // The arrayRef is actually a MEMBER_ACCESS, and its base is an ARRAY_ACCESS
        if (arrayRef->children.size() < 1) {
            std::cout << "⚠️ Malformed member access in array assignment\n";
            return builder.makeNop();
        }
        
        auto memberBase = arrayRef->children[0];
        if (memberBase->type != ASTNodeType::ARRAY_ACCESS || memberBase->children.size() < 2) {
            std::cout << "⚠️ Array assignment: member access base is not array access\n";
            return builder.makeNop();
        }
        
        // Get the array and first index (e.g., asg[4])
        auto arrayVar = memberBase->children[0];
        auto firstIndex = memberBase->children[1];
        
        if (arrayVar->type != ASTNodeType::IDENTIFIER) {
            std::cout << "⚠️ Array assignment: array variable is not identifier\n";
            return builder.makeNop();
        }
        
        arrayName = arrayVar->value;
        auto arrayIt = arrayInfos.find(arrayName);
        if (arrayIt == arrayInfos.end()) {
            auto globalArrayIt = globalArrays.find(arrayName);
            if (globalArrayIt == globalArrays.end()) {
                std::cout << "⚠️ Unknown array: " << arrayName << "\n";
                return builder.makeNop();
            }
            arrayInfo = globalArrayIt->second;
        } else {
            arrayInfo = arrayIt->second;
        }
        
        // Check if array element is a record
        auto recordTypeIt = recordTypes.find(arrayInfo.elemTypeName);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Array element is not a record: " << arrayInfo.elemTypeName << "\n";
            return builder.makeNop();
        }
        
        // Find the field (e.g., "name")
        std::string fieldName = arrayRef->value;
        int fieldOffset = -1;
        wasm::Type fieldType = wasm::Type::i32;
        int fieldArraySize = 0;
        
        for (const auto& field : recordTypeIt->second.fields) {
            if (field.first == fieldName) {
                fieldOffset = field.second.second;
                // Check if field is an array
                auto arrayFieldIt = recordTypeIt->second.arrayFieldElementTypes.find(fieldName);
                if (arrayFieldIt != recordTypeIt->second.arrayFieldElementTypes.end()) {
                    // Field is an array - get its element type from the element type name
                    std::string elemTypeName = arrayFieldIt->second;
                    
                    // Handle primitive types
                    if (elemTypeName == "integer" || elemTypeName == "boolean") {
                        fieldType = wasm::Type::i32;
                    } else if (elemTypeName == "real") {
                        fieldType = wasm::Type::f64;
                    } else {
                        // Check if it's a record type
                        auto recordIt = recordTypes.find(elemTypeName);
                        if (recordIt != recordTypes.end()) {
                            fieldType = wasm::Type::i32; // Records are stored as i32 pointers
                        } else {
                            // Check if it's a type alias
                            auto resolvedType = resolveTypeAlias(elemTypeName);
                            if (resolvedType && resolvedType->type == ASTNodeType::PRIMITIVE_TYPE) {
                                if (resolvedType->value == "integer" || resolvedType->value == "boolean") {
                                    fieldType = wasm::Type::i32;
                                } else if (resolvedType->value == "real") {
                                    fieldType = wasm::Type::f64;
                                } else {
                                    fieldType = wasm::Type::i32; // Default
                                }
                            } else {
                                // Could be a nested array type (e.g., "array[10] integer")
                                // For now, we'll default to i32, but in a full implementation,
                                // we'd need to parse the array type string to extract the innermost element type
                                // For nested arrays, the element type is still the innermost type
                                // Since we're accessing a single element, we need the innermost element type
                                fieldType = wasm::Type::i32; // Default for nested arrays or unknown types
                            }
                        }
                    }
                } else {
                    fieldType = field.second.first;
                }
                break;
            }
        }
        
        if (fieldOffset == -1) {
            std::cout << "⚠️ Unknown field: " << fieldName << "\n";
            return builder.makeNop();
        }
        
        // Calculate address: arrayBase + (firstIndex * recordSize) + fieldOffset + (secondIndex * elemSize)
        wasm::Expression* arrayBase;
        auto globalArrayIt = globalArrays.find(arrayName);
        if (globalArrayIt != globalArrays.end()) {
            arrayBase = builder.makeConst(wasm::Literal(globalArrayIt->second.baseOffset));
        } else {
            arrayBase = builder.makeConst(wasm::Literal(arrayInfo.baseOffset));
        }
        
        wasm::Expression* firstIdx = generateExpression(firstIndex, F);
        wasm::Expression* recordSize = builder.makeConst(wasm::Literal(recordTypeIt->second.totalSize));
        wasm::Expression* recordOffset = builder.makeBinary(wasm::MulInt32, firstIdx, recordSize);
        wasm::Expression* elementBase = builder.makeBinary(wasm::AddInt32, arrayBase, recordOffset);
        wasm::Expression* fieldBase = builder.makeBinary(wasm::AddInt32, elementBase, 
                                                          builder.makeConst(wasm::Literal(fieldOffset)));
        
        wasm::Expression* secondIdx = generateExpression(indexExpr, F);
        int elemSize = (fieldType == wasm::Type::f64) ? 8 : 4;
        wasm::Expression* finalAddr = builder.makeBinary(
            wasm::AddInt32,
            fieldBase,
            builder.makeBinary(wasm::MulInt32, secondIdx, builder.makeConst(wasm::Literal(elemSize)))
        );
        
        if (rhs) {
            // Type conversion should have been done in generateAssignment before calling this
            // But we ensure the store uses the correct type
            if (fieldType == wasm::Type::f64) {
                return builder.makeStore(8, 0, 0, finalAddr, rhs, wasm::Type::f64, wasm::Name("memory"));
            } else {
                return builder.makeStore(4, 0, 0, finalAddr, rhs, wasm::Type::i32, wasm::Name("memory"));
            }
        }
    
        return finalAddr;
    } else {
        std::cout << "⚠️ Array assignment on unsupported node type: " << tname(arrayRef->type) << "\n";
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
            return builder.makeStore(8, 0, 0, addr, rhs, wasm::Type::f64, wasm::Name("memory"));
    } else {
            return builder.makeStore(4, 0, 0, addr, rhs, wasm::Type::i32, wasm::Name("memory"));
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
        
        // Check if it's a record parameter (in localVarIndices but not in recordVariables)
        bool isParameter = false;
        std::string recordTypeName;
        if (recordIt == recordVariables.end()) {
            // Check if it's a parameter (passed as i32 pointer)
            auto localIt = localVarIndices.find(recordName);
            if (localIt != localVarIndices.end()) {
                // It's a local variable - check if it's a record type parameter
                auto typeIt = localVarTypes.find(recordName);
                if (typeIt != localVarTypes.end() && typeIt->second == wasm::Type::i32) {
                    // Could be a record parameter - need to find the record type from parameter list
                    std::shared_ptr<ASTNode> params = nullptr;
                    for (auto& ch : F.node->children) {
                        if (ch && ch->type == ASTNodeType::PARAMETER_LIST) { params = ch; break; }
                    }
                    if (params) {
                        for (auto& p : params->children) {
                            if (p && p->type == ASTNodeType::PARAMETER && p->value == recordName) {
                                // Found the parameter - check if it's a record type
                                for (auto& pc : p->children) {
                                    if (pc && pc->type == ASTNodeType::USER_TYPE) {
                                        recordTypeName = pc->value;
                                        isParameter = true;
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
            if (!isParameter) {
                std::cout << "⚠️ Unknown record variable: " << recordName << "\n";
                return emitI32Const(0);
            }
        } else {
            recordTypeName = recordIt->second.recordType;
        }
        
        auto recordTypeIt = recordTypes.find(recordTypeName);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Unknown record type: " << recordTypeName << "\n";
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
                      << recordTypeName << "'\n";
            return emitI32Const(0);
        }
        
        wasm::Expression* recordBase;
        if (isParameter) {
            // Parameter is passed as i32 pointer - get it from local variable
            recordBase = emitLocalGet(recordName);
        } else {
            recordBase = emitRecordBaseAddress(recordName);
        }
        
        wasm::Expression* addr = builder.makeBinary(
            wasm::AddInt32,
            recordBase,
            builder.makeConst(wasm::Literal(fieldOffset))
        );
        
        if (fieldType == wasm::Type::f64) {
            return builder.makeLoad(8, false, 0, 0, addr, wasm::Type::f64, wasm::Name("memory"));
        } else {
            return builder.makeLoad(4, false, 0, 0, addr, wasm::Type::i32, wasm::Name("memory"));
        }
    } else if (base->type == ASTNodeType::ARRAY_ACCESS) {
        // Handle array[index].field (e.g., asg[4].id)
        if (base->children.size() < 2) {
            std::cout << "⚠️ Malformed array access in member access\n";
            return emitI32Const(0);
        }
        
        auto arrayRef = base->children[0];
        auto indexExpr = base->children[1];
        
        if (arrayRef->type != ASTNodeType::IDENTIFIER) {
            std::cout << "⚠️ Member access on array with non-identifier base\n";
            return emitI32Const(0);
        }
        
        std::string arrayName = arrayRef->value;
        auto arrayIt = arrayInfos.find(arrayName);
        if (arrayIt == arrayInfos.end()) {
            std::cout << "⚠️ Unknown array: " << arrayName << "\n";
            return emitI32Const(0);
        }
        
        // Check if array element type is a record
        auto recordTypeIt = recordTypes.find(arrayIt->second.elemTypeName);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Array element type is not a record: " << arrayIt->second.elemTypeName << "\n";
            return emitI32Const(0);
        }
        
        // Find field offset
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
                      << arrayIt->second.elemTypeName << "'\n";
            return emitI32Const(0);
        }
        
        // Calculate address: arrayBase + (index * recordSize) + fieldOffset
        wasm::Expression* arrayBase;
        auto globalArrayIt = globalArrays.find(arrayName);
        if (globalArrayIt != globalArrays.end()) {
            arrayBase = builder.makeConst(wasm::Literal(globalArrayIt->second.baseOffset));
    } else {
            arrayBase = builder.makeConst(wasm::Literal(arrayIt->second.baseOffset));
        }
        wasm::Expression* index = generateExpression(indexExpr, F);
        wasm::Expression* recordSize = builder.makeConst(wasm::Literal(recordTypeIt->second.totalSize));
        wasm::Expression* recordOffset = builder.makeBinary(wasm::MulInt32, index, recordSize);
        wasm::Expression* elementBase = builder.makeBinary(wasm::AddInt32, arrayBase, recordOffset);
        wasm::Expression* addr = builder.makeBinary(wasm::AddInt32, elementBase, 
                                                     builder.makeConst(wasm::Literal(fieldOffset)));
        
        if (fieldType == wasm::Type::f64) {
            return builder.makeLoad(8, false, 0, 0, addr, wasm::Type::f64, wasm::Name("memory"));
    } else {
            return builder.makeLoad(4, false, 0, 0, addr, wasm::Type::i32, wasm::Name("memory"));
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
    
    auto base = memberAccess->children[0];
    std::string fieldName = memberAccess->value;
    std::cout << "🔧 generateMemberAssignment called: field='" << fieldName 
              << "', base type=" << (base ? tname(base->type) : "null") << std::endl;
    
    wasm::Type fieldType = wasm::Type::i32;
    
    if (base->type == ASTNodeType::IDENTIFIER) {
        std::string recordName = base->value;
        auto recordIt = recordVariables.find(recordName);
        
        // Check if it's a record parameter (in localVarIndices but not in recordVariables)
        bool isParameter = false;
        std::string recordTypeName;
        if (recordIt == recordVariables.end()) {
            // Check if it's a parameter (passed as i32 pointer)
            auto localIt = localVarIndices.find(recordName);
            if (localIt != localVarIndices.end()) {
                // It's a local variable - check if it's a record type parameter
                auto typeIt = localVarTypes.find(recordName);
                std::cout << "🔍 Checking if '" << recordName << "' is a parameter: "
                          << "in localVarIndices=" << (localIt != localVarIndices.end())
                          << ", hasType=" << (typeIt != localVarTypes.end()) << std::endl;
                if (typeIt != localVarTypes.end() && typeIt->second == wasm::Type::i32) {
                    // Could be a record parameter - need to find the record type from parameter list
                    std::shared_ptr<ASTNode> params = nullptr;
                    for (auto& ch : F.node->children) {
                        if (ch && ch->type == ASTNodeType::PARAMETER_LIST) { params = ch; break; }
                    }
                    if (params) {
                        for (auto& p : params->children) {
                            if (p && p->type == ASTNodeType::PARAMETER && p->value == recordName) {
                                // Found the parameter - check if it's a record type
                                for (auto& pc : p->children) {
                                    if (pc && pc->type == ASTNodeType::USER_TYPE) {
                                        recordTypeName = pc->value;
                                        isParameter = true;
                                        std::cout << "✅ Found record parameter '" << recordName 
                                                  << "' with type '" << recordTypeName << "'" << std::endl;
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
            if (!isParameter) {
                std::cout << "⚠️ Unknown record variable: " << recordName << "\n";
                return builder.makeNop();
            }
        } else {
            recordTypeName = recordIt->second.recordType;
        }
        
        auto recordTypeIt = recordTypes.find(recordTypeName);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Unknown record type: " << recordTypeName << "\n";
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
                      << recordTypeName << "'\n";
            return builder.makeNop();
        }
        
        wasm::Expression* recordBase;
        if (isParameter) {
            // Parameter is passed as i32 pointer - get it from local variable
            recordBase = emitLocalGet(recordName);
            std::cout << "🔧 Member assignment: parameter '" << recordName 
                      << "' (record type: " << recordTypeName 
                      << ", field: " << fieldName << ", offset: " << fieldOffset << ")" << std::endl;
        } else {
            recordBase = emitRecordBaseAddress(recordName);
            std::cout << "🔧 Member assignment: record variable '" << recordName 
                      << "' (record type: " << recordTypeName 
                      << ", field: " << fieldName << ", offset: " << fieldOffset << ")" << std::endl;
        }
        
        wasm::Expression* addr = builder.makeBinary(
            wasm::AddInt32,
            recordBase,
            builder.makeConst(wasm::Literal(fieldOffset))
        );
        
        if (rhs) {
            // The RHS expression should already be the correct type from generateAssignment
            if (fieldType == wasm::Type::f64) {
                return builder.makeStore(8, 0, 0, addr, rhs, wasm::Type::f64, wasm::Name("memory"));
        } else {
                return builder.makeStore(4, 0, 0, addr, rhs, wasm::Type::i32, wasm::Name("memory"));
        }
        }
        
        return addr;
    } else if (base->type == ASTNodeType::ARRAY_ACCESS) {
        // Handle array[index].field := value (e.g., asg[4].id := 35)
        if (base->children.size() < 2) {
            std::cout << "⚠️ Malformed array access in member assignment\n";
            return builder.makeNop();
        }
        
        auto arrayRef = base->children[0];
        auto indexExpr = base->children[1];
        
        if (arrayRef->type != ASTNodeType::IDENTIFIER) {
            std::cout << "⚠️ Member assignment on array with non-identifier base\n";
            return builder.makeNop();
        }
        
        std::string arrayName = arrayRef->value;
        auto arrayIt = arrayInfos.find(arrayName);
        if (arrayIt == arrayInfos.end()) {
            std::cout << "⚠️ Unknown array: " << arrayName << "\n";
            return builder.makeNop();
        }
        
        // Check if array element type is a record
        auto recordTypeIt = recordTypes.find(arrayIt->second.elemTypeName);
        if (recordTypeIt == recordTypes.end()) {
            std::cout << "⚠️ Array element type is not a record: " << arrayIt->second.elemTypeName << "\n";
            return builder.makeNop();
        }
        
        // Find field offset
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
                      << arrayIt->second.elemTypeName << "'\n";
            return builder.makeNop();
        }
        
        // Calculate address: arrayBase + (index * recordSize) + fieldOffset
        wasm::Expression* arrayBase;
        auto globalArrayIt = globalArrays.find(arrayName);
        if (globalArrayIt != globalArrays.end()) {
            arrayBase = builder.makeConst(wasm::Literal(globalArrayIt->second.baseOffset));
        } else {
            arrayBase = builder.makeConst(wasm::Literal(arrayIt->second.baseOffset));
        }
        wasm::Expression* index = generateExpression(indexExpr, F);
        wasm::Expression* recordSize = builder.makeConst(wasm::Literal(recordTypeIt->second.totalSize));
        wasm::Expression* recordOffset = builder.makeBinary(wasm::MulInt32, index, recordSize);
        wasm::Expression* elementBase = builder.makeBinary(wasm::AddInt32, arrayBase, recordOffset);
        wasm::Expression* addr = builder.makeBinary(wasm::AddInt32, elementBase, 
                                                     builder.makeConst(wasm::Literal(fieldOffset)));
        
        if (rhs) {
            if (fieldType == wasm::Type::f64) {
                return builder.makeStore(8, 0, 0, addr, rhs, wasm::Type::f64, wasm::Name("memory"));
        } else {
                return builder.makeStore(4, 0, 0, addr, rhs, wasm::Type::i32, wasm::Name("memory"));
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
            
            // Check local variables first
            auto localTypeIt = localVarTypes.find(varName);
            if (localTypeIt != localVarTypes.end()) {
                if (localTypeIt->second == wasm::Type::f64) return ValueType::REAL;
                return ValueType::INTEGER;  // i32 for integer/boolean
            }
            
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
                    auto globalIt = globalArrays.find(arrayRef->value);
                    if (globalIt != globalArrays.end()) {
                        if (globalIt->second.elemType == wasm::Type::f64) return ValueType::REAL;
                        return ValueType::INTEGER;
                    }
                } else if (arrayRef->type == ASTNodeType::MEMBER_ACCESS) {
                    // Handle nested array access: employees[1].name[32]
                    // The arrayRef is a MEMBER_ACCESS, and its base is an ARRAY_ACCESS
                    if (arrayRef->children.size() > 0 && arrayRef->children[0]) {
                        auto memberBase = arrayRef->children[0];
                        if (memberBase->type == ASTNodeType::ARRAY_ACCESS && 
                            memberBase->children.size() > 0 && memberBase->children[0]) {
                            auto arrayVar = memberBase->children[0];
                            if (arrayVar->type == ASTNodeType::IDENTIFIER) {
                                std::string arrayName = arrayVar->value;
                                ArrayInfo arrayInfo;
                                auto arrayIt = arrayInfos.find(arrayName);
                                if (arrayIt == arrayInfos.end()) {
                                    auto globalArrayIt = globalArrays.find(arrayName);
                                    if (globalArrayIt == globalArrays.end()) {
                                        return ValueType::INTEGER; // Default
                                    }
                                    arrayInfo = globalArrayIt->second;
                                } else {
                                    arrayInfo = arrayIt->second;
                                }
                                
                                // Check if array element is a record
                                auto recordTypeIt = recordTypes.find(arrayInfo.elemTypeName);
                                if (recordTypeIt != recordTypes.end()) {
                                    std::string fieldName = arrayRef->value;
                                    // Find the field and check if it's an array
                                    auto arrayFieldIt = recordTypeIt->second.arrayFieldElementTypes.find(fieldName);
                                    if (arrayFieldIt != recordTypeIt->second.arrayFieldElementTypes.end()) {
                                        // Field is an array - get its element type
                                        std::string elemTypeName = arrayFieldIt->second;
                                        if (elemTypeName == "real") return ValueType::REAL;
                                        return ValueType::INTEGER;
                                    }
                                }
                            }
                        }
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
        // Truncate (not round) - just truncate directly without adding 0.5
        return builder.makeUnary(wasm::TruncSatSFloat64ToInt32, expr);
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

void WasmCompiler::addPrintImports() {
    // Add print_i32 import
    auto printI32 = std::make_unique<wasm::Function>();
    printI32->name = wasm::Name("print_i32");
    printI32->module = wasm::Name("env");
    printI32->base = wasm::Name("print_i32");
    wasm::Signature sigI32(wasm::Type::i32, wasm::Type::none);
    printI32->type = wasm::Type(sigI32, wasm::NonNullable, wasm::Inexact);
    module->addFunction(std::move(printI32));
    
    // Add print_f64 import
    auto printF64 = std::make_unique<wasm::Function>();
    printF64->name = wasm::Name("print_f64");
    printF64->module = wasm::Name("env");
    printF64->base = wasm::Name("print_f64");
    wasm::Signature sigF64(wasm::Type::f64, wasm::Type::none);
    printF64->type = wasm::Type(sigF64, wasm::NonNullable, wasm::Inexact);
    module->addFunction(std::move(printF64));
    
    // Add print_string import (for string literals)
    auto printString = std::make_unique<wasm::Function>();
    printString->name = wasm::Name("print_string");
    printString->module = wasm::Name("env");
    printString->base = wasm::Name("print_string");
    wasm::Signature sigString(wasm::Type::i32, wasm::Type::none); // i32 is pointer to string in memory
    printString->type = wasm::Type(sigString, wasm::NonNullable, wasm::Inexact);
    module->addFunction(std::move(printString));
}

wasm::Expression* WasmCompiler::generatePrintStatement(std::shared_ptr<ASTNode> printStmt,
                                          const FuncInfo& F) {
    if (!printStmt || printStmt->children.empty()) return builder.makeNop();
    
    auto exprList = printStmt->children[0];
    if (!exprList || exprList->type != ASTNodeType::EXPRESSION_LIST) return builder.makeNop();
    
    std::vector<wasm::Expression*> printCalls;
    for (auto& expr : exprList->children) {
        if (!expr) continue;
        
        if (expr->type == ASTNodeType::LITERAL_STRING) {
            // For string literals, we need to store the string in memory and pass the pointer
            // For now, we'll just print the string value directly via a host function
            // In a full implementation, we'd store the string in memory and pass the pointer
            // For simplicity, we'll use a placeholder that prints the string
            std::cout << "  📝 PRINT: \"" << expr->value << "\"\n";
            // TODO: Implement proper string printing via host function
            // For now, we'll just output to console during compilation
        } else {
            // Generate the expression value
            wasm::Expression* exprVal = generateExpression(expr, F);
            ValueType exprType = getExpressionType(expr, F);
            
            // Determine if it's a record or array (just print pointer)
            bool isRecordOrArray = false;
            if (expr->type == ASTNodeType::IDENTIFIER) {
                std::string varName = expr->value;
                if (recordVariables.find(varName) != recordVariables.end() ||
                    arrayInfos.find(varName) != arrayInfos.end() ||
                    globalArrays.find(varName) != globalArrays.end()) {
                    isRecordOrArray = true;
                }
            }
            
            if (isRecordOrArray) {
                // For records and arrays, just print the pointer (i32)
                printCalls.push_back(builder.makeCall(wasm::Name("print_i32"), {exprVal}, wasm::Type::none));
            } else if (exprType == ValueType::REAL) {
                // Print real value
                printCalls.push_back(builder.makeCall(wasm::Name("print_f64"), {exprVal}, wasm::Type::none));
            } else {
                // Print integer/boolean value
                printCalls.push_back(builder.makeCall(wasm::Name("print_i32"), {exprVal}, wasm::Type::none));
            }
        }
    }
    
    if (printCalls.empty()) return builder.makeNop();
    if (printCalls.size() == 1) return printCalls[0];
    return builder.makeBlock("", printCalls);
}



// ======================================================================
// Helper to resolve type alias (e.g., "myId" -> "real")
// ======================================================================
std::shared_ptr<ASTNode> WasmCompiler::resolveTypeAlias(const std::string& typeName) const {
    // Check if it's a record type first
    if (recordTypes.find(typeName) != recordTypes.end()) {
        return nullptr; // It's a record, not an alias
    }
    
    // Check if it's a type alias
    auto it = typeDefinitions.find(typeName);
    if (it != typeDefinitions.end()) {
        auto def = it->second;
        // If the definition is a primitive type, return it
        if (def->type == ASTNodeType::PRIMITIVE_TYPE) {
            return def;
        }
        // If it's another USER_TYPE, recursively resolve
        if (def->type == ASTNodeType::USER_TYPE) {
            return resolveTypeAlias(def->value);
        }
    }
    return nullptr;
}
