#include "wasm_compiler.h"
#include <iostream>
#include <cstring>
std::string astNodeTypeToString(ASTNodeType type) {
        switch (type) {
            case ASTNodeType::PROGRAM: return "PROGRAM";
            case ASTNodeType::VAR_DECL: return "VAR_DECL";
            case ASTNodeType::TYPE_DECL: return "TYPE_DECL";
            case ASTNodeType::ROUTINE_DECL: return "ROUTINE_DECL";
            case ASTNodeType::ROUTINE_FORWARD_DECL: return "ROUTINE_FORWARD_DECL";
            case ASTNodeType::PARAMETER: return "PARAMETER";
            case ASTNodeType::PRIMITIVE_TYPE: return "PRIMITIVE_TYPE";
            case ASTNodeType::ARRAY_TYPE: return "ARRAY_TYPE";
            case ASTNodeType::RECORD_TYPE: return "RECORD_TYPE";
            case ASTNodeType::USER_TYPE: return "USER_TYPE";
            case ASTNodeType::BINARY_OP: return "BINARY_OP";
            case ASTNodeType::UNARY_OP: return "UNARY_OP";
            case ASTNodeType::LITERAL_INT: return "LITERAL_INT";
            case ASTNodeType::LITERAL_REAL: return "LITERAL_REAL";
            case ASTNodeType::LITERAL_BOOL: return "LITERAL_BOOL";
            case ASTNodeType::LITERAL_STRING: return "LITERAL_STRING";
            case ASTNodeType::IDENTIFIER: return "IDENTIFIER";
            case ASTNodeType::ROUTINE_CALL: return "ROUTINE_CALL";
            case ASTNodeType::ARRAY_ACCESS: return "ARRAY_ACCESS";
            case ASTNodeType::MEMBER_ACCESS: return "MEMBER_ACCESS";
            case ASTNodeType::SIZE_EXPRESSION: return "SIZE_EXPRESSION";
            case ASTNodeType::ASSIGNMENT: return "ASSIGNMENT";
            case ASTNodeType::IF_STMT: return "IF_STMT";
            case ASTNodeType::WHILE_LOOP: return "WHILE_LOOP";
            case ASTNodeType::FOR_LOOP: return "FOR_LOOP";
            case ASTNodeType::PRINT_STMT: return "PRINT_STMT";
            case ASTNodeType::RETURN_STMT: return "RETURN_STMT";
            case ASTNodeType::BODY: return "BODY";
            case ASTNodeType::EXPRESSION_LIST: return "EXPRESSION_LIST";
            case ASTNodeType::PARAMETER_LIST: return "PARAMETER_LIST";
            case ASTNodeType::ARGUMENT_LIST: return "ARGUMENT_LIST";
            case ASTNodeType::RANGE: return "RANGE";
            default: return "UNKNOWN";
        }
    }
bool WasmCompiler::compile(std::shared_ptr<ASTNode> ast, const std::string& filename) {
    std::cout << "🚀 COMPILING TO WASM: " << filename << std::endl;
    
    // Extract the actual return value from AST
    int returnValue = extractReturnValueFromAST(ast);
    std::cout << "🔍 EXTRACTED RETURN VALUE: " << returnValue << std::endl;
    
    // Find main function for type analysis
    auto mainFunc = findMainFunction(ast);
    if (!mainFunc) {
        std::cerr << "❌ No main function found!" << std::endl;
        return false;
    }
    
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "❌ Cannot create file: " << filename << std::endl;
        return false;
    }
    
    // Pass mainFunc to generateWasmWithReturnValue for type analysis
    generateWasmWithReturnValue(file, returnValue, mainFunc);
    
    std::cout << "✅ WROTE WASM returning: " << returnValue << std::endl;
    return true;
}

void WasmCompiler::generateWasmWithReturnValue(std::ofstream& file, int returnValue, std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "🔧 BUILDING WASM SECTIONS DYNAMICALLY..." << std::endl;
    
    // WASM header
    uint8_t header[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    file.write(reinterpret_cast<char*>(header), sizeof(header));
    
    // Build and write each section (pass mainFunc for type analysis)
    auto typeSection = buildTypeSection(mainFunc);
    file.write(reinterpret_cast<char*>(typeSection.data()), typeSection.size());
    
    auto functionSection = buildFunctionSection();
    file.write(reinterpret_cast<char*>(functionSection.data()), functionSection.size());
    
    auto exportSection = buildExportSection();
    file.write(reinterpret_cast<char*>(exportSection.data()), exportSection.size());
    
    auto codeSection = buildCodeSection(returnValue, mainFunc);
    file.write(reinterpret_cast<char*>(codeSection.data()), codeSection.size());
    
    std::cout << "✅ DYNAMIC WASM GENERATION COMPLETE!" << std::endl;
}

std::vector<uint8_t> WasmCompiler::buildTypeSection(std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "  🔧 Building TYPE section from AST..." << std::endl;
    
    std::vector<uint8_t> section;
    section.push_back(0x01); // Type section ID
    
    // Analyze function signature from AST
    auto functionType = analyzeFunctionSignature(mainFunc);
    
    writeLeb128(section, functionType.size());
    section.insert(section.end(), functionType.begin(), functionType.end());
    
    return section;
}

std::vector<uint8_t> WasmCompiler::analyzeFunctionSignature(std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "  🔍 Analyzing function signature..." << std::endl;
    
    std::vector<uint8_t> typeContents;
    typeContents.push_back(0x01); // 1 type for now
    
    // Function type
    typeContents.push_back(0x60); // func type
    
    // Analyze parameters
    auto paramTypes = analyzeParameters(mainFunc);
    writeLeb128(typeContents, paramTypes.size()); // param count
    typeContents.insert(typeContents.end(), paramTypes.begin(), paramTypes.end());
    
    // Analyze return type  
    auto returnTypes = analyzeReturnType(mainFunc);
    writeLeb128(typeContents, returnTypes.size()); // return count
    typeContents.insert(typeContents.end(), returnTypes.begin(), returnTypes.end());
    
    return typeContents;
}

std::vector<uint8_t> WasmCompiler::analyzeParameters(std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "  🔍 Analyzing parameters..." << std::endl;
    
    std::vector<uint8_t> paramTypes;
    
    // DEBUG: Show all children to find the parameter list
    std::cout << "  🔍 Main function children:" << std::endl;
    for (size_t i = 0; i < mainFunc->children.size(); ++i) {
        auto child = mainFunc->children[i];
        if (child) {
            std::cout << "    [" << i << "] " << astNodeTypeToString(child->type) 
                      << " value: '" << child->value << "'" << std::endl;
        }
    }
    
    // Look for PARAMETER_LIST in main function children
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::PARAMETER_LIST) {
            std::cout << "  ✅ Found PARAMETER_LIST with " << child->children.size() << " parameters" << std::endl;
            
            for (auto& param : child->children) {
                if (param && param->type == ASTNodeType::PARAMETER) {
                    // Extract parameter type
                    auto wasmType = extractTypeFromParam(param);
                    paramTypes.push_back(wasmType);
                    std::cout << "    📊 Parameter '" << param->value << "' type: 0x" 
                              << std::hex << (int)wasmType << std::dec << std::endl;
                }
            }
            break;
        }
    }
    
    if (paramTypes.empty()) {
        std::cout << "  ℹ️ No parameters found - using empty parameter list" << std::endl;
    }
    
    return paramTypes;
}

std::vector<uint8_t> WasmCompiler::analyzeReturnType(std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "  🔍 Analyzing return type..." << std::endl;
    
    std::vector<uint8_t> returnTypes;
    
    // Look for return type declaration in main function children
    bool foundReturnType = false;
    
    for (size_t i = 0; i < mainFunc->children.size(); ++i) {
        auto child = mainFunc->children[i];
        
        if (child && child->type == ASTNodeType::USER_TYPE) {
            std::cout << "  ✅ Found return type: " << child->value << std::endl;
            auto wasmType = mapTypeToWasm(child->value);
            returnTypes.push_back(wasmType);
            foundReturnType = true;
            break;
        }
    }
    
    // If no explicit return type, infer from return statement
    if (!foundReturnType) {
        std::cout << "  🔍 No explicit return type - inferring from return statement..." << std::endl;
        auto inferredType = inferReturnTypeFromBody(mainFunc);
        returnTypes.push_back(inferredType);
    }
    
    return returnTypes;
}

uint8_t WasmCompiler::extractTypeFromParam(std::shared_ptr<ASTNode> param) {
    if (!param || param->children.empty()) {
        std::cout << "    ❌ Parameter has no type info - defaulting to i32" << std::endl;
        return 0x7f; // i32
    }
    
    auto typeNode = param->children[0];
    if (!typeNode) {
        return 0x7f; // i32 default
    }
    
    if (typeNode->type == ASTNodeType::USER_TYPE) {
        return mapTypeToWasm(typeNode->value);
    } else if (typeNode->type == ASTNodeType::PRIMITIVE_TYPE) {
        return mapTypeToWasm(typeNode->value);
    }
    
    std::cout << "    ❓ Unknown parameter type - defaulting to i32" << std::endl;
    return 0x7f; // i32 default
}

uint8_t WasmCompiler::inferReturnTypeFromBody(std::shared_ptr<ASTNode> mainFunc) {
    // Look for return statement in body
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            for (auto& stmt : child->children) {
                if (stmt && stmt->type == ASTNodeType::RETURN_STMT) {
                    if (!stmt->children.empty()) {
                        auto returnExpr = stmt->children[0];
                        if (returnExpr) {
                            std::cout << "    🔍 Return expression: " 
                                      << astNodeTypeToString(returnExpr->type) 
                                      << " value: '" << returnExpr->value << "'" << std::endl;
                            
                            switch (returnExpr->type) {
                                case ASTNodeType::LITERAL_INT:
                                    std::cout << "    ✅ Inferred return type: i32 (from integer literal)" << std::endl;
                                    return 0x7f; // i32
                                case ASTNodeType::LITERAL_REAL:
                                    std::cout << "    ✅ Inferred return type: f64 (from real literal)" << std::endl;
                                    return 0x7c; // f64
                                case ASTNodeType::LITERAL_BOOL:
                                    std::cout << "    ✅ Inferred return type: i32 (from boolean literal)" << std::endl;
                                    return 0x7f; // i32 (WASM uses i32 for bool)
                                case ASTNodeType::IDENTIFIER:
                                    std::cout << "    🔍 Identifier return - need type analysis" << std::endl;
                                    return inferTypeFromIdentifier(returnExpr->value, mainFunc);
                                default:
                                    std::cout << "    ❓ Unknown return expression type: " 
                                              << astNodeTypeToString(returnExpr->type) 
                                              << " - defaulting to i32" << std::endl;
                                    return 0x7f; // i32
                            }
                        }
                    }
                    break;
                }
            }
            break;
        }
    }
    
    std::cout << "    ❓ Could not infer return type - defaulting to i32" << std::endl;
    return 0x7f; // i32 default
}

uint8_t WasmCompiler::inferTypeFromIdentifier(const std::string& name, std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "    🔍 Inferring type for identifier: " << name << std::endl;
    
    // Check if it's a parameter
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::PARAMETER_LIST) {
            for (auto& param : child->children) {
                if (param && param->type == ASTNodeType::PARAMETER && param->value == name) {
                    auto wasmType = extractTypeFromParam(param);
                    std::cout << "    ✅ Found parameter type: 0x" << std::hex << (int)wasmType << std::dec << std::endl;
                    return wasmType;
                }
            }
        }
    }
    
    // TODO: Check local variables when we implement them
    
    std::cout << "    ❓ Unknown identifier type - defaulting to i32" << std::endl;
    return 0x7f; // i32 default
}

uint8_t WasmCompiler::mapTypeToWasm(const std::string& typeName) {
    if (typeName == "integer" || typeName == "int") {
        return 0x7f; // i32
    } else if (typeName == "real" || typeName == "float" || typeName == "double") {
        return 0x7c; // f64
    } else if (typeName == "boolean" || typeName == "bool") {
        return 0x7f; // i32 (WASM uses i32 for bool)
    } else {
        std::cout << "    ❓ Unknown type '" << typeName << "' - defaulting to i32" << std::endl;
        return 0x7f; // i32 default
    }
}

std::vector<uint8_t> WasmCompiler::buildFunctionSection() {
    std::cout << "  🔧 Building FUNCTION section..." << std::endl;
    
    std::vector<uint8_t> section;
    section.push_back(0x03); // Function section ID
    
    // Function contents: 1 function using type index 0
    std::vector<uint8_t> funcContents;
    funcContents.push_back(0x01); // 1 function
    funcContents.push_back(0x00); // type index 0
    
    writeLeb128(section, funcContents.size());
    section.insert(section.end(), funcContents.begin(), funcContents.end());
    
    return section;
}

std::vector<uint8_t> WasmCompiler::buildExportSection() {
    std::cout << "  🔧 Building EXPORT section..." << std::endl;
    
    std::vector<uint8_t> section;
    section.push_back(0x07); // Export section ID
    
    // Export contents: export "main" function
    std::vector<uint8_t> exportContents;
    exportContents.push_back(0x01); // 1 export
    
    // Export "main" function
    writeString(exportContents, "main");
    exportContents.push_back(0x00); // export kind (function)
    exportContents.push_back(0x00); // function index 0
    
    writeLeb128(section, exportContents.size());
    section.insert(section.end(), exportContents.begin(), exportContents.end());
    
    return section;
}

std::vector<uint8_t> WasmCompiler::buildCodeSection(int returnValue, std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "  🔧 Building CODE section with locals..." << std::endl;
    
    std::vector<uint8_t> section;
    section.push_back(0x0a); // Code section ID
    
    std::vector<uint8_t> codeContents;
    codeContents.push_back(0x01); // 1 function
    
    // Analyze local variables
    auto localTypes = analyzeLocalVariables(mainFunc);
    
    std::vector<uint8_t> funcBody;
    
    // Local variable declarations
    if (!localTypes.empty()) {
        writeLeb128(funcBody, localTypes.size()); // local count
        for (auto type : localTypes) {
            writeLeb128(funcBody, 1); // count of this type
            funcBody.push_back(type); // type
        }
    } else {
        funcBody.push_back(0x00); // 0 locals
    }
    
    // Generate variable initialization and main logic
    generateLocalVariableInitialization(funcBody, mainFunc);
    generateMainLogic(funcBody, mainFunc);
    
    // CRITICAL: Add explicit return instruction if function has return type
    auto returnTypes = analyzeReturnType(mainFunc);
    if (!returnTypes.empty()) {
        // If we have a return type, ensure there's a value on the stack
        // The return is implicit in WASM - the value left on stack is returned
        std::cout << "  🔧 Function has return type - ensuring return value on stack" << std::endl;
    }
    
    // Add the end instruction for the function body
    funcBody.push_back(0x0b); // end
    
    writeLeb128(codeContents, funcBody.size());
    codeContents.insert(codeContents.end(), funcBody.begin(), funcBody.end());
    
    writeLeb128(section, codeContents.size());
    section.insert(section.end(), codeContents.begin(), codeContents.end());
    
    return section;
}

void WasmCompiler::generateConstantInstruction(std::vector<uint8_t>& funcBody, uint8_t type, std::shared_ptr<ASTNode> mainFunc) {
    // Extract the actual literal value from return statement
    auto literalValue = extractLiteralValueFromReturn(mainFunc);
    
    switch (type) {
        case 0x7f: // i32 (integer or boolean)
            generateI32Constant(funcBody, literalValue, mainFunc);
            break;
        case 0x7c: // f64 (real)
            generateF64Constant(funcBody, literalValue, mainFunc);
            break;
        default:
            std::cout << "  ❓ Unknown type 0x" << std::hex << (int)type << std::dec 
                      << " - defaulting to i32" << std::endl;
            generateI32Constant(funcBody, literalValue, mainFunc);
            break;
    }
    
    funcBody.push_back(0x0b); // end
}

void WasmCompiler::generateI32Constant(std::vector<uint8_t>& funcBody, const std::string& value, std::shared_ptr<ASTNode> mainFunc) {
    funcBody.push_back(0x41); // i32.const opcode
    
    // Check if it's boolean or integer
    auto returnExpr = getReturnExpression(mainFunc);
    if (returnExpr && returnExpr->type == ASTNodeType::LITERAL_BOOL) {
        // Boolean: true=1, false=0
        int boolValue = (value == "true") ? 1 : 0;
        writeLeb128(funcBody, boolValue);
        std::cout << "  🔍 Instructions: i32.const " << boolValue << " (boolean '" << value << "'), end" << std::endl;
    } else {
        // Integer
        try {
            int intValue = std::stoi(value);
            writeLeb128(funcBody, intValue);
            std::cout << "  🔍 Instructions: i32.const " << intValue << " (integer), end" << std::endl;
        } catch (...) {
            std::cout << "  ❌ Failed to parse integer: " << value << " - using 0" << std::endl;
            writeLeb128(funcBody, 0);
        }
    }
}

void WasmCompiler::generateF64Constant(std::vector<uint8_t>& funcBody, const std::string& value, std::shared_ptr<ASTNode> mainFunc) {
    funcBody.push_back(0x44); // f64.const opcode
    
    try {
        double realValue = std::stod(value);
        
        // Convert double to 8 bytes (IEEE 754)
        uint64_t bits;
        memcpy(&bits, &realValue, sizeof(bits));
        
        // Write little-endian bytes
        for (int i = 0; i < 8; i++) {
            funcBody.push_back((bits >> (i * 8)) & 0xFF);
        }
        
        std::cout << "  🔍 Instructions: f64.const " << realValue << " (real), end" << std::endl;
        
    } catch (...) {
        std::cout << "  ❌ Failed to parse real: " << value << " - using 0.0" << std::endl;
        // Write 0.0 as double
        for (int i = 0; i < 8; i++) {
            funcBody.push_back(0x00);
        }
    }
}

std::string WasmCompiler::extractLiteralValueFromReturn(std::shared_ptr<ASTNode> mainFunc) {
    // Find return statement and extract the literal value
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            for (auto& stmt : child->children) {
                if (stmt && stmt->type == ASTNodeType::RETURN_STMT) {
                    if (!stmt->children.empty()) {
                        auto returnExpr = stmt->children[0];
                        if (returnExpr && (returnExpr->type == ASTNodeType::LITERAL_INT || 
                                          returnExpr->type == ASTNodeType::LITERAL_REAL ||
                                          returnExpr->type == ASTNodeType::LITERAL_BOOL)) {
                            return returnExpr->value;
                        }
                    }
                    break;
                }
            }
            break;
        }
    }
    return "0"; // fallback
}

std::shared_ptr<ASTNode> WasmCompiler::getReturnExpression(std::shared_ptr<ASTNode> mainFunc) {
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            for (auto& stmt : child->children) {
                if (stmt && stmt->type == ASTNodeType::RETURN_STMT) {
                    if (!stmt->children.empty()) {
                        return stmt->children[0];
                    }
                    break;
                }
            }
            break;
        }
    }
    return nullptr;
}

bool WasmCompiler::shouldGenerateParameterAccess(std::shared_ptr<ASTNode> mainFunc) {
    // Check if function has parameters
    bool hasParams = false;
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::PARAMETER_LIST) {
            hasParams = !child->children.empty();
            break;
        }
    }
    
    if (!hasParams) return false;
    
    // Check if return statement uses a parameter
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            for (auto& stmt : child->children) {
                if (stmt && stmt->type == ASTNodeType::RETURN_STMT) {
                    if (!stmt->children.empty()) {
                        auto returnExpr = stmt->children[0];
                        if (returnExpr && returnExpr->type == ASTNodeType::IDENTIFIER) {
                            std::cout << "  ✅ Return statement uses parameter: " << returnExpr->value << std::endl;
                            return true;
                        }
                    }
                    break;
                }
            }
            break;
        }
    }
    
    return false;
}
void WasmCompiler::writeLeb128(std::vector<uint8_t>& vec, uint32_t value) {
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) byte |= 0x80;
        vec.push_back(byte);
    } while (value != 0);
}

void WasmCompiler::writeString(std::vector<uint8_t>& vec, const std::string& str) {
    writeLeb128(vec, str.length());
    for (char c : str) {
        vec.push_back(static_cast<uint8_t>(c));
    }
}

std::vector<uint8_t> WasmCompiler::analyzeLocalVariables(std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "  🔍 Analyzing local variables..." << std::endl;
    
    std::vector<uint8_t> localTypes;
    localVarIndices.clear();
    
    // Start local indices after parameters
    int localIndex = countParameters(mainFunc); 
    
    // Find the BODY node in main function
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            // Scan for VAR_DECL nodes in the body
            for (auto& stmt : child->children) {
                if (stmt && stmt->type == ASTNodeType::VAR_DECL) {
                    std::string varName = stmt->value;
                    uint8_t varType = inferLocalVariableType(stmt);
                    
                    // Track this local variable
                    localVarIndices[varName] = localIndex++;
                    localTypes.push_back(varType);
                    
                    std::cout << "    📊 Local variable '" << varName 
                              << "' -> local[" << localVarIndices[varName] 
                              << "], type: 0x" << std::hex << (int)varType << std::dec << std::endl;
                }
            }
            break;
        }
    }
    
    return localTypes;
}
uint8_t WasmCompiler::inferLocalVariableType(std::shared_ptr<ASTNode> varDecl) {
    if (!varDecl || varDecl->children.empty()) {
        return 0x7f; // i32 default
    }
    
    // Check type declaration (first child)
    auto typeNode = varDecl->children[0];
    if (typeNode) {
        if (typeNode->type == ASTNodeType::PRIMITIVE_TYPE) {
            return mapTypeToWasm(typeNode->value);
        } else if (typeNode->type == ASTNodeType::USER_TYPE) {
            return mapTypeToWasm(typeNode->value);
        }
    }
    
    // Check initialization expression for type inference
    if (varDecl->children.size() > 1) {
        auto initExpr = varDecl->children[1];
        if (initExpr) {
            switch (initExpr->type) {
                case ASTNodeType::LITERAL_INT: return 0x7f; // i32
                case ASTNodeType::LITERAL_REAL: return 0x7c; // f64  
                case ASTNodeType::LITERAL_BOOL: return 0x7f; // i32
                default: break;
            }
        }
    }
    
    return 0x7f; // i32 default
}

int WasmCompiler::countParameters(std::shared_ptr<ASTNode> mainFunc) {
    int paramCount = 0;
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::PARAMETER_LIST) {
            paramCount = child->children.size();
            break;
        }
    }
    std::cout << "  🔍 Found " << paramCount << " parameters" << std::endl;
    return paramCount;
}

void WasmCompiler::generateLocalVariableInitialization(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "  🔧 Generating local variable initialization..." << std::endl;
    
    // Find the BODY node
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            // Process each statement to find variable declarations with initializers
            for (auto& stmt : child->children) {
                if (stmt && stmt->type == ASTNodeType::VAR_DECL && stmt->children.size() > 1) {
                    // This variable has an initializer!
                    std::string varName = stmt->value;
                    auto initExpr = stmt->children[1]; // Second child is initializer
                    
                    if (initExpr) {
                        std::cout << "    🔧 Initializing variable '" << varName << "'" << std::endl;
                        
                        // Generate the initialization expression
                        generateExpression(funcBody, initExpr, mainFunc);
                        
                        // Store to local variable
                        int varIndex = localVarIndices[varName];
                        funcBody.push_back(0x21); // local.set
                        writeLeb128(funcBody, varIndex);
                    }
                }
            }
            break;
        }
    }
}


void WasmCompiler::generateMainLogic(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "  🔧 Generating main logic..." << std::endl;
    
    // Find the BODY node
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            // Process each statement (skip VAR_DECL since we handled initialization)
            for (auto& stmt : child->children) {
                if (!stmt) continue;
                
                switch (stmt->type) {
                    case ASTNodeType::ASSIGNMENT:
                        generateAssignment(funcBody, stmt, mainFunc);
                        break;
                    case ASTNodeType::RETURN_STMT:
                        generateReturnStatement(funcBody, stmt, mainFunc);
                        break;
                    case ASTNodeType::VAR_DECL:
                        // Already handled in initialization - skip
                        break;
                    default:
                        std::cout << "    ⚠️  Unhandled statement type: " 
                                  << astNodeTypeToString(stmt->type) << std::endl;
                        break;
                }
            }
            break;
        }
    }
}

void WasmCompiler::generateExpression(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> expr, std::shared_ptr<ASTNode> mainFunc) {
    if (!expr) return;
    
    switch (expr->type) {
        case ASTNodeType::LITERAL_INT:
            funcBody.push_back(0x41); // i32.const
            writeLeb128(funcBody, std::stoi(expr->value));
            std::cout << "    🔧 Generated i32.const " << expr->value << std::endl;
            break;
        case ASTNodeType::LITERAL_BOOL :{
            funcBody.push_back(0x41); // i32.const (WASM uses i32 for bool)
            int boolValue = (expr->value == "true") ? 1 : 0;
            writeLeb128(funcBody, boolValue);
            std::cout << "    🔧 Generated i32.const " << boolValue << " (bool: " << expr->value << ")" << std::endl;
            break;
        }
        case ASTNodeType::IDENTIFIER:
            generateVariableLoad(funcBody, expr->value, mainFunc);
            break;
        default:
            std::cout << "    ⚠️  Unhandled expression type: " 
                      << astNodeTypeToString(expr->type) << std::endl;
            // Generate a default value to avoid empty expression
            funcBody.push_back(0x41); // i32.const 0
            writeLeb128(funcBody, 0);
            break;
    }
}
void WasmCompiler::generateVariableLoad(std::vector<uint8_t>& funcBody, const std::string& varName, std::shared_ptr<ASTNode> mainFunc) {
    if (localVarIndices.find(varName) != localVarIndices.end()) {
        // It's a local variable
        int varIndex = localVarIndices[varName];
        funcBody.push_back(0x20); // local.get
        writeLeb128(funcBody, varIndex);
        std::cout << "    🔧 Loading local variable '" << varName << "' from local[" << varIndex << "]" << std::endl;
    } else {
        // Check if it's a parameter
        // TODO: Implement parameter lookup
        std::cout << "    ⚠️  Variable '" << varName << "' not found in locals" << std::endl;
    }
}

void WasmCompiler::generateAssignment(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> assignment, std::shared_ptr<ASTNode> mainFunc) {
    if (assignment->children.size() < 2) return;
    
    auto target = assignment->children[0];
    auto value = assignment->children[1];
    
    if (target->type == ASTNodeType::IDENTIFIER) {
        // Generate the value expression
        generateExpression(funcBody, value, mainFunc);
        
        // Store to variable
        std::string varName = target->value;
        if (localVarIndices.find(varName) != localVarIndices.end()) {
            int varIndex = localVarIndices[varName];
            funcBody.push_back(0x21); // local.set
            writeLeb128(funcBody, varIndex);
            std::cout << "    🔧 Storing to local variable '" << varName << "' at local[" << varIndex << "]" << std::endl;
        }
    }
}

void WasmCompiler::generateReturnStatement(std::vector<uint8_t>& funcBody, std::shared_ptr<ASTNode> returnStmt, std::shared_ptr<ASTNode> mainFunc) {
    if (!returnStmt->children.empty()) {
        auto returnExpr = returnStmt->children[0];
        generateExpression(funcBody, returnExpr, mainFunc);
    }
    // Return is implicit in WASM - value is left on stack
}

int WasmCompiler::extractReturnValueFromAST(std::shared_ptr<ASTNode> ast) {
    std::cout << "🔍 SEARCHING FOR MAIN FUNCTION IN AST..." << std::endl;
    
    if (!ast || ast->type != ASTNodeType::PROGRAM) {
        std::cout << "❌ Invalid AST structure!" << std::endl;
        return 42;
    }
    
    std::cout << "📊 PROGRAM has " << ast->children.size() << " children" << std::endl;
    
    auto mainFunc = findMainFunction(ast);
    if (!mainFunc) {
        std::cout << "❌ No main function found!" << std::endl;
        return 42;
    }
    
    std::cout << "✅ FOUND main function!" << std::endl;
    return extractIntegerFromReturn(mainFunc);
}

std::shared_ptr<ASTNode> WasmCompiler::findMainFunction(std::shared_ptr<ASTNode> program) {
    for (size_t i = 0; i < program->children.size(); ++i) {
        auto child = program->children[i];
        if (!child) continue;
        
        std::cout << "  🔍 Child " << i << ": " << astNodeTypeToString(child->type) 
                  << " value: '" << child->value << "'" << std::endl;
        
        if (child->type == ASTNodeType::ROUTINE_DECL && child->value == "main") {
            std::cout << "  ✅ FOUND main routine!" << std::endl;
            return child;
        }
    }
    return nullptr;
}

int WasmCompiler::extractIntegerFromReturn(std::shared_ptr<ASTNode> mainFunc) {
    std::cout << "🔍 EXTRACTING RETURN VALUE FROM MAIN FUNCTION..." << std::endl;
    
    if (mainFunc->children.empty()) {
        std::cout << "❌ Main function has no children!" << std::endl;
        return 42;
    }
    
    // Find the BODY node
    std::shared_ptr<ASTNode> body = nullptr;
    for (auto& child : mainFunc->children) {
        if (child && child->type == ASTNodeType::BODY) {
            body = child;
            break;
        }
    }
    
    if (!body || body->type != ASTNodeType::BODY || body->children.empty()) {
        std::cout << "❌ Invalid body structure!" << std::endl;
        return 42;
    }
    
    auto returnStmt = body->children[0];
    if (!returnStmt || returnStmt->type != ASTNodeType::RETURN_STMT || returnStmt->children.empty()) {
        std::cout << "❌ Invalid return statement!" << std::endl;
        return 42;
    }
    
    auto returnExpr = returnStmt->children[0];
    if (!returnExpr) {
        std::cout << "❌ Return expression is null!" << std::endl;
        return 42;
    }
    
    std::cout << "🔍 Return expression: " << astNodeTypeToString(returnExpr->type) 
              << " value: '" << returnExpr->value << "'" << std::endl;
    
    // Handle boolean literals
    if (returnExpr->type == ASTNodeType::LITERAL_BOOL) {
        int value = (returnExpr->value == "true") ? 1 : 0;
        std::cout << "✅ EXTRACTED BOOLEAN: " << returnExpr->value << " -> " << value << std::endl;
        return value;
    }
    
    // If we're returning a parameter (IDENTIFIER), we can't extract a hardcoded value!
    if (returnExpr->type == ASTNodeType::IDENTIFIER) {
        std::cout << "✅ Returning parameter: " << returnExpr->value << " - no hardcoded value needed!" << std::endl;
        return 0; // This value won't be used since we'll generate get_local instead
    }
    
    if (returnExpr->type == ASTNodeType::LITERAL_INT) {
        try {
            int value = std::stoi(returnExpr->value);
            std::cout << "✅ EXTRACTED INTEGER: " << value << std::endl;
            return value;
        } catch (...) {
            std::cout << "❌ Failed to parse integer: " << returnExpr->value << std::endl;
            return 42;
        }
    } else {
        std::cout << "❌ Expected LITERAL_INT, LITERAL_BOOL or IDENTIFIER, got: " << astNodeTypeToString(returnExpr->type) << std::endl;
        return 42;
    }
}