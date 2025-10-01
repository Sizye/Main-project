#include "ast.h"
#include <sstream>
#include <functional>

std::shared_ptr<ASTNode> astRoot = nullptr;

static const char* names[] = {
    "PROGRAM","VAR_DECL","TYPE_DECL","ROUTINE_DECL","ROUTINE_FORWARD_DECL",
    "PARAMETER","PRIMITIVE_TYPE","ARRAY_TYPE","RECORD_TYPE","USER_TYPE",
    "BINARY_OP","UNARY_OP","LITERAL_INT","LITERAL_REAL","LITERAL_BOOL",
    "LITERAL_STRING","IDENTIFIER","ROUTINE_CALL","ARRAY_ACCESS",
    "MEMBER_ACCESS","SIZE_EXPRESSION","ASSIGNMENT","IF_STMT","WHILE_LOOP",
    "FOR_LOOP","PRINT_STMT","RETURN_STMT","BODY","EXPR_LIST",
    "PARAM_LIST","ARG_LIST","RANGE"
};

std::string ASTNode::typeToStr() const {
    return names[static_cast<int>(type)];
}

std::string ASTNode::toDot() const {
    std::ostringstream out;
    out << "digraph AST {\n  node [shape=box];\n";
    int counter = 0;
    std::function<void(const ASTNode*, int)> rec =
        [&](const ASTNode* n, int id) {
            std::string lbl = n->typeToStr();
            if (!n->value.empty()) lbl += "\\n" + n->value;
            out << "  n" << id << " [label=\"" << lbl << "\"];\n";
            int cid = id + 1;
            for (auto& ch : n->children) {
                out << "  n" << id << " -> n" << cid << ";\n";
                rec(ch.get(), cid);
                cid += 1000; // простой сдвиг id
            }
        };
    rec(this, counter);
    out << "}\n";
    return out.str();
}

std::shared_ptr<ASTNode> createNode(ASTNodeType t, const std::string& v) {
    return std::make_shared<ASTNode>(t, v);
}
std::shared_ptr<ASTNode> createBinaryOp(const std::string& op,
                                        std::shared_ptr<ASTNode> l,
                                        std::shared_ptr<ASTNode> r) {
    auto n = createNode(ASTNodeType::BINARY_OP, op);
    n->addChild(l); n->addChild(r);
    return n;
}
std::shared_ptr<ASTNode> createUnaryOp(const std::string& op,
                                       std::shared_ptr<ASTNode> o) {
    auto n = createNode(ASTNodeType::UNARY_OP, op);
    n->addChild(o);
    return n;
}
