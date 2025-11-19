#ifndef SEMANTICS_H
#define SEMANTICS_H

#include "ast.h"
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <map>
// TODO: for, while loops and empty if branches proper removal
// TODO: DO NOT CONFUSE RECORD FIELDS DECLARATIONS FROM VARIABLE DECLARATIONS!!!
class SemanticAnalyzer {
private:
    // ========== CORE DATA STRUCTURES ==========
    std::unordered_map<std::string, int> arraySizes;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::set<std::string> globalVariables;
    std::set<std::string> functionsWithSideEffects;
    std::set<std::string> functionsThatReadGlobals;
    std::set<std::string> functionsThatWriteGlobals;
    std::set<std::string> outerScopeVariables;

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

    // ========== NEW: SCOPE TRACKING FOR DECLARATIONS BEFORE USAGE ==========
    struct Scope {
        std::set<std::string> vars;
        std::set<std::string> types;
        std::set<std::string> routines;
    };
    
    std::vector<Scope> scopeStack;
    
    void pushScope() { scopeStack.emplace_back(); }
    void popScope() { if (!scopeStack.empty()) scopeStack.pop_back(); }
    
    bool isVisibleVar(const std::string& n) const {
        for (int i = (int)scopeStack.size() - 1; i >= 0; --i) 
            if (scopeStack[i].vars.count(n)) return true;
        return false;
    }
    
    bool isVisibleType(const std::string& n) const {
        // Primitive types are always visible
        if (n == "integer" || n == "real" || n == "boolean") return true;
        for (int i = (int)scopeStack.size() - 1; i >= 0; --i) 
            if (scopeStack[i].types.count(n)) return true;
        return false;
    }
    
    bool isVisibleRoutine(const std::string& n) const {
        for (int i = (int)scopeStack.size() - 1; i >= 0; --i) 
            if (scopeStack[i].routines.count(n)) return true;
        return false;
    }
    
    void declareVar(const std::string& n) { 
        if (!scopeStack.empty()) scopeStack.back().vars.insert(n); 
    }
    
    void declareType(const std::string& n) { 
        if (!scopeStack.empty()) scopeStack.back().types.insert(n); 
    }
    
    void declareRoutine(const std::string& n) { 
        if (!scopeStack.empty()) scopeStack.back().routines.insert(n); 
    }
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
public:
    bool analyze(std::shared_ptr<ASTNode> ast) {
        std::cout << "=== STARTING SEMANTIC ANALYSIS ===" << std::endl;
        if (!ast) {
            error("AST is null");
            return false;
        }
        
        // PASS 0: Collect type definitions FIRST
        collectTypeDefinitions(ast);
        
        // PASS 0.5: Track global variables
        std::cout << "=== PASS 0.5: GLOBAL VARIABLE TRACKING ===" << std::endl;
        trackGlobalVariables(ast);
        
        // PASS 1: Collect ALL declarations (including parameters)
        collectAllDeclarations(ast);
        
        // NEW PASS: Collect outer scope variables
        std::cout << "=== PASS 1.2: OUTER SCOPE VARIABLE TRACKING ===" << std::endl;
        collectOuterScopeVariables(ast);
        std::cout << "🔍 Found " << outerScopeVariables.size() << " outer scope variables: ";
        for (const auto& var : outerScopeVariables) {
            std::cout << var << " ";
        }
        std::cout << std::endl;
        
        // PASS 1.5: Constant folding optimization
        std::cout << "=== PASS 1.5: CONSTANT FOLDING ===" << std::endl;
        foldConstants(ast);
        
        // PASS 1.7: Check declarations before usage
        std::cout << "=== PASS 1.7: DECLARATIONS BEFORE USAGE ===" << std::endl;
        bool declOk = checkDeclaredBeforeUsage(ast);
        
        // PASS 2: Semantic checks with SMART analysis
        bool semanticSuccess = checkSemantics(ast);
        
        // PASS 3: Collect COMPLETE usage data
        collectCompleteUsage(ast);
        
        // DEBUG: Show what we tracked
        debugUsageTracking();
        
        // PASS 4: Run ADVANCED optimizations (now with global AND outer scope awareness)
        optimizeAST(ast);
        
        // PASS 5: Report optimization results
        reportOptimizations();
        
        return declOk && semanticSuccess && errors.empty();
    }

private:
    // ========== NEW: CONSTANT FOLDING ==========
    bool isIntLit(std::shared_ptr<ASTNode> n) { return n && n->type == ASTNodeType::LITERAL_INT; }
    bool isRealLit(std::shared_ptr<ASTNode> n) { return n && n->type == ASTNodeType::LITERAL_REAL; }
    bool isBoolLit(std::shared_ptr<ASTNode> n) { return n && n->type == ASTNodeType::LITERAL_BOOL; }
    void trackGlobalVariables(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        // Only track VAR_DECL that are direct children of PROGRAM
        if (node->type == ASTNodeType::PROGRAM) {
            for (auto& child : node->children) {
                if (child && child->type == ASTNodeType::VAR_DECL) {
                    globalVariables.insert(child->value);
                    std::cout << "🌍 TRACKED GLOBAL: " << child->value << std::endl;
                }
            }
        }
        
        // Don't recursively track inside routines - those are LOCAL!
        if (node->type != ASTNodeType::ROUTINE_DECL && 
            node->type != ASTNodeType::ROUTINE_FORWARD_DECL) {
            for (auto& child : node->children) {
                trackGlobalVariables(child);
            }
        }
    }

