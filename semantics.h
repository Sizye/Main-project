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
    // ========== CORE DATA STRUCTURES ==========
    std::unordered_map<std::string, int> arraySizes;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    // PRECISE usage tracking
    std::set<std::string> declaredIdentifiers;
    std::set<std::string> writtenVariables;
    std::set<std::string> readVariables;
    std::set<std::string> calledRoutines;
    
    // Track which identifiers are actually routines
    std::set<std::string> routineDeclarations;
    
    // Loop analysis
    std::map<std::string, std::pair<int, int>> loopVariableRanges;
    
    // Type system
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
        
        // PASS 1: Collect ALL declarations (including parameters)
        collectAllDeclarations(ast);
        
        // PASS 2: Semantic checks with SMART analysis  
        bool semanticSuccess = checkSemantics(ast);
        
        // PASS 3: Collect COMPLETE usage data
        collectCompleteUsage(ast);
        
        // DEBUG: Show what we tracked
        debugUsageTracking();
        
        // PASS 4: Run ADVANCED optimizations
        optimizeAST(ast);
        
        // PASS 5: Report optimization results
        reportOptimizations();
        
        return semanticSuccess && errors.empty();
    }

private:
    // ========== TYPE SYSTEM COLLECTION ==========
    void collectAllDeclarations(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        switch (node->type) {
            case ASTNodeType::VAR_DECL:
                declaredIdentifiers.insert(node->value);
                std::cout << "Found declaration: " << node->value << std::endl;
                break;
                
            case ASTNodeType::PARAMETER:
                declaredIdentifiers.insert(node->value);
                std::cout << "Found parameter: " << node->value << std::endl;
                break;
                
            case ASTNodeType::ROUTINE_DECL:
            case ASTNodeType::ROUTINE_FORWARD_DECL:
                declaredIdentifiers.insert(node->value);
                routineDeclarations.insert(node->value); // Track as routine
                std::cout << "Found routine: " << node->value << std::endl;
                break;
        }
        
        for (auto& child : node->children) {
            collectAllDeclarations(child);
        }
    }
    
    void collectTypeDefinitions(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        if (node->type == ASTNodeType::TYPE_DECL) {
            std::string typeName = node->value;
            if (node->children.size() > 0 && node->children[0]) {
                typeDefinitions[typeName] = node->children[0];
                std::cout << "📋 Found type definition: " << typeName << std::endl;
            }
        }
        
        for (auto& child : node->children) {
            collectTypeDefinitions(child);
        }
    }
    
    int getArraySizeFromType(std::shared_ptr<ASTNode> typeNode) {
        if (!typeNode) return -1;
        
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
            return -1;
        }
        else if (typeNode->type == ASTNodeType::USER_TYPE) {
            std::string typeName = typeNode->value;
            if (typeDefinitions.find(typeName) != typeDefinitions.end()) {
                return getArraySizeFromType(typeDefinitions[typeName]);
            }
        }
        
        return -1;
    }

    // ========== PRECISE SEMANTIC CHECKS ==========
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
                success = checkMultiDimArrayBounds(node);
                break;
                
            case ASTNodeType::ASSIGNMENT:
                if (node->children.size() > 0 && node->children[0]) {
                    trackVariableWrite(node->children[0]);
                }
                break;
                
            default:
                break;
        }
        
        for (auto& child : node->children) {
            if (!checkSemantics(child)) {
                success = false;
            }
        }
        
        if (node->type == ASTNodeType::FOR_LOOP) {
            std::string loopVar = node->value;
            loopVariableRanges.erase(loopVar);
        }
        
        return success;
    }

    void trackVariableWrite(std::shared_ptr<ASTNode> leftSide) {
        if (!leftSide) return;
        
        if (leftSide->type == ASTNodeType::IDENTIFIER) {
            writtenVariables.insert(leftSide->value);
            std::cout << "📝 TRACKED WRITE: " << leftSide->value << std::endl;
        }
        else if (leftSide->type == ASTNodeType::MEMBER_ACCESS) {
            writtenVariables.insert(leftSide->value);
            std::cout << "📝 TRACKED WRITE (field): " << leftSide->value << std::endl;
            if (leftSide->children.size() > 0 && leftSide->children[0]) {
                trackVariableWrite(leftSide->children[0]);
            }
        }
        else if (leftSide->type == ASTNodeType::ARRAY_ACCESS) {
            if (leftSide->children.size() > 0 && leftSide->children[0]) {
                trackVariableWrite(leftSide->children[0]);
            }
        }
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

    // ========== MULTI-DIMENSIONAL ARRAY BOUNDS CHECKING ==========
    bool checkMultiDimArrayBounds(std::shared_ptr<ASTNode> arrayAccess) {
        if (!arrayAccess || arrayAccess->type != ASTNodeType::ARRAY_ACCESS) {
            return true;
        }
        
        std::cout << "=== CHECKING MULTI-DIMENSIONAL ARRAY BOUNDS ===" << std::endl;
        return checkArrayAccessRecursive(arrayAccess, 1);
    }

    std::string getArrayAtCurrentLevel(std::shared_ptr<ASTNode> arrayAccess) {
        if (!arrayAccess || arrayAccess->type != ASTNodeType::ARRAY_ACCESS) return "";
        
        if (arrayAccess->children.size() > 0 && arrayAccess->children[0]) {
            auto leftChild = arrayAccess->children[0];
            
            if (leftChild->type == ASTNodeType::IDENTIFIER) {
                return leftChild->value;
            }
            else if (leftChild->type == ASTNodeType::MEMBER_ACCESS) {
                return leftChild->value;
            }
            else if (leftChild->type == ASTNodeType::ARRAY_ACCESS) {
                return getArrayAtCurrentLevel(leftChild);
            }
        }
        
        return "";
    }

    bool checkArrayAccessRecursive(std::shared_ptr<ASTNode> arrayAccess, int dimension) {
        if (!arrayAccess || arrayAccess->type != ASTNodeType::ARRAY_ACCESS) return true;
        
        std::string arrayName = getArrayAtCurrentLevel(arrayAccess);
        
        if (arrayName.empty()) {
            warning("Complex array access - cannot determine array for bounds checking");
            return true;
        }
        
        std::cout << "Checking access to array: '" << arrayName << "' at dimension " << dimension << std::endl;
        
        if (arraySizes.find(arrayName) != arraySizes.end()) {
            int arraySize = arraySizes[arrayName];
            
            if (arrayAccess->children.size() >= 2) {
                auto index = arrayAccess->children[1];
                if (!checkSingleIndex(arrayName, arraySize, index, dimension, dimension)) {
                    return false;
                }
            }
            
            if (arrayAccess->children.size() > 0 && arrayAccess->children[0]) {
                auto base = arrayAccess->children[0];
                if (base->type == ASTNodeType::ARRAY_ACCESS) {
                    return checkArrayAccessRecursive(base, dimension + 1);
                }
            }
        } 
        else if (declaredIdentifiers.find(arrayName) != declaredIdentifiers.end()) {
            warning("Array '" + arrayName + "' has dynamic size - cannot verify bounds at compile time");
            return true;
        }
        else {
            error("Undeclared array '" + arrayName + "'");
            return false;
        }
        
        return true;
    }
    
    bool checkSingleIndex(const std::string& arrayName, int arraySize, 
                         std::shared_ptr<ASTNode> index, int dimension, int totalDimensions) {
        // ... (same as before)
        if (index && index->type == ASTNodeType::LITERAL_INT) {
            int idx = std::stoi(index->value);
            std::cout << "Static index check: " << idx << " in dimension " << dimension << "\n";
            
            if (idx < 1 || idx > arraySize) {
                error("Array index " + std::to_string(idx) + 
                      " out of bounds in dimension " + std::to_string(dimension) +
                      " for array '" + arrayName + "' of size " + std::to_string(arraySize));
                return false;
            } else {
                std::cout << "✓ Dimension " << dimension << " check PASSED\n";
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
                          << " in [" << minIdx << ".." << maxIdx << "] for dimension " << dimension << std::endl;
                
                if (minIdx >= 1 && maxIdx <= arraySize) {
                    std::cout << "🎯 Dimension " << dimension << " RANGE CHECK PASSED!" << std::endl;
                    return true;
                } else {
                    error("Loop variable '" + indexVar + "' range [" + 
                          std::to_string(minIdx) + ".." + std::to_string(maxIdx) + 
                          "] may go out of bounds in dimension " + std::to_string(dimension) +
                          " for array '" + arrayName + "' of size " + std::to_string(arraySize));
                    return false;
                }
            } else {
                std::cout << "Variable index check: " << indexVar << " in dimension " << dimension << std::endl;
                warning("Dynamic index in dimension " + std::to_string(dimension) + 
                       " for array '" + arrayName + "' - cannot verify bounds at compile time");
                return true;
            }
        }
        else {
            warning("Complex index expression in dimension " + std::to_string(dimension) + 
                   " for array '" + arrayName + "' - cannot verify bounds at compile time");
            return true;
        }
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

    // ========== PRECISE USAGE ANALYSIS ==========
    void collectCompleteUsage(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        switch (node->type) {
            case ASTNodeType::ASSIGNMENT:
                if (node->children.size() > 1) {
                    trackReadsInExpression(node->children[1]);
                }
                break;
                
            case ASTNodeType::IF_STMT:
                if (node->children.size() > 0) {
                    trackReadsInExpression(node->children[0]);
                }
                break;
                
            case ASTNodeType::WHILE_LOOP:
                if (node->children.size() > 0) {
                    trackReadsInExpression(node->children[0]);
                }
                break;
                
            case ASTNodeType::FOR_LOOP:
                if (node->children.size() > 0) {
                    trackReadsInExpression(node->children[0]);
                }
                break;
                
            case ASTNodeType::RETURN_STMT:
                if (node->children.size() > 0) {
                    trackReadsInExpression(node->children[0]);
                }
                break;
                
            case ASTNodeType::PRINT_STMT:
                if (node->children.size() > 0) {
                    trackReadsInExpression(node->children[0]);
                }
                break;
                
            case ASTNodeType::ROUTINE_CALL:
                // 🚨 CRITICAL FIX: Properly track routine calls
                if (!node->value.empty()) {
                    trackRoutineCall(node->value);
                    std::cout << "📞 TRACKED ROUTINE CALL: " << node->value << std::endl;
                }
                if (node->children.size() > 0) {
                    trackReadsInExpression(node->children[0]);
                }
                break;
                
            default:
                if (isExpressionContext(node->type)) {
                    trackReadsInExpression(node);
                }
                break;
        }
        
        for (auto& child : node->children) {
            collectCompleteUsage(child);
        }
    }
    
    void trackRoutineCall(const std::string& routineName) {
        calledRoutines.insert(routineName);
    }
    
    bool isExpressionContext(ASTNodeType type) {
        return type == ASTNodeType::BINARY_OP ||
               type == ASTNodeType::UNARY_OP ||
               type == ASTNodeType::SIZE_EXPRESSION ||
               type == ASTNodeType::LITERAL_INT ||
               type == ASTNodeType::LITERAL_REAL ||
               type == ASTNodeType::LITERAL_BOOL ||
               type == ASTNodeType::LITERAL_STRING;
    }
    
    void trackReadsInExpression(std::shared_ptr<ASTNode> expr) {
        if (!expr) return;
        
        if (expr->type == ASTNodeType::IDENTIFIER) {
            readVariables.insert(expr->value);
            std::cout << "📖 TRACKED READ: " << expr->value << std::endl;
        }
        else if (expr->type == ASTNodeType::MEMBER_ACCESS) {
            readVariables.insert(expr->value);
            std::cout << "📖 TRACKED READ (field): " << expr->value << std::endl;
            if (expr->children.size() > 0) {
                trackReadsInExpression(expr->children[0]);
            }
        }
        else if (expr->type == ASTNodeType::ARRAY_ACCESS) {
            if (expr->children.size() > 0 && expr->children[0]) {
                trackReadsInExpression(expr->children[0]);
            }
        }
        
        for (auto& child : expr->children) {
            trackReadsInExpression(child);
        }
    }
    
    bool isRecordType(std::shared_ptr<ASTNode> typeNode) {
        if (!typeNode) return false;
        
        if (typeNode->type == ASTNodeType::RECORD_TYPE) {
            return true;
        }
        
        if (typeNode->type == ASTNodeType::USER_TYPE) {
            std::string typeName = typeNode->value;
            if (typeDefinitions.find(typeName) != typeDefinitions.end()) {
                return isRecordType(typeDefinitions[typeName]);
            }
        }
        
        if (typeNode->type == ASTNodeType::ARRAY_TYPE) {
            if (typeNode->children.size() > 1 && typeNode->children[1]) {
                return isRecordType(typeNode->children[1]);
            }
        }
        
        return false;
    }
    
    bool isRecordFieldDeclaration(std::shared_ptr<ASTNode> varDecl) {
        if (!varDecl || varDecl->children.empty() || !varDecl->children[0]) {
            return false;
        }
        
        auto typeNode = varDecl->children[0];
        
        if (typeNode->type == ASTNodeType::RECORD_TYPE) {
            return true;
        }
        
        if (typeNode->type == ASTNodeType::ARRAY_TYPE) {
            if (typeNode->children.size() > 1 && typeNode->children[1]) {
                auto elementType = typeNode->children[1];
                return isRecordType(elementType);
            }
        }
        
        if (typeNode->type == ASTNodeType::USER_TYPE) {
            std::string typeName = typeNode->value;
            if (typeDefinitions.find(typeName) != typeDefinitions.end()) {
                return isRecordType(typeDefinitions[typeName]);
            }
        }
        
        return false;
    }
    
    // ========== DEBUG USAGE TRACKING ==========
    void debugUsageTracking() {
        std::cout << "\n=== DEBUG USAGE TRACKING ===" << std::endl;
        std::cout << "READ VARIABLES (" << readVariables.size() << "): ";
        for (const auto& var : readVariables) {
            std::cout << var << " ";
        }
        std::cout << std::endl;
        
        std::cout << "WRITTEN VARIABLES (" << writtenVariables.size() << "): ";
        for (const auto& var : writtenVariables) {
            std::cout << var << " ";
        }
        std::cout << std::endl;
        
        std::cout << "DECLARED VARIABLES (" << declaredIdentifiers.size() << "): ";
        for (const auto& var : declaredIdentifiers) {
            std::cout << var << " ";
        }
        std::cout << std::endl;
        
        std::cout << "ROUTINE DECLARATIONS (" << routineDeclarations.size() << "): ";
        for (const auto& routine : routineDeclarations) {
            std::cout << routine << " ";
        }
        std::cout << std::endl;
        
        std::cout << "CALLED ROUTINES (" << calledRoutines.size() << "): ";
        for (const auto& routine : calledRoutines) {
            std::cout << routine << " ";
        }
        std::cout << std::endl;
    }
    
    // ========== ADVANCED OPTIMIZATION ==========
    void optimizeAST(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        if (node->type == ASTNodeType::BODY || node->type == ASTNodeType::PROGRAM) {
            optimizeUnusedDeclarations(node);
        }
        optimizeDeadCode(node);
        for (auto& child : node->children) {
            optimizeAST(child);
        }
    }
    
    void optimizeUnusedDeclarations(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        std::vector<std::shared_ptr<ASTNode>> newChildren;
        int removedCount = 0;
        int preservedRecordFields = 0;
        int preservedRoutines = 0;
        
        std::cout << "=== OPTIMIZING NODE WITH " << node->children.size() << " CHILDREN ===" << std::endl;
        
        for (auto& child : node->children) {
            if (!child) {
                newChildren.push_back(child);
                continue;
            }
            
            if (child->type == ASTNodeType::VAR_DECL) {
                std::string varName = child->value;
                
                bool isRead = (readVariables.find(varName) != readVariables.end());
                bool isWritten = (writtenVariables.find(varName) != writtenVariables.end());
                
                if (isRecordFieldDeclaration(child)) {
                    if (isRead || isWritten) {
                        newChildren.push_back(child);
                        preservedRecordFields++;
                        std::cout << "💾 PRESERVING used record field: " << varName << std::endl;
                    } else {
                        std::cout << "🔥 OPTIMIZATION: Removing UNUSED record field '" << varName << "'" << std::endl;
                        removedCount++;
                    }
                } 
                else {
                    if (isRead || isWritten) {
                        newChildren.push_back(child);
                    } else {
                        std::cout << "🔥 OPTIMIZATION: Removing unused variable '" << varName << "'" << std::endl;
                        removedCount++;
                    }
                }
            } 
            else if (child->type == ASTNodeType::ROUTINE_DECL || 
                     child->type == ASTNodeType::ROUTINE_FORWARD_DECL) {
                std::string routineName = child->value;
                bool isCalled = (calledRoutines.find(routineName) != calledRoutines.end());
                
                // 🚨 CRITICAL FIX: Keep routines that are called OR are main entry point
                if (isCalled || routineName == "main" || routineName == "testRunner") {
                    newChildren.push_back(child);
                    preservedRoutines++;
                    std::cout << "💾 PRESERVING routine: " << routineName 
                              << (isCalled ? " (called)" : " (entry point)") << std::endl;
                } else {
                    std::cout << "🔥 OPTIMIZATION: Removing unused routine '" << routineName << "'" << std::endl;
                    removedCount++;
                }
            } 
            else {
                newChildren.push_back(child);
            }
        }
        
        if (removedCount > 0) {
            node->children = newChildren;
            std::cout << "🔥 Removed " << removedCount << " unused declaration(s)" << std::endl;
            
            if (preservedRecordFields > 0) {
                std::cout << "💾 Preserved " << preservedRecordFields << " used record field(s)" << std::endl;
            }
            if (preservedRoutines > 0) {
                std::cout << "💾 Preserved " << preservedRoutines << " routine(s)" << std::endl;
            }
        }
    }

    void optimizeDeadCode(std::shared_ptr<ASTNode> node) {
        // ... (same as before)
        if (!node) return;
        
        if (node->type == ASTNodeType::BODY) {
            std::vector<std::shared_ptr<ASTNode>> newChildren;
            
            for (auto& child : node->children) {
                bool shouldKeep = true;
                
                if (child && child->type == ASTNodeType::ASSIGNMENT) {
                    if (isDeadAssignment(child)) {
                        std::cout << "🔥 OPTIMIZATION: Removing dead assignment to '" << getAssignmentTarget(child) << "'" << std::endl;
                        shouldKeep = false;
                    }
                }
                else if (child && child->type == ASTNodeType::FOR_LOOP) {
                    optimizeLoopBody(child);
                }
                
                if (shouldKeep) {
                    newChildren.push_back(child);
                }
            }
            
            if (newChildren.size() != node->children.size()) {
                node->children = newChildren;
            }
        }
    }

    void optimizeLoopBody(std::shared_ptr<ASTNode> forLoop) {
        if (!forLoop || forLoop->type != ASTNodeType::FOR_LOOP) return;
        
        if (forLoop->children.size() > 2) {
            auto body = forLoop->children[2];
            if (body && body->type == ASTNodeType::BODY) {
                optimizeDeadCode(body);
            }
        }
    }

    bool isDeadAssignment(std::shared_ptr<ASTNode> assignment) {
        if (!assignment || assignment->type != ASTNodeType::ASSIGNMENT) return false;
        
        std::string target = getAssignmentTarget(assignment);
        if (target.empty()) return false;
        
        return (readVariables.find(target) == readVariables.end());
    }

    std::string getAssignmentTarget(std::shared_ptr<ASTNode> assignment) {
        if (!assignment || assignment->children.empty()) return "";
        
        auto target = assignment->children[0];
        if (!target) return "";
        
        if (target->type == ASTNodeType::IDENTIFIER) {
            return target->value;
        }
        
        return "";
    }

    // ========== OPTIMIZATION REPORT ==========
    void reportOptimizations() {
        std::cout << "\n=== OPTIMIZATION REPORT ===" << std::endl;
        
        std::vector<std::string> unusedVars;
        std::vector<std::string> writeOnlyVars;
        std::vector<std::string> unusedRoutines;
        
        for (const auto& var : declaredIdentifiers) {
            // Skip routines in variable analysis
            if (routineDeclarations.find(var) != routineDeclarations.end()) continue;
            
            bool isRead = (readVariables.find(var) != readVariables.end());
            bool isWritten = (writtenVariables.find(var) != writtenVariables.end());
            
            if (!isRead && !isWritten) {
                unusedVars.push_back(var);
            } else if (isWritten && !isRead) {
                writeOnlyVars.push_back(var);
            }
        }
        
        // Check for uncalled routines
        for (const auto& routine : routineDeclarations) {
            bool isCalled = (calledRoutines.find(routine) != calledRoutines.end());
            bool isEntryPoint = (routine == "main" || routine == "testRunner");
            
            if (!isCalled && !isEntryPoint) {
                unusedRoutines.push_back(routine);
            }
        }
        
        if (!unusedVars.empty()) {
            std::cout << "Unused variables: ";
            for (size_t i = 0; i < unusedVars.size(); ++i) {
                std::cout << unusedVars[i];
                if (i < unusedVars.size() - 1) std::cout << ", ";
            }
            std::cout << std::endl;
        }
        
        if (!writeOnlyVars.empty()) {
            std::cout << "Write-only variables (potential dead stores): ";
            for (size_t i = 0; i < writeOnlyVars.size(); ++i) {
                std::cout << writeOnlyVars[i];
                if (i < writeOnlyVars.size() - 1) std::cout << ", ";
            }
            std::cout << std::endl;
        }
        
        if (!unusedRoutines.empty()) {
            std::cout << "Unused routines: ";
            for (size_t i = 0; i < unusedRoutines.size(); ++i) {
                std::cout << unusedRoutines[i];
                if (i < unusedRoutines.size() - 1) std::cout << ", ";
            }
            std::cout << std::endl;
        }
        
        if (unusedVars.empty() && writeOnlyVars.empty() && unusedRoutines.empty()) {
            std::cout << "✓ All declarations are properly used" << std::endl;
        }
        
        std::cout << "Statistics:" << std::endl;
        std::cout << "  Total declarations: " << declaredIdentifiers.size() << std::endl;
        std::cout << "  Variables read: " << readVariables.size() << std::endl;
        std::cout << "  Variables written: " << writtenVariables.size() << std::endl;
        std::cout << "  Routine declarations: " << routineDeclarations.size() << std::endl;
        std::cout << "  Routines called: " << calledRoutines.size() << std::endl;
        std::cout << "  Known array types: " << arraySizes.size() << std::endl;
    }

    // ========== ERROR HANDLING ==========
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