#ifndef AST_H
#define AST_H

#include <memory>
#include <vector>
#include <string>
#include <iostream>

/* ---- Типы узлов ---- */
enum class ASTNodeType {
    PROGRAM, VAR_DECL, TYPE_DECL, ROUTINE_DECL, ROUTINE_FORWARD_DECL,
    PARAMETER, PRIMITIVE_TYPE, ARRAY_TYPE, RECORD_TYPE, USER_TYPE,
    BINARY_OP, UNARY_OP, LITERAL_INT, LITERAL_REAL, LITERAL_BOOL,
    LITERAL_STRING, IDENTIFIER, ROUTINE_CALL, ARRAY_ACCESS,
    MEMBER_ACCESS, SIZE_EXPRESSION, ASSIGNMENT, IF_STMT, WHILE_LOOP,
    FOR_LOOP, PRINT_STMT, RETURN_STMT, BODY, EXPRESSION_LIST,
    PARAMETER_LIST, ARGUMENT_LIST, RANGE
};

class ASTNode {
public:
    ASTNodeType type;
    std::string value;
    std::vector<std::shared_ptr<ASTNode>> children;

    ASTNode(ASTNodeType t, std::string v = {}) : type(t), value(std::move(v)) {}
    void addChild(const std::shared_ptr<ASTNode>& n) { if (n) children.push_back(n); }
    void print(int d = 0) const {
        for (int i = 0; i < d; ++i) std::cout << "  ";
        std::cout << typeToStr() << (value.empty() ? "" : " (" + value + ")") << '\n';
        for (auto& c : children) if (c) c->print(d + 1);
    }
    std::string toDot() const;
private:
    std::string typeToStr() const;
};

/* фабрики AST */
std::shared_ptr<ASTNode> createNode(ASTNodeType t, const std::string& v = {});
std::shared_ptr<ASTNode> createBinaryOp(const std::string& op,
                                        std::shared_ptr<ASTNode> l,
                                        std::shared_ptr<ASTNode> r);
std::shared_ptr<ASTNode> createUnaryOp(const std::string& op,
                                       std::shared_ptr<ASTNode> o);

extern std::shared_ptr<ASTNode> astRoot;

#endif