    // Check if a variable access involves global variables
    bool involvesGlobalVariable(std::shared_ptr<ASTNode> node) {
        if (!node) return false;
        
        if (node->type == ASTNodeType::IDENTIFIER) {
            return globalVariables.find(node->value) != globalVariables.end();
        }
        else if (node->type == ASTNodeType::MEMBER_ACCESS) {
            // For member access like corp.ceo.id, check the base
            if (node->children.size() > 0 && node->children[0]) {
                return involvesGlobalVariable(node->children[0]);
            }
            return globalVariables.find(node->value) != globalVariables.end();
        }
        else if (node->type == ASTNodeType::ARRAY_ACCESS) {
            // For array access like people[i], check the array name
            if (node->children.size() > 0 && node->children[0]) {
                return involvesGlobalVariable(node->children[0]);
            }
        }
        
        return false;
    }
    void foldConstantsInPlace(std::shared_ptr<ASTNode>& node) {
        if (!node) return;
        
        // First, fold all children
        for (auto& ch : node->children) foldConstantsInPlace(ch);
        
        // Handle unary operations
        if (node->type == ASTNodeType::UNARY_OP && !node->children.empty()) {
            auto op = node->value;
            auto arg = node->children[0];
            
            if (op == "not" && isBoolLit(arg)) {
                bool v = (arg->value == "true");
                node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_BOOL, v ? "false" : "true");
                std::cout << "  Folded: not " << arg->value << " -> " << node->value << std::endl;
                return;
            }
            
            if ((op == "+" || op == "-") && isIntLit(arg)) {
                long long v = std::stoll(arg->value);
                if (op == "-") v = -v;
                node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_INT, std::to_string(v));
                std::cout << "  Folded: " << op << arg->value << " -> " << node->value << std::endl;
                return;
            }
            
            if ((op == "+" || op == "-") && isRealLit(arg)) {
                double v = std::stod(arg->value);
                if (op == "-") v = -v;
                node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_REAL, std::to_string(v));
                std::cout << "  Folded: " << op << arg->value << " -> " << node->value << std::endl;
                return;
            }
        }
        
        // Handle binary operations
        if (node->type == ASTNodeType::BINARY_OP && node->children.size() >= 2) {
            auto op = node->value;
            auto L = node->children[0];
            auto R = node->children[1];
            
            try {
                // Boolean operations
                if ((op == "and" || op == "or" || op == "xor") && isBoolLit(L) && isBoolLit(R)) {
                    bool a = (L->value == "true"), b = (R->value == "true");
                    bool res = (op == "and" ? (a && b) : (op == "or" ? (a || b) : (a != b)));
                    node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_BOOL, res ? "true" : "false");
                    std::cout << "  Folded: " << L->value << " " << op << " " << R->value << " -> " << node->value << std::endl;
                    return;
                }
                
                // Comparison operations
                if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "=" || op == "/=") {
                    // int-int comparison
                    if (isIntLit(L) && isIntLit(R)) {
                        long long a = std::stoll(L->value), b = std::stoll(R->value);
                        bool res = (op == "<" ? a < b :
                                    op == "<=" ? a <= b :
                                    op == ">" ? a > b :
                                    op == ">=" ? a >= b :
                                    op == "=" ? a == b : a != b);
                        node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_BOOL, res ? "true" : "false");
                        std::cout << "  Folded: " << a << " " << op << " " << b << " -> " << node->value << std::endl;
                        return;
                    }
                    
                    // real comparison (or mixed int/real)
                    if ((isRealLit(L) || isIntLit(L)) && (isRealLit(R) || isIntLit(R))) {
                        if (isRealLit(L) || isRealLit(R)) {
                            double a = std::stod(L->value);
                            double b = std::stod(R->value);
                            bool res = (op == "<" ? a < b :
                                        op == "<=" ? a <= b :
                                        op == ">" ? a > b :
                                        op == ">=" ? a >= b :
                                        op == "=" ? a == b : a != b);
                            node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_BOOL, res ? "true" : "false");
                            std::cout << "  Folded: " << a << " " << op << " " << b << " -> " << node->value << std::endl;
                            return;
                        }
                    }
                    
                    // bool comparison
                    if (isBoolLit(L) && isBoolLit(R) && (op == "=" || op == "/=")) {
                        bool a = (L->value == "true"), b = (R->value == "true");
                        bool res = (op == "=" ? a == b : a != b);
                        node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_BOOL, res ? "true" : "false");
                        std::cout << "  Folded: " << L->value << " " << op << " " << R->value << " -> " << node->value << std::endl;
                        return;
                    }
                }
                
                // Arithmetic over integers
                if (isIntLit(L) && isIntLit(R)) {
                    long long a = std::stoll(L->value), b = std::stoll(R->value);
                    if (op == "+") {
                        node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_INT, std::to_string(a + b));
                        std::cout << "  Folded: " << a << " + " << b << " -> " << node->value << std::endl;
                        return;
                    }
                    if (op == "-") {
                        node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_INT, std::to_string(a - b));
                        std::cout << "  Folded: " << a << " - " << b << " -> " << node->value << std::endl;
                        return;
                    }
                    if (op == "*") {
                        node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_INT, std::to_string(a * b));
                        std::cout << "  Folded: " << a << " * " << b << " -> " << node->value << std::endl;
                        return;
                    }
                    if (op == "%" && b != 0) {
                        node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_INT, std::to_string(a % b));
                        std::cout << "  Folded: " << a << " % " << b << " -> " << node->value << std::endl;
                        return;
                    }
                    // Division not folded to preserve type semantics
                }
                
                // Arithmetic with reals (or mixed)
                if ((isRealLit(L) || isIntLit(L)) && (isRealLit(R) || isIntLit(R))) {
                    if (isRealLit(L) || isRealLit(R)) {
                        double a = std::stod(L->value);
                        double b = std::stod(R->value);
                        if (op == "+") {
                            node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_REAL, std::to_string(a + b));
                            std::cout << "  Folded: " << a << " + " << b << " -> " << node->value << std::endl;
                            return;
                        }
                        if (op == "-") {
                            node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_REAL, std::to_string(a - b));
                            std::cout << "  Folded: " << a << " - " << b << " -> " << node->value << std::endl;
                            return;
                        }
                        if (op == "*") {
                            node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_REAL, std::to_string(a * b));
                            std::cout << "  Folded: " << a << " * " << b << " -> " << node->value << std::endl;
                            return;
                        }
                        if (op == "/" && b != 0.0) {
                            node = std::make_shared<ASTNode>(ASTNodeType::LITERAL_REAL, std::to_string(a / b));
                            std::cout << "  Folded: " << a << " / " << b << " -> " << node->value << std::endl;
                            return;
                        }
                    }
                }
            } catch (...) {
                // Ignore errors during folding
            }
        }
    }
    
    void foldConstants(std::shared_ptr<ASTNode>& root) {
        foldConstantsInPlace(root);
    }

    // ========== NEW: DECLARATIONS BEFORE USAGE CHECK ==========
    bool checkIdentifierUse(std::shared_ptr<ASTNode> n) {
        if (!n) return true;
        if (n->type == ASTNodeType::IDENTIFIER) {
            if (!isVisibleVar(n->value)) {
                error("Use of undeclared variable '" + n->value + "'");
                return false;
            }
        }
        return true;
    }
    
    bool checkUserTypeUse(std::shared_ptr<ASTNode> n) {
        if (!n) return true;
        if (n->type == ASTNodeType::USER_TYPE) {
            if (!isVisibleType(n->value)) {
                error("Use of undeclared type '" + n->value + "'");
                return false;
            }
        }
        return true;
    }
    
    bool checkDeclaredBeforeUsageRec(std::shared_ptr<ASTNode> node) {
        if (!node) return true;
        bool ok = true;
        
        switch (node->type) {
            case ASTNodeType::PROGRAM: {
                pushScope();
                for (auto& ch : node->children) {
                    ok = ok && checkDeclaredBeforeUsageRec(ch);
                }
                popScope();
                return ok;
            }
            
            case ASTNodeType::TYPE_DECL: {
                // Check type definition first, then declare the type name
                if (!node->children.empty() && node->children[0]) {
                    ok = ok && checkDeclaredBeforeUsageRec(node->children[0]);
                    ok = ok && checkUserTypeUse(node->children[0]);
                }
                declareType(node->value);
                return ok;
            }
            
            case ASTNodeType::VAR_DECL: {
                // Declare variable, then check initialization
                declareVar(node->value);
                for (auto& ch : node->children) {
                    ok = ok && checkDeclaredBeforeUsageRec(ch);
                    ok = ok && checkIdentifierUse(ch);
                    ok = ok && checkUserTypeUse(ch);
                }
                return ok;
            }
            
            case ASTNodeType::ROUTINE_FORWARD_DECL: {
                // Forward declares routine immediately
                declareRoutine(node->value);
                if (!node->children.empty() && node->children[0]) {
                    pushScope();
                    for (auto& p : node->children[0]->children) {
                        if (p && p->type == ASTNodeType::PARAMETER) {
                            declareVar(p->value);
                            for (auto& t : p->children) {
                                ok = ok && checkDeclaredBeforeUsageRec(t);
                                ok = ok && checkUserTypeUse(t);
                            }
                        }
                    }
                    popScope();
                }
                return ok;
            }
            
            case ASTNodeType::ROUTINE_DECL: {
                // Declare routine name first
                declareRoutine(node->value);
                size_t i = 0;
                pushScope();
                // Parameters
                if (i < node->children.size() && node->children[i] && 
                    node->children[i]->type == ASTNodeType::PARAMETER_LIST) {
                    for (auto& p : node->children[i]->children) {
                        if (p && p->type == ASTNodeType::PARAMETER) {
                            declareVar(p->value);
                            for (auto& t : p->children) {
                                ok = ok && checkDeclaredBeforeUsageRec(t);
                                ok = ok && checkUserTypeUse(t);
                            }
                        }
                    }
                    ++i;
                }
                // Return type
                if (i < node->children.size() && node->children[i] && 
                    node->children[i]->type == ASTNodeType::USER_TYPE) {
                    ok = ok && checkUserTypeUse(node->children[i]);
                    ++i;
                }
                // Body
                for (; i < node->children.size(); ++i) {
                    ok = ok && checkDeclaredBeforeUsageRec(node->children[i]);
                }
                popScope();
                return ok;
            }
            
            case ASTNodeType::ROUTINE_CALL: {
                if (!isVisibleRoutine(node->value)) {
                    error("Call of undeclared routine '" + node->value + "'");
                    ok = false;
                }
                for (auto& ch : node->children) {
                    ok = ok && checkDeclaredBeforeUsageRec(ch);
                    ok = ok && checkIdentifierUse(ch);
                    ok = ok && checkUserTypeUse(ch);
                }
                return ok;
            }
            
            case ASTNodeType::BODY: {
                pushScope();
                for (auto& ch : node->children) {
                    ok = ok && checkDeclaredBeforeUsageRec(ch);
                }
                popScope();
                return ok;
            }
            
            case ASTNodeType::FOR_LOOP: {
                // For loop variable is declared in the loop scope
                pushScope();
                declareVar(node->value);
            
                // IMPORTANT: skip the special IDENTIFIER("reverse") marker
                for (auto& ch : node->children) {
                    if (ch && ch->type == ASTNodeType::IDENTIFIER && ch->value == "reverse") {
                        continue; // do not treat as a variable use
                    }
                    ok = ok && checkDeclaredBeforeUsageRec(ch);
                    ok = ok && checkIdentifierUse(ch);
                    ok = ok && checkUserTypeUse(ch);
                }
            
                popScope();
                return ok;
            }
            
            
            default: {
                for (auto& ch : node->children) {
                    ok = ok && checkDeclaredBeforeUsageRec(ch);
                    ok = ok && checkIdentifierUse(ch);
                    ok = ok && checkUserTypeUse(ch);
                }
                ok = ok && checkIdentifierUse(node);
                ok = ok && checkUserTypeUse(node);
                return ok;
            }
        }
    }
    
    bool checkDeclaredBeforeUsage(std::shared_ptr<ASTNode> root) {
        scopeStack.clear();
        return checkDeclaredBeforeUsageRec(root);
    }

    // ========== EXISTING CODE CONTINUES BELOW ==========
    void collectOuterScopeVariables(std::shared_ptr<ASTNode> node, int currentDepth = 0, 
                               const std::set<std::string>& currentScopeVars = {}) {
        if (!node) return;
        
        std::set<std::string> newCurrentScopeVars = currentScopeVars;
        
        if (node->type == ASTNodeType::ROUTINE_DECL || 
            node->type == ASTNodeType::ROUTINE_FORWARD_DECL ||
            node->type == ASTNodeType::FOR_LOOP) {
            
            // When entering a new scope, track which variables are declared in THIS scope
            if (node->type == ASTNodeType::ROUTINE_DECL) {
                // Parameters are in current scope
                if (node->children.size() > 0 && node->children[0] && 
                    node->children[0]->type == ASTNodeType::PARAMETER_LIST) {
                    for (auto& param : node->children[0]->children) {
                        if (param && param->type == ASTNodeType::PARAMETER) {
                            newCurrentScopeVars.insert(param->value);
                        }
                    }
                }
            }
            else if (node->type == ASTNodeType::FOR_LOOP) {
                newCurrentScopeVars.insert(node->value); // loop variable
            }
            
            // Recursively process children with the new scope
            for (auto& child : node->children) {
                collectOuterScopeVariables(child, currentDepth + 1, newCurrentScopeVars);
            }
            return;
        }
        
        if (node->type == ASTNodeType::VAR_DECL && currentDepth > 0) {
            // Add this variable to current scope
            newCurrentScopeVars.insert(node->value);
        }
        
        if (node->type == ASTNodeType::IDENTIFIER && currentDepth > 0) {
            // If we find an identifier that's NOT in current scope but IS declared, it's from outer scope
            if (currentScopeVars.find(node->value) == currentScopeVars.end() &&
                declaredIdentifiers.find(node->value) != declaredIdentifiers.end()) {
                outerScopeVariables.insert(node->value);
                std::cout << "🔍 FOUND OUTER SCOPE VARIABLE: " << node->value << std::endl;
            }
        }
        
        for (auto& child : node->children) {
            collectOuterScopeVariables(child, currentDepth, newCurrentScopeVars);
        }
    }

    // ========== DETECT WRITES TO OUTER SCOPE VARIABLES ==========
    bool writesToOuterScope(std::shared_ptr<ASTNode> assignment) {
        if (!assignment || assignment->type != ASTNodeType::ASSIGNMENT) return false;
        
        std::string target = getAssignmentTarget(assignment);
        if (target.empty()) return false;
        
        // Check if this variable exists in outer scope
        return outerScopeVariables.find(target) != outerScopeVariables.end();
    }

    bool accessesOuterScope(std::shared_ptr<ASTNode> node) {
        if (!node) return false;
        
        if (node->type == ASTNodeType::IDENTIFIER) {
            return outerScopeVariables.find(node->value) != outerScopeVariables.end();
        }
        else if (node->type == ASTNodeType::MEMBER_ACCESS) {
            if (node->children.size() > 0 && node->children[0]) {
                return accessesOuterScope(node->children[0]);
            }
            return outerScopeVariables.find(node->value) != outerScopeVariables.end();
        }
        else if (node->type == ASTNodeType::ARRAY_ACCESS) {
            if (node->children.size() > 0 && node->children[0]) {
                return accessesOuterScope(node->children[0]);
            }
        }
        
        // Check children
        for (auto& child : node->children) {
            if (accessesOuterScope(child)) {
                return true;
            }
        }
        
        return false;
    }

    // ========== SIDE EFFECT DETECTION ==========
    bool hasSideEffects(std::shared_ptr<ASTNode> node) {
        if (!node) return false;
        
        // FUNCTION CALLS ALWAYS HAVE POTENTIAL SIDE EFFECTS!
        if (node->type == ASTNodeType::ROUTINE_CALL) {
            return true;
        }
        
        if (node->type == ASTNodeType::ASSIGNMENT && writesToOuterScope(node)) {
            return true;
        }
        
        // Check children recursively
        for (auto& child : node->children) {
            if (hasSideEffects(child)) {
                return true;
            }
        }
        
        return false;
    }

    // Check if expression has side effects (function calls, global access, outer scope access)
    bool hasSideEffectsOrExternalAccess(std::shared_ptr<ASTNode> node) {
        if (!node) return false;
        
        return hasSideEffects(node) || 
            involvesGlobalVariable(node) || 
            accessesOuterScope(node);
    }

    //isDeadAssignment with complete side effect awareness
    bool isDeadAssignment(std::shared_ptr<ASTNode> assignment) {
        if (!assignment || assignment->type != ASTNodeType::ASSIGNMENT) return false;
        
        std::string target = getAssignmentTarget(assignment);
        if (target.empty()) return false;
        
        // NEVER remove assignments to global variables
        if (globalVariables.find(target) != globalVariables.end()) {
            return false;
        }
        
        // NEVER remove assignments to outer scope variables
        if (false) {
            std::cout << "💾 PRESERVING assignment to outer scope variable '" << target << "'" << std::endl;
            return false;
        }
        
        // Check if the RHS has side effects - if so, PRESERVE!
        if (assignment->children.size() > 1 && assignment->children[1]) {
            if (hasSideEffectsOrExternalAccess(assignment->children[1])) {
                std::cout << "💾 PRESERVING assignment with side effects to '" << target << "'" << std::endl;
                return false;
            }
        }
        
        // Only consider it dead if it's a local variable that's never read AND has no side effects
        return (readVariables.find(target) == readVariables.end());
    }

    bool isVariableUsedOnlyInDeadCode(const std::string& varName, std::shared_ptr<ASTNode> /* node */) {
        return (declaredIdentifiers.find(varName) != declaredIdentifiers.end()) &&
               (readVariables.find(varName) == readVariables.end()) &&
               (writtenVariables.find(varName) == writtenVariables.end());
    }

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
                routineDeclarations.insert(node->value);
                std::cout << "Found routine: " << node->value << std::endl;
                break;
            default:
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
                return -1;
            }
        }
        else if (typeNode->type == ASTNodeType::USER_TYPE) {
            std::string typeName = typeNode->value;
            if (typeDefinitions.find(typeName) != typeDefinitions.end()) {
                return getArraySizeFromType(typeDefinitions[typeName]);
            }
        }
        return -1;
    }

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
        
        std::cout << "  📝 WRITE TRACKING: " << astNodeTypeToString(leftSide->type);
        if (!leftSide->value.empty()) std::cout << " (" << leftSide->value << ")";
        std::cout << std::endl;
        
        if (leftSide->type == ASTNodeType::IDENTIFIER) {
            writtenVariables.insert(leftSide->value);
            std::cout << "  ✅📝 TRACKED WRITE: " << leftSide->value << std::endl;
        }
        else if (leftSide->type == ASTNodeType::MEMBER_ACCESS) {
            writtenVariables.insert(leftSide->value);
            std::cout << "  ✅📝 TRACKED WRITE (field): " << leftSide->value << std::endl;
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
    bool oneTime = false;
    bool checkMultiDimArrayBounds(std::shared_ptr<ASTNode> arrayAccess) {
        if (!arrayAccess || arrayAccess->type != ASTNodeType::ARRAY_ACCESS) {
            return true;
        }
        if (!oneTime) {
            std::cout << "=== CHECKING MULTI-DIMENSIONAL ARRAY BOUNDS ===" << std::endl;
            oneTime = true;
        }
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
        
        if (declaredIdentifiers.find(arrayName) != declaredIdentifiers.end()) {
            if (arraySizes.find(arrayName) != arraySizes.end()) {
                int arraySize = arraySizes[arrayName];
                if (arrayAccess->children.size() >= 2) {
                    auto index = arrayAccess->children[1];
                    if (!checkSingleIndex(arrayName, arraySize, index, dimension)) {
                        return false;
                    }
                }
            } else {
                // Dynamic array - can't verify bounds at compile time
                warning("Array '" + arrayName + "' has dynamic size - cannot verify bounds at compile time");
                
                // BUT we can still check for obviously wrong static indices!
                if (arrayAccess->children.size() >= 2) {
                    auto index = arrayAccess->children[1];
                    if (index && index->type == ASTNodeType::LITERAL_INT) {
                        int idx = std::stoi(index->value);
                        if (idx < 1) {  // Negative indices are always wrong
                            error("Array index " + std::to_string(idx) +
                                " is negative or zero - array indices must start from 1");
                            return false;
                        }
                    }
                }
            }
            
            // Continue checking nested array accesses
            // if (arrayAccess->children.size() > 0 && arrayAccess->children[0]) {
            //     auto base = arrayAccess->children[0];
            //     if (base->type == ASTNodeType::ARRAY_ACCESS) {
            //         return checkArrayAccessRecursive(base, dimension + 1);
            //     }
            // }
        }
        else {
            error("Undeclared array '" + arrayName + "'");
            return false;
        }
        return true;
    }
    bool checkSingleIndex(const std::string& arrayName, int arraySize,
                         std::shared_ptr<ASTNode> index, int dimension) {
        if (index && index->type == ASTNodeType::LITERAL_INT) {
            int idx = std::stoi(index->value);
            if (idx < 1 || idx > arraySize) {
                return false;
            } else {
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
            } else {
                // Track dynamic arrays too, so we know they exist
                // This prevents "Undeclared array" errors for parameter arrays
                std::cout << "Found dynamic array declaration: " << varName << std::endl;
            }
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

    void optimizeAST(std::shared_ptr<ASTNode> node) {
        if (!node) return;
         if (node->type == ASTNodeType::TYPE_DECL || 
            node->type == ASTNodeType::RECORD_TYPE ||
            node->type == ASTNodeType::ARRAY_TYPE ||
            node->type == ASTNodeType::PRIMITIVE_TYPE ||
            node->type == ASTNodeType::USER_TYPE) {
                std::cout << "💾 PRESERVING type node: " << astNodeTypeToString(node->type) << std::endl;
                return;
        }
        // Optimize ALL bodies, not just PROGRAM and top-level BODY
        if (node->type == ASTNodeType::BODY || 
            node->type == ASTNodeType::PROGRAM ||
            node->type == ASTNodeType::ROUTINE_DECL) {
            std::cout << "🎯 OPTIMIZING: " << astNodeTypeToString(node->type) << std::endl;
            optimizeUnusedDeclarations(node);
        }
        
        optimizeDeadCode(node);
        
        for (auto& child : node->children) {
            optimizeAST(child);
        }
    }
    void removeAssignmentsToVariable(const std::string& varName, std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        if (node->type == ASTNodeType::BODY || node->type == ASTNodeType::PROGRAM) {
            std::vector<std::shared_ptr<ASTNode>> newChildren;
            for (auto& child : node->children) {
                bool shouldKeep = true;
                
                if (child && child->type == ASTNodeType::ASSIGNMENT) {
                    std::string target = getAssignmentTarget(child);
                    if (target == varName) {
                        std::cout << "    🔥 Removing assignment to: " << varName << std::endl;
                        shouldKeep = false;
                    }
                }
                
                if (shouldKeep) {
                    newChildren.push_back(child);
                    // Recursively process this child to remove nested assignments
                    removeAssignmentsToVariable(varName, child);
                }
            }
            
            if (newChildren.size() != node->children.size()) {
                node->children = newChildren;
                std::cout << "    🔥 Removed " << (node->children.size() - newChildren.size()) 
                        << " assignment(s) to '" << varName << "'" << std::endl;
            }
        } else {
            // For non-body nodes, recursively process all children
            for (auto& child : node->children) {
                removeAssignmentsToVariable(varName, child);
            }
        }
    }

    void optimizeUnusedDeclarations(std::shared_ptr<ASTNode> node) {
        if (!node) return;

        
        std::vector<std::shared_ptr<ASTNode>> newChildren;
        int removedCount = 0;
        int preservedRecordFields = 0;
        int preservedRoutines = 0;
        int preservedGlobalWrites = 0;
        int preservedSideEffects = 0;
        int removedUnusedGlobals = 0;
        
        // COLLECT all write-only variables first
        std::set<std::string> writeOnlyVarsToRemove;
        
        std::cout << "=== OPTIMIZING NODE WITH " << node->children.size() << " CHILDREN ===" << std::endl;
        
        // FIRST PASS: Identify which variables to remove
        for (auto& child : node->children) {
            if (!child) continue;
            
            if (child->type == ASTNodeType::VAR_DECL) {
                std::string varName = child->value;
                bool isRead = (readVariables.find(varName) != readVariables.end());
                bool isWritten = (writtenVariables.find(varName) != writtenVariables.end());
                bool isGlobal = (globalVariables.find(varName) != globalVariables.end());
                
                std::cout << "🔍 VAR ANALYSIS: " << varName 
                        << " | Read: " << isRead 
                        << " | Written: " << isWritten
                        << " | Global: " << isGlobal << std::endl;
                
                if (!isRecordFieldDeclaration(child)) {
                    if (!isRead && isWritten && !isGlobal) {
                        // Mark this write-only local variable for removal
                        writeOnlyVarsToRemove.insert(varName);
                        std::cout << "🎯 MARKED for removal: " << varName << std::endl;
                    }
                }
            }
        }
        
        // SECOND PASS: Actually build the new children list
        for (auto& child : node->children) {
            if (!child) {
                newChildren.push_back(child);
                continue;
            }
            
            if (child->type == ASTNodeType::VAR_DECL) {
                std::string varName = child->value;
                bool isRead = (readVariables.find(varName) != readVariables.end());
                bool isWritten = (writtenVariables.find(varName) != writtenVariables.end());
                bool isGlobal = (globalVariables.find(varName) != globalVariables.end());
                
                if (isRecordFieldDeclaration(child)) {
                    if (isRead || isWritten) {
                        newChildren.push_back(child);
                        preservedRecordFields++;
                        std::cout << "💾 PRESERVING used record field: " << varName << std::endl;
                    } else {
                        std::cout << "🔥 OPTIMIZATION: Removing UNUSED record field '" << varName << "'" << std::endl;
                        removedCount++;
                        // DON'T add to newChildren - this removes the declaration
                    }
                }
                else {
                    if (isRead) {
                        newChildren.push_back(child);
                        std::cout << "💾 PRESERVING read variable: " << varName << std::endl;
                    } else if (isGlobal) {
                        if (isRead || isWritten) {
                            newChildren.push_back(child);
                            std::cout << "💾 PRESERVING USED GLOBAL variable: " << varName << std::endl;
                        } else {
                            std::cout << "🔥 OPTIMIZATION: Removing UNUSED GLOBAL variable '" << varName << "'" << std::endl;
                            removedCount++;
                            removedUnusedGlobals++;
                        }
                    } else if (writeOnlyVarsToRemove.find(varName) != writeOnlyVarsToRemove.end()) {
                        // WRITE-ONLY LOCAL VARIABLE - REMOVE DECLARATION
                        std::cout << "🔥 OPTIMIZATION: Removing write-only LOCAL variable '" << varName << "'" << std::endl;
                        removedCount++;
                        removeAssignmentsToVariable(varName, node);
                        // DON'T add to newChildren - this removes the declaration
                    } else {
                        // COMPLETELY UNUSED VARIABLE
                        std::cout << "🔥 OPTIMIZATION: Removing unused variable '" << varName << "'" << std::endl;
                        removedCount++;
                    }
                }
            }
            else if (child->type == ASTNodeType::ASSIGNMENT) {
                std::string target = getAssignmentTarget(child);
                
                // Check if this assignment should be removed
                if (!target.empty() && writeOnlyVarsToRemove.find(target) != writeOnlyVarsToRemove.end()) {
                    // Check if RHS has side effects
                    bool rhsHasSideEffects = false;
                    if (child->children.size() > 1 && child->children[1]) {
                        rhsHasSideEffects = hasSideEffectsOrExternalAccess(child->children[1]);
                    }
                    
                    if (rhsHasSideEffects) {
                        // PRESERVE the function call but remove the assignment
                        // Convert: unusedResult := sideEffectFunction()
                        // To:      sideEffectFunction()  (standalone call)
                        std::cout << "💾 PRESERVING side effects in assignment to '" << target << "'" << std::endl;
                        
                        // Extract the RHS and add it as a standalone expression
                        auto rhs = child->children[1];
                        newChildren.push_back(rhs);
                        preservedSideEffects++;
                    } else {
                        // Remove the entire assignment (no side effects)
                        std::cout << "🔥 OPTIMIZATION: Removing assignment to write-only variable '" << target << "'" << std::endl;
                        removeAssignmentsToVariable(target, node);
                        removedCount++;
                    }
                }
                else {
                    // Check if this assignment should be preserved due to side effects
                    bool hasExternalEffects = false;
                    if (child->children.size() > 0) {
                        if (involvesGlobalVariable(child->children[0])) {
                            hasExternalEffects = true;
                        }
                        if (child->children.size() > 1 && hasSideEffectsOrExternalAccess(child->children[1])) {
                            hasExternalEffects = true;
                        }
                    }
                    
                    if (hasExternalEffects) {
                        if (!target.empty()) {
                            std::cout << "💾 PRESERVING assignment with external effects: " << target << std::endl;
                            preservedSideEffects++;
                        }
                        newChildren.push_back(child);
                    } else {
                        if (!isDeadAssignment(child)) {
                            newChildren.push_back(child);
                        } else {
                            std::cout << "🔥 OPTIMIZATION: Removing dead assignment to '"
                                    << target << "'" << std::endl;
                            removedCount++;
                            removeAssignmentsToVariable(target, node);
                            
                        }
                    }
                }
            }
            else if (child->type == ASTNodeType::ROUTINE_DECL ||
                    child->type == ASTNodeType::ROUTINE_FORWARD_DECL) {
                std::string routineName = child->value;
                bool isCalled = (calledRoutines.find(routineName) != calledRoutines.end());
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
            // Preserve standalone expressions with side effects (like function calls without assignment)
            else if (hasSideEffectsOrExternalAccess(child)) {
                newChildren.push_back(child);
                preservedSideEffects++;
                std::cout << "💾 PRESERVING expression with side effects/external access" << std::endl;
            }
            else {
                newChildren.push_back(child);
            }
        }
        
        if (removedCount > 0) {
            node->children = newChildren;
            std::cout << "🔥 Removed " << removedCount << " unused declaration(s)" << std::endl;
            if (removedUnusedGlobals > 0) {
                std::cout << "🔥 Removed " << removedUnusedGlobals << " UNUSED global variable(s)" << std::endl;
            }
            if (preservedRecordFields > 0) {
                std::cout << "💾 Preserved " << preservedRecordFields << " used record field(s)" << std::endl;
            }
            if (preservedRoutines > 0) {
                std::cout << "💾 Preserved " << preservedRoutines << " routine(s)" << std::endl;
            }
            if (preservedGlobalWrites > 0) {
                std::cout << "💾 Preserved " << preservedGlobalWrites << " global variable assignment(s)" << std::endl;
            }
            if (preservedSideEffects > 0) {
                std::cout << "💾 Preserved " << preservedSideEffects << " expression(s) with side effects" << std::endl;
            }
        }
    }
    
    void collectCompleteUsage(std::shared_ptr<ASTNode> node) {
        if (!node) return;
        
        // DEBUG: Track which node we're processing
        std::cout << "🔍 USAGE TRACKING: Processing " << astNodeTypeToString(node->type);
        if (!node->value.empty()) std::cout << " (" << node->value << ")";
        std::cout << std::endl;
        
        switch (node->type) {
            case ASTNodeType::RETURN_STMT:
                std::cout << "  📊 RETURN_STMT with " << node->children.size() << " children" << std::endl;
                if (!node->children.empty()) {
                    std::cout << "  📖 Processing return expression" << std::endl;
                    trackReadsInExpression(node->children[0]);
                    collectCompleteUsage(node->children[0]); // For routine calls
                }
                return;
            case ASTNodeType::PRINT_STMT:
                if (node->children.size() > 0) {
                    // Track ALL identifiers in print statements as READS!
                    trackReadsInExpression(node->children[0]);
                }
                break;
            case ASTNodeType::IF_STMT:
                std::cout << "  📊 IF_STMT with " << node->children.size() << " children" << std::endl;
                if (node->children.size() > 0) {
                    trackReadsInExpression(node->children[0]); // condition
                }
                // Track both branches - they might both be reachable
                if (node->children.size() > 1) {
                    std::cout << "  📊 Processing THEN branch" << std::endl;
                    collectCompleteUsage(node->children[1]); // then branch
                }
                if (node->children.size() > 2) {
                    std::cout << "  📊 Processing ELSE branch" << std::endl;
                    collectCompleteUsage(node->children[2]); // else branch  
                }
                return;

            case ASTNodeType::ASSIGNMENT:
                std::cout << "  📊 ASSIGNMENT" << std::endl;
                if (node->children.size() > 0) {
                    std::string target = getAssignmentTarget(node);
                    std::cout << "  📝 Assignment target: " << target << std::endl;
                }
                if (node->children.size() > 1) {
                    std::cout << "  📖 Tracking RHS expression" << std::endl;
                    trackReadsInExpression(node->children[1]);
                }
                break;
                
            case ASTNodeType::FOR_LOOP:
                std::cout << "  📊 FOR_LOOP: " << node->value << std::endl;
                if (node->children.size() > 0) {
                    trackReadsInExpression(node->children[0]);
                }
                break;
                
            case ASTNodeType::ROUTINE_CALL:
                std::cout << "  📊 ROUTINE_CALL: " << node->value << std::endl;
                if (!node->value.empty()) {
                    trackRoutineCall(node->value);
                }
                if (node->children.size() > 0) {
                    trackReadsInExpression(node->children[0]);
                }
                break;
                
            default:
                if (isExpressionContext(node->type)) {
                    std::cout << "  📊 Expression context" << std::endl;
                    trackReadsInExpression(node);
                }
                break;
        }
        
        // Only process children for non-IF_STMT nodes
        if (node->type != ASTNodeType::IF_STMT) {
            for (auto& child : node->children) {
                collectCompleteUsage(child);
            }
        }
    }

    void trackReadsInExpression(std::shared_ptr<ASTNode> expr) {
        if (!expr) return;
        
        std::cout << "  📖 EXPRESSION: " << astNodeTypeToString(expr->type);
        if (!expr->value.empty()) std::cout << " (" << expr->value << ")";
        std::cout << std::endl;
        
        if (expr->type == ASTNodeType::IDENTIFIER) {
            readVariables.insert(expr->value);
            std::cout << "  ✅📖 TRACKED READ: " << expr->value << std::endl;
        }
        else if (expr->type == ASTNodeType::MEMBER_ACCESS) {
            readVariables.insert(expr->value);
            std::cout << "  ✅📖 TRACKED READ (field): " << expr->value << std::endl;
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
    bool isBodyEmpty(std::shared_ptr<ASTNode> body) {
        if (!body || body->type != ASTNodeType::BODY) return true;
        for (auto& child : body->children) {
            if (child && !isMeaninglessNode(child)) {
                return false;
            }
        }
        return true;
    }

    bool isMeaninglessNode(std::shared_ptr<ASTNode> node) {
        if (!node) return true;
        if (node->type == ASTNodeType::VAR_DECL) {
            return false;
        }
        return false;
    }

    std::string getAssignmentTarget(std::shared_ptr<ASTNode> assignment) {
        if (!assignment || assignment->children.empty()) return "";
        auto target = assignment->children[0];
        if (!target) return "";
        
        // Handle simple identifiers
        if (target->type == ASTNodeType::IDENTIFIER) {
            return target->value;
        }
        // Handle member access like corp.ceo.id
        else if (target->type == ASTNodeType::MEMBER_ACCESS) {
            // Return the base identifier name (corp) for global tracking
            return extractBaseIdentifier(target);
        }
        // Handle array access like people[i]
        else if (target->type == ASTNodeType::ARRAY_ACCESS) {
            if (target->children.size() > 0 && target->children[0]) {
                return getAssignmentTarget(target->children[0]);
            }
        }
        
        return "";
    }

    std::string extractBaseIdentifier(std::shared_ptr<ASTNode> node) {
        if (!node) return "";
        
        if (node->type == ASTNodeType::IDENTIFIER) {
            return node->value;
        }
        else if (node->type == ASTNodeType::MEMBER_ACCESS) {
            if (node->children.size() > 0 && node->children[0]) {
                return extractBaseIdentifier(node->children[0]);
            }
        }
        else if (node->type == ASTNodeType::ARRAY_ACCESS) {
            if (node->children.size() > 0 && node->children[0]) {
                return extractBaseIdentifier(node->children[0]);
            }
        }
        
        return node->value; // fallback
    }


    void optimizeDeadCode(std::shared_ptr<ASTNode> node) {
        // Only remove obviously dead code, preserve everything else
        if (!node) return;
        
        // Only remove assignments to local variables that are truly dead
        // Never remove anything involving globals
        if (node->type == ASTNodeType::BODY) {
            std::vector<std::shared_ptr<ASTNode>> newChildren;
            for (auto& child : node->children) {
                bool shouldKeep = true;
                if (child && child->type == ASTNodeType::ASSIGNMENT) {
                    std::string target = getAssignmentTarget(child);
                    bool isGlobal = (globalVariables.find(target) != globalVariables.end());
                    
                    // Only remove if it's local AND truly dead
                    if (!isGlobal && isDeadAssignment(child)) {
                        std::cout << "🔥 OPTIMIZATION: Removing dead assignment to local '" 
                                << target << "'" << std::endl;
                        shouldKeep = false;
                    }
                }
                if (shouldKeep) {
                    newChildren.push_back(child);
                }
            }
            if (newChildren.size() != node->children.size()) {
                node->children = newChildren;
            }
        }
        
        for (auto& child : node->children) {
            optimizeDeadCode(child);
        }
    }


    void reportOptimizations() {
        std::cout << "\n=== OPTIMIZATION REPORT ===" << std::endl;
        std::vector<std::string> unusedVars;
        std::vector<std::string> writeOnlyVars;
        std::vector<std::string> unusedRoutines;
        std::vector<std::string> unusedGlobals;  // NEW: Track unused globals separately
        
        for (const auto& var : declaredIdentifiers) {
            if (routineDeclarations.find(var) != routineDeclarations.end()) continue;
            
            bool isRead = (readVariables.find(var) != readVariables.end());
            bool isWritten = (writtenVariables.find(var) != writtenVariables.end());
            bool isGlobal = (globalVariables.find(var) != globalVariables.end());
            
            if (!isRead && !isWritten) {
                if (isGlobal) {
                    unusedGlobals.push_back(var);  // Separate unused globals
                } else {
                    unusedVars.push_back(var);
                }
            } else if (isWritten && !isRead) {
                writeOnlyVars.push_back(var);
            }
        }
        
        for (const auto& routine : routineDeclarations) {
            bool isCalled = (calledRoutines.find(routine) != calledRoutines.end());
            bool isEntryPoint = (routine == "main" || routine == "testRunner");
            if (!isCalled && !isEntryPoint) {
                unusedRoutines.push_back(routine);
            }
        }
        
        if (!unusedVars.empty()) {
            std::cout << "Unused LOCAL variables: ";
            for (size_t i = 0; i < unusedVars.size(); ++i) {
                std::cout << unusedVars[i];
                if (i < unusedVars.size() - 1) std::cout << ", ";
            }
            std::cout << std::endl;
        }
        
        if (!unusedGlobals.empty()) {
            std::cout << "Unused GLOBAL variables: ";
            for (size_t i = 0; i < unusedGlobals.size(); ++i) {
                std::cout << unusedGlobals[i];
                if (i < unusedGlobals.size() - 1) std::cout << ", ";
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
        
        if (unusedVars.empty() && unusedGlobals.empty() && writeOnlyVars.empty() && unusedRoutines.empty()) {
            std::cout << "✓ All declarations are properly used" << std::endl;
        }
        
        std::cout << "Statistics:" << std::endl;
        std::cout << "  Total declarations: " << declaredIdentifiers.size() << std::endl;
        std::cout << "  Variables read: " << readVariables.size() << std::endl;
        std::cout << "  Variables written: " << writtenVariables.size() << std::endl;
        std::cout << "  Routine declarations: " << routineDeclarations.size() << std::endl;
        std::cout << "  Routines called: " << calledRoutines.size() << std::endl;
        std::cout << "  Known array types: " << arraySizes.size() << std::endl;
        std::cout << "  Global variables: " << globalVariables.size() << std::endl;  // NEW: Show globals count
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