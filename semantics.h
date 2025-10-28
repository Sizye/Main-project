#ifndef SEMANTICS_H
#define SEMANTICS_H

#include "ast.h"
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <set>
#include <map>

class SemanticAnalyzer {
private:
    std::unordered_map<std::string, int> arraySizes;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    // UNIFIED usage tracking
    std::set<std::string> usedIdentifiers;
    std::set<std::string> declaredIdentifiers;
    
    // Loop analysis
    std::map<std::string, std::pair<int, int>> loopVariableRanges;
    
    // Type system - track type definitions
    std::unordered_map<std::string, std::shared_ptr<ASTNode>> typeDefinitions;

public:
    bool analyze(std::shared_ptr<ASTNode> ast) {
        std::cout << "=== STARTING SEMANTIC ANALYSIS ===\n";
        
        if (!ast) {
            error("AST is null");
            return false;
        }
        
        // PASS 0: Collect type definitions FIRST
        collectTypeDefinitions(ast);
        
        // PASS 1: Semantic checks with SMART analysis
        bool semanticSuccess = checkSemantics(ast);
        
        // PASS 2: Collect COMPLETE usage data
        collectCompleteUsage(ast);
        
        // PASS 3: Run SMART optimizations
        optimizeAST(ast);
        
        // PASS 4: Report optimization results
        reportOptimizations();
        
        return semanticSuccess;
    }

private:
    // ========== TYPE SYSTEM COLLECTION ==========
    
    void collectTypeDefinitions(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        // Collect type declarations
        if (node->type == ASTNodeType::TYPE_DECL) {
            std::string typeName = node->value;
            if (node->children.size() > 0 && node->children[0]) {
                typeDefinitions[typeName] = node->children[0];
                std::cout << "📋 Found type definition: " << typeName << std::endl;
            }
        }
        
        // Recursively collect from children
        for (auto& child : node->children) {
            collectTypeDefinitions(child);
        }
    }
    
    int getArraySizeFromType(std::shared_ptr<ASTNode> typeNode) {
        if (!typeNode) return -1;
        
        // Direct array type
        if (typeNode->type == ASTNodeType::ARRAY_TYPE) {
            if (typeNode->children.size() > 0 && typeNode->children[0]) {
                auto sizeExpr = typeNode->children[0];
                if (sizeExpr->type == ASTNodeType::LITERAL_INT) {
                    try {
                        return std::stoi(sizeExpr->value);
                    } catch (...) {
                        return -1;
                    }
                }
            }
            return -1; // Dynamic array size
        }
        // User type (alias) - follow the alias
        else if (typeNode->type == ASTNodeType::USER_TYPE) {
            std::string typeName = typeNode->value;
            if (typeDefinitions.find(typeName) != typeDefinitions.end()) {
                return getArraySizeFromType(typeDefinitions[typeName]);
            }
        }
        
        return -1; // Not an array type or unknown
    }

    // ========== SMART SEMANTIC CHECKS ==========
    
    bool checkSemantics(std::shared_ptr<ASTNode> node) {
        if (!node) return true;
        
        bool success = true;
        
        switch (node->type) {
            case ASTNodeType::VAR_DECL:
                collectArrayDeclaration(node);
                break;
                
            case ASTNodeType::FOR_LOOP:
                success = analyzeForLoop(node);
                break;
                
            case ASTNodeType::ARRAY_ACCESS:
                success = checkArrayBoundsSmart(node);
                break;
                
            default:
                break;
        }
        
        // Recursively check children
        for (auto& child : node->children) {
            if (!checkSemantics(child)) {
                success = false;
            }
        }
        
        // Clean up loop variables when exiting scope
        if (node->type == ASTNodeType::FOR_LOOP) {
            std::string loopVar = node->value;
            loopVariableRanges.erase(loopVar);
        }
        
        return success;
    }

    bool analyzeForLoop(std::shared_ptr<ASTNode> forLoop) {
        if (!forLoop || forLoop->type != ASTNodeType::FOR_LOOP) return true;
        
        std::string loopVar = forLoop->value;
        
        if (forLoop->children.size() > 0) {
            auto rangeNode = forLoop->children[0];
            if (rangeNode && rangeNode->type == ASTNodeType::RANGE) {
                if (rangeNode->children.size() >= 2) {
                    auto startNode = rangeNode->children[0];
                    auto endNode = rangeNode->children[1];
                    
                    if (startNode && endNode && 
                        startNode->type == ASTNodeType::LITERAL_INT &&
                        endNode->type == ASTNodeType::LITERAL_INT) {
                        
                        int start = std::stoi(startNode->value);
                        int end = std::stoi(endNode->value);
                        
                        loopVariableRanges[loopVar] = {start, end};
                        std::cout << "🎯 LOOP RANGE TRACKED: " << loopVar << " in [" << start << ".." << end << "]" << std::endl;
                    }
                }
            }
        }
        
        return true;
    }

    bool checkArrayBoundsSmart(std::shared_ptr<ASTNode> arrayAccess) {
        if (!arrayAccess || arrayAccess->type != ASTNodeType::ARRAY_ACCESS) {
            return true;
        }
        
        std::cout << "=== CHECKING ARRAY BOUNDS ===" << std::endl;
        
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
                        
                        if (loopVariableRanges.find(indexVar) != loopVariableRanges.end()) {
                            auto range = loopVariableRanges[indexVar];
                            int minIdx = range.first;
                            int maxIdx = range.second;
                            
                            std::cout << "🔥 LOOP VARIABLE CHECK: " << indexVar 
                                      << " in [" << minIdx << ".." << maxIdx 
                                      << "] for array '" << arrayName << "' of size " << arraySize << std::endl;
                            
                            if (minIdx >= 0 && maxIdx < arraySize) {
                                std::cout << "🎯 LOOP RANGE CHECK PASSED - ALL INDICES SAFE!" << std::endl;
                                return true;
                            } else {
                                error("Loop variable '" + indexVar + "' range [" + 
                                      std::to_string(minIdx) + ".." + std::to_string(maxIdx) + 
                                      "] may go out of bounds for array '" + arrayName + 
                                      "' of size " + std::to_string(arraySize));
                                return false;
                            }
                        } else {
                            std::cout << "Variable index check: " << indexVar 
                                      << " for array '" << arrayName << "' of size " << arraySize << std::endl;
                            warning("Dynamic index for array '" + arrayName + "' - cannot verify bounds at compile time");
                            return true;
                        }
                    }
                    else {
                        warning("Complex index expression for array '" + arrayName + "' - cannot verify bounds at compile time");
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

    void collectArrayDeclaration(std::shared_ptr<ASTNode> varDecl) {
        if (!varDecl || varDecl->type != ASTNodeType::VAR_DECL) return;
        
        std::string varName = varDecl->value;
        
        if (varDecl->children.size() > 0 && varDecl->children[0]) {
            auto typeNode = varDecl->children[0];
            int arraySize = getArraySizeFromType(typeNode);
            
            if (arraySize != -1) {
                arraySizes[varName] = arraySize;
                std::cout << "Found array declaration: " << varName 
                          << " with size " << arraySize << std::endl;
            }
        }
    }

    // ========== COMPLETE USAGE ANALYSIS ==========
    
    void collectCompleteUsage(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        switch (node->type) {
            case ASTNodeType::VAR_DECL:
                declaredIdentifiers.insert(node->value);
                std::cout << "Found declaration: " << node->value << std::endl;
                break;
                
            case ASTNodeType::IDENTIFIER:
                usedIdentifiers.insert(node->value);
                break;
                
            case ASTNodeType::MEMBER_ACCESS:
                // Track member access: people[i].id → mark 'id' as used
                usedIdentifiers.insert(node->value);
                std::cout << "Found member access: " << node->value << std::endl;
                // Also track the base object
                if (node->children.size() > 0) {
                    collectCompleteUsage(node->children[0]);
                }
                break;
                
            case ASTNodeType::ARRAY_ACCESS:
                // Track array base usage
                if (node->children.size() > 0 && node->children[0]) {
                    collectCompleteUsage(node->children[0]);
                }
                break;
                
            default:
                break;
        }
        
        // Recursively process ALL children
        for (auto& child : node->children) {
            collectCompleteUsage(child);
        }
    }
    
    bool isRecordFieldDeclaration(std::shared_ptr<ASTNode> varDecl) {
        if (varDecl->children.size() > 0 && varDecl->children[0]) {
            auto typeNode = varDecl->children[0];
            return typeNode->type == ASTNodeType::RECORD_TYPE;
        }
        return false;
    }
    
    // ========== SMART OPTIMIZATION ==========
    
    void optimizeAST(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        if (node->type == ASTNodeType::BODY || node->type == ASTNodeType::PROGRAM) {
            optimizeUnusedDeclarations(node);
        }
        
        for (auto& child : node->children) {
            optimizeAST(child);
        }
    }
    
    void optimizeUnusedDeclarations(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        std::vector<std::shared_ptr<ASTNode>> newChildren;
        int removedCount = 0;
        int preservedRecordFields = 0;
        
        for (auto& child : node->children) {
            if (child && child->type == ASTNodeType::VAR_DECL) {
                std::string varName = child->value;
                
                // Check if this declaration is used
                bool isUsed = (usedIdentifiers.find(varName) != usedIdentifiers.end());
                
                // Handle record fields specially
                if (isRecordFieldDeclaration(child)) {
                    if (isUsed) {
                        newChildren.push_back(child);
                        preservedRecordFields++;
                        std::cout << "💾 PRESERVING used record field: " << varName << std::endl;
                    } else {
                        std::cout << "🔥 OPTIMIZATION: Removing UNUSED record field '" << varName << "'" << std::endl;
                        removedCount++;
                    }
                } 
                // Handle regular variables
                else {
                    if (isUsed) {
                        newChildren.push_back(child);
                    } else {
                        std::cout << "🔥 OPTIMIZATION: Removing unused variable '" << varName << "'" << std::endl;
                        removedCount++;
                    }
                }
            } else {
                newChildren.push_back(child);
            }
        }
        
        if (removedCount > 0) {
            node->children = newChildren;
            std::cout << "🔥 Removed " << removedCount << " unused declaration(s)" << std::endl;
            if (preservedRecordFields > 0) {
                std::cout << "💾 Preserved " << preservedRecordFields << " used record field(s)" << std::endl;
            }
        }
    }
    
    void reportOptimizations() {
        std::cout << "\n=== OPTIMIZATION REPORT ===" << std::endl;
        
        std::vector<std::string> unusedVars;
        for (const auto& var : declaredIdentifiers) {
            if (usedIdentifiers.find(var) == usedIdentifiers.end()) {
                unusedVars.push_back(var);
            }
        }
        
        if (!unusedVars.empty()) {
            std::cout << "Unused declarations: ";
            for (size_t i = 0; i < unusedVars.size(); ++i) {
                std::cout << unusedVars[i];
                if (i < unusedVars.size() - 1) std::cout << ", ";
            }
            std::cout << std::endl;
        } else {
            std::cout << "✓ All declarations are used" << std::endl;
        }
        
        std::cout << "Total declarations: " << declaredIdentifiers.size() << std::endl;
        std::cout << "Total used identifiers: " << usedIdentifiers.size() << std::endl;
        std::cout << "Known array types: " << arraySizes.size() << std::endl;
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