
#ifndef SEMANTICS_H
#define SEMANTICS_H

#include "ast.h"
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>

class SemanticAnalyzer {
private:
    std::unordered_map<std::string, int> arraySizes;
    std::unordered_map<std::string, int> variableValues;
    std::unordered_map<std::string, bool> variableKnown;
    std::unordered_map<std::string, std::pair<int, int>> variableRanges;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

public:
    bool analyze(std::shared_ptr<ASTNode> ast) {
        std::cout << "=== STARTING SEMANTIC ANALYSIS ===\n";
        
        if (!ast) {
            error("AST is null");
            return false;
        }
        
        collectArrayDeclarations(ast);
        return checkSemantics(ast);
    }

private:
    void collectArrayDeclarations(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        if (node->type == ASTNodeType::VAR_DECL) {
            std::string varName = node->value;
            
            if (node->children.size() > 0 && node->children[0] && 
                node->children[0]->type == ASTNodeType::ARRAY_TYPE) {
                
                auto arrayType = node->children[0];
                if (arrayType->children.size() > 0 && arrayType->children[0]) {
                    auto sizeExpr = arrayType->children[0];
                    if (sizeExpr->type == ASTNodeType::LITERAL_INT) {
                        try {
                            int size = std::stoi(sizeExpr->value);
                            arraySizes[varName] = size;
                            std::cout << "Found array declaration: " << varName 
                                      << " with size " << size << std::endl;
                        } catch (...) {
                            warning("Could not parse array size for '" + varName + "'");
                        }
                    }
                }
            }
        }
        
        for (auto& child : node->children) {
            collectArrayDeclarations(child);
        }
    }

    bool checkSemantics(std::shared_ptr<ASTNode> node) {
        if (!node) return true;
        
        bool success = true;
        
        if (node->type == ASTNodeType::ASSIGNMENT) {
            trackVariableAssignment(node);
        }
        
        if (node->type == ASTNodeType::FOR_LOOP) {
            trackForLoopRange(node);
        }
        
        if (node->type == ASTNodeType::ARRAY_ACCESS) {
            std::cout << "=== CHECKING ARRAY BOUNDS ===" << std::endl;
            if (!checkArrayBounds(node)) {
                success = false;
            }
        }
        
        for (auto& child : node->children) {
            if (!checkSemantics(child)) {
                success = false;
            }
        }
        
        if (node->type == ASTNodeType::FOR_LOOP) {
            cleanupLoopVariable(node);
        }
        
        return success;
    }

    void trackForLoopRange(std::shared_ptr<ASTNode> forLoop) {
        if (!forLoop || forLoop->children.size() < 1) {
            std::cout << "DEBUG: ForLoop node has insufficient children: " 
                      << (forLoop ? forLoop->children.size() : 0) << std::endl;
            return;
        }
        
        std::string loopVar = forLoop->value;
        std::cout << "DEBUG: Processing ForLoop with variable: " << loopVar << std::endl;
        std::cout << "DEBUG: ForLoop children count: " << forLoop->children.size() << std::endl;
        
        // The range should be the first child
        auto rangeNode = forLoop->children[0];
        
        if (!rangeNode) {
            std::cout << "DEBUG: Range node is null" << std::endl;
            return;
        }
        
        std::cout << "DEBUG: Range node type: " << static_cast<int>(rangeNode->type) 
                  << " (expected: " << static_cast<int>(ASTNodeType::RANGE) << ")" << std::endl;
        
        if (rangeNode->type == ASTNodeType::RANGE) {
            if (rangeNode->children.size() >= 2) {
                auto startNode = rangeNode->children[0];
                auto endNode = rangeNode->children[1];
                
                std::cout << "DEBUG: Start node type: " << (startNode ? static_cast<int>(startNode->type) : -1) << std::endl;
                std::cout << "DEBUG: End node type: " << (endNode ? static_cast<int>(endNode->type) : -1) << std::endl;
                
                if (startNode && startNode->type == ASTNodeType::LITERAL_INT &&
                    endNode && endNode->type == ASTNodeType::LITERAL_INT) {
                    
                    try {
                        int start = std::stoi(startNode->value);
                        int end = std::stoi(endNode->value);
                        
                        variableRanges[loopVar] = {start, end};
                        std::cout << "🎯 LOOP RANGE TRACKED: " << loopVar << " in [" 
                                  << start << ".." << end << "]" << std::endl;
                        
                    } catch (...) {
                        warning("Could not parse loop range for '" + loopVar + "'");
                    }
                } else {
                    std::cout << "DEBUG: Range bounds are not literal integers" << std::endl;
                }
            } else {
                std::cout << "DEBUG: Range node has insufficient children: " 
                          << rangeNode->children.size() << std::endl;
            }
        } else {
            std::cout << "DEBUG: First child is not a RANGE node" << std::endl;
        }
    }

    void cleanupLoopVariable(std::shared_ptr<ASTNode> forLoop) {
        std::string loopVar = forLoop->value;
        variableRanges.erase(loopVar);
        std::cout << "DEBUG: Cleaned up loop variable: " << loopVar << std::endl;
    }

    void trackVariableAssignment(std::shared_ptr<ASTNode> assignment) {
        if (!assignment || assignment->children.size() < 2) return;
        
        auto left = assignment->children[0];
        auto right = assignment->children[1];
        
        if (left && left->type == ASTNodeType::IDENTIFIER && right) {
            std::string varName = left->value;
            
            if (right->type == ASTNodeType::LITERAL_INT) {
                try {
                    int value = std::stoi(right->value);
                    variableValues[varName] = value;
                    variableKnown[varName] = true;
                    std::cout << "TRACKING: " << varName << " = " << value << std::endl;
                } catch (...) {
                    variableKnown[varName] = false;
                }
            } else {
                variableKnown[varName] = false;
                std::cout << "UNKNOWN VALUE: " << varName << " (complex expression)" << std::endl;
            }
        }
    }

    bool checkArrayBounds(std::shared_ptr<ASTNode> arrayAccess) {
        if (!arrayAccess || arrayAccess->type != ASTNodeType::ARRAY_ACCESS) {
            return true;
        }
        
        std::cout << "Found array access - checking bounds...\n";
        
        if (arrayAccess->children.size() >= 2) {
            auto array = arrayAccess->children[0];
            auto index = arrayAccess->children[1];
            
            if (array && array->type == ASTNodeType::IDENTIFIER) {
                std::string arrayName = array->value;
                
                if (arraySizes.find(arrayName) != arraySizes.end()) {
                    int arraySize = arraySizes[arrayName];
                    
                    if (index && index->type == ASTNodeType::LITERAL_INT) {
                        int idx = std::stoi(index->value);
                        std::cout << "Static index check: " << idx 
                                  << " for array '" << arrayName 
                                  << "' of size " << arraySize << "\n";
                        
                        if (idx < 0 || idx >= arraySize) {
                            error("Array index " + std::to_string(idx) + 
                                  " out of bounds for array '" + arrayName + 
                                  "' of size " + std::to_string(arraySize));
                            return false;
                        } else {
                            std::cout << "✓ Array bound check PASSED\n";
                            return true;
                        }
                    }
                    else if (index && index->type == ASTNodeType::IDENTIFIER) {
                        std::string indexVar = index->value;
                        
                        if (variableKnown[indexVar]) {
                            int idx = variableValues[indexVar];
                            std::cout << "Variable index check: " << indexVar << " = " << idx
                                      << " for array '" << arrayName 
                                      << "' of size " << arraySize << "\n";
                            
                            if (idx < 0 || idx >= arraySize) {
                                error("Array index " + std::to_string(idx) + 
                                      " (variable '" + indexVar + "') out of bounds for array '" + 
                                      arrayName + "' of size " + std::to_string(arraySize));
                                return false;
                            } else {
                                std::cout << "✓ Variable array bound check PASSED\n";
                                return true;
                            }
                        }
                        else if (variableRanges.find(indexVar) != variableRanges.end()) {
                            auto range = variableRanges[indexVar];
                            int start = range.first;
                            int end = range.second;
                            
                            std::cout << "🔥 LOOP VARIABLE CHECK: " << indexVar 
                                      << " in [" << start << ".." << end << "]"
                                      << " for array '" << arrayName 
                                      << "' of size " << arraySize << "\n";
                            
                            if (start < 0 || end >= arraySize) {
                                error("Loop variable '" + indexVar + "' range [" + 
                                      std::to_string(start) + ".." + std::to_string(end) + 
                                      "] out of bounds for array '" + arrayName + 
                                      "' of size " + std::to_string(arraySize));
                                return false;
                            } else {
                                std::cout << "🎯 LOOP RANGE CHECK PASSED - ALL INDICES SAFE!\n";
                                return true;
                            }
                        }
                        else {
                            warning("Unknown index value for variable '" + indexVar + 
                                   "' - cannot verify bounds at compile time");
                            return true;
                        }
                    }
                    else {
                        warning("Complex index expression - cannot verify bounds at compile time");
                        return true;
                    }
                } else {
                    error("Undeclared array '" + arrayName + "'");
                    return false;
                }
            }
        }
        
        return true;
    }

    void error(const std::string& message) {
        errors.push_back(message);
        std::cerr << "ERROR: " << message << std::endl;
    }

    void warning(const std::string& message) {
        warnings.push_back(message);
        std::cout << "WARNING: " << message << std::endl;
    }
};

#endif
