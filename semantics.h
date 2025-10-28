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
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

public:
    bool analyze(std::shared_ptr<ASTNode> ast) {
        std::cout << "=== STARTING SEMANTIC ANALYSIS ===\n";
        
        if (!ast) {
            error("AST is null");
            return false;
        }
        
        // First pass: collect array declarations
        collectArrayDeclarations(ast);
        
        // Second pass: check array bounds and other semantics
        return checkSemantics(ast);
    }

private:
    void collectArrayDeclarations(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        // Look for array variable declarations
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
        
        // Recursively process children
        for (auto& child : node->children) {
            collectArrayDeclarations(child);
        }
    }

    bool checkSemantics(std::shared_ptr<ASTNode> node) {
        if (!node) return true;
        
        bool success = true;
        
        // SPECIAL CASE: Check array accesses when we find them
        if (node->type == ASTNodeType::ARRAY_ACCESS) {
            std::cout << "=== CHECKING ARRAY BOUNDS ===" << std::endl;
            if (!checkArrayBounds(node)) {
                success = false;
            }
        }
        
        // ALWAYS recursively check all children
        for (auto& child : node->children) {
            if (!checkSemantics(child)) {
                success = false;
            }
        }
        
        return success;
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
                    } else {
                        warning("Dynamic index for array '" + arrayName + 
                               "' - cannot verify bounds at compile time");
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