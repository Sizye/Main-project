#include "wasm_compiler.h"

#include <iostream>
#include <cstring>
#include <cmath>

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
        case ASTNodeType::RETURN_STMT: return "RETURN_STMT";
        case ASTNodeType::BINARY_OP: return "BINARY_OP";
        case ASTNodeType::UNARY_OP: return "UNARY_OP";
        case ASTNodeType::LITERAL_INT: return "LITERAL_INT";
        case ASTNodeType::LITERAL_BOOL: return "LITERAL_BOOL";
        case ASTNodeType::LITERAL_REAL: return "LITERAL_REAL";
        case ASTNodeType::IDENTIFIER: return "IDENTIFIER";
        case ASTNodeType::ROUTINE_CALL: return "ROUTINE_CALL";
        case ASTNodeType::ARGUMENT_LIST: return "ARGUMENT_LIST";
        default: return "OTHER";
    }
}

// ======================================================================
// Public API
// ======================================================================

bool WasmCompiler::compile(std::shared_ptr<ASTNode> program,
                           const std::string& filename) {
    std::cout << "🚀 COMPILING TO WASM: " << filename << std::endl;

    if (!collectFunctions(program)) {
        std::cerr << "❌ No routines found (need at least main)" << std::endl;
        return false;
    }

    std::vector<uint8_t> mod;

    // Magic + version
    const uint8_t header[8] = {
        0x00, 0x61, 0x73, 0x6d, // "\0asm"
        0x01, 0x00, 0x00, 0x00  // version 1
    };
    mod.insert(mod.end(), header, header + 8);

    auto typeSec = buildTypeSection();
    auto funcSec = buildFunctionSection();
    auto expSec  = buildExportSection();
    auto codeSec = buildCodeSection();

    mod.insert(mod.end(), typeSec.begin(), typeSec.end());
    mod.insert(mod.end(), funcSec.begin(), funcSec.end());
    mod.insert(mod.end(), expSec.begin(),  expSec.end());
    mod.insert(mod.end(), codeSec.begin(), codeSec.end());

    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::cerr << "❌ Cannot open " << filename << " for writing\n";
        return false;
    }
    out.write(reinterpret_cast<const char*>(mod.data()), mod.size());
    out.close();

    std::cout << "✅ WROTE WASM module (" << mod.size() << " bytes)\n";
    std::cout << "💡 You can run it with: wasmtime --invoke main " << filename << "\n";
    return true;
}

// ======================================================================
// Collect routines and signatures
// ======================================================================

bool WasmCompiler::collectFunctions(std::shared_ptr<ASTNode> program) {
    funcs.clear();
    funcIndexByName.clear();

    if (!program || program->type != ASTNodeType::PROGRAM) return false;

    // Gather ROUTINE_DECLs in appearance order
    for (auto& n : program->children) {
        if (!n) continue;
        if (n->type == ASTNodeType::ROUTINE_DECL) {
            FuncInfo F;
            F.name = n->value;
            F.node = n;
            F.typeIndex = 0; // filled later
            F.funcIndex = 0; // filled later
            analyzeFunctionSignature(F);
            funcs.push_back(std::move(F));
        }
    }

    if (funcs.empty()) return false;

    // Assign indices (typeIndex == func index for simplicity)
    for (uint32_t i = 0; i < funcs.size(); ++i) {
        funcs[i].typeIndex = i;
        funcs[i].funcIndex = i;
        funcIndexByName[funcs[i].name] = i;
    }

    // Ensure main exists
    if (!funcIndexByName.count("main")) {
        std::cerr << "❌ main routine not found\n";
        return false;
    }

    std::cout << "✅ Collected " << funcs.size() << " routines\n";
    return true;
}

void WasmCompiler::analyzeFunctionSignature(FuncInfo& F) {
    // Default: () -> i32
    F.paramTypes.clear();
    F.resultTypes.clear();

    // Scan children to find PARAMETER_LIST and return type
    std::shared_ptr<ASTNode> params = nullptr;
    std::shared_ptr<ASTNode> retType = nullptr;

    for (auto& ch : F.node->children) {
        if (!ch) continue;
        if (ch->type == ASTNodeType::PARAMETER_LIST) params = ch;
        else if (ch->type == ASTNodeType::PRIMITIVE_TYPE ||
                 ch->type == ASTNodeType::USER_TYPE) retType = ch;
    }

    // Params
    if (params) {
        for (auto& p : params->children) {
            if (!p || p->type != ASTNodeType::PARAMETER) continue;
            // find the type child
            uint8_t wt = 0x7f; // default i32
            for (auto& pc : p->children) {
                if (!pc) continue;
                if (pc->type == ASTNodeType::PRIMITIVE_TYPE) {
                    wt = mapPrimitiveToWasm(pc->value);
                } else if (pc->type == ASTNodeType::USER_TYPE) {
                    // user-defined primitive aliases not lowered yet; default i32
                    wt = 0x7f;
                }
            }
            F.paramTypes.push_back(wt);
        }
    }

    // Result
    if (retType) {
        F.resultTypes.push_back(mapPrimitiveToWasm(retType->value));
    } else {
        // Default i32 (your tests use integer)
        F.resultTypes.push_back(0x7f);
    }
}

uint8_t WasmCompiler::mapPrimitiveToWasm(const std::string& tname) {
    if (tname == "integer" || tname == "boolean") return 0x7f; // i32
    if (tname == "real") return 0x7c; // f64
    return 0x7f;
}

// ======================================================================
// Encoders
// ======================================================================

void WasmCompiler::writeLeb128(std::vector<uint8_t>& buf, uint32_t v) {
    do {
        uint8_t b = v & 0x7f;
        v >>= 7;
        if (v) b |= 0x80;
        buf.push_back(b);
    } while (v);
}

void WasmCompiler::writeString(std::vector<uint8_t>& buf, const std::string& s) {
    writeLeb128(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

// ======================================================================
// Sections
// ======================================================================

std::vector<uint8_t> WasmCompiler::buildTypeSection() {
    std::vector<uint8_t> payload;

    // one func type per routine
    writeLeb128(payload, static_cast<uint32_t>(funcs.size()));
    for (auto& F : funcs) {
        payload.push_back(0x60); // func
        // params
        writeLeb128(payload, static_cast<uint32_t>(F.paramTypes.size()));
        for (auto t : F.paramTypes) payload.push_back(t);
        // results
        writeLeb128(payload, static_cast<uint32_t>(F.resultTypes.size()));
        for (auto t : F.resultTypes) payload.push_back(t);
    }

    std::vector<uint8_t> sec;
    sec.push_back(0x01);
    writeLeb128(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
    return sec;
}

std::vector<uint8_t> WasmCompiler::buildFunctionSection() {
    std::vector<uint8_t> payload;
    writeLeb128(payload, static_cast<uint32_t>(funcs.size())); // count
    for (auto& F : funcs) {
        writeLeb128(payload, F.typeIndex); // type index == func index here
    }

    std::vector<uint8_t> sec;
    sec.push_back(0x03);
    writeLeb128(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
    return sec;
}

std::vector<uint8_t> WasmCompiler::buildExportSection() {
    std::vector<uint8_t> payload;

    // only export "main"
    writeLeb128(payload, 1);
    writeString(payload, "main");
    payload.push_back(0x00); // kind: func
    writeLeb128(payload, funcIndexByName["main"]);

    std::vector<uint8_t> sec;
    sec.push_back(0x07);
    writeLeb128(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
    return sec;
}

std::vector<uint8_t> WasmCompiler::buildCodeSection() {
    std::vector<uint8_t> payload;

    // function count
    writeLeb128(payload, static_cast<uint32_t>(funcs.size()));

    for (auto& F : funcs) {
        // Build one function body
        std::vector<uint8_t> body;

        resetLocals();
        addParametersToLocals(F); // params occupy local indices 0..n-1

        // locals header (only VAR_DECL locals, not params)
        auto localsHeader = analyzeLocalVariables(F);
        body.insert(body.end(), localsHeader.begin(), localsHeader.end());

        // local initializers
        generateLocalInitializers(body, F);

        // body stmts
        generateFunctionBody(body, F);

        // fallback: if path reaches end without return
        if (!F.resultTypes.empty()) {
            // default return 0 / 0.0
            if (F.resultTypes[0] == 0x7f) emitI32Const(body, 0);
            else                          emitF64Const(body, 0.0);
            body.push_back(0x0f); // return
        }

        body.push_back(0x0b); // end

        // prepend size
        std::vector<uint8_t> entry;
        writeLeb128(entry, static_cast<uint32_t>(body.size()));
        entry.insert(entry.end(), body.begin(), body.end());

        payload.insert(payload.end(), entry.begin(), entry.end());
    }

    std::vector<uint8_t> sec;
    sec.push_back(0x0a);
    writeLeb128(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
    return sec;
}

// ======================================================================
// Per-function codegen utilities
// ======================================================================

void WasmCompiler::resetLocals() {
    localVarIndices.clear();
    nextLocalIndex = 0;
}

void WasmCompiler::addParametersToLocals(const FuncInfo& F) {
    // parameters become locals 0..n-1
    std::shared_ptr<ASTNode> params = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::PARAMETER_LIST) { params = ch; break; }
    }
    if (!params) return;
    int idx = 0;
    for (auto& p : params->children) {
        if (!p || p->type != ASTNodeType::PARAMETER) continue;
        localVarIndices[p->value] = idx++;
    }
    nextLocalIndex = idx;
}

std::vector<uint8_t> WasmCompiler::analyzeLocalVariables(const FuncInfo& F) {
    std::vector<uint8_t> buf;
    std::vector<std::pair<uint32_t,uint8_t>> locals;

    std::shared_ptr<ASTNode> bodyNode = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
    }
    if (!bodyNode) {
        writeLeb128(buf, 0);
        return buf;
    }

    for (auto& s : bodyNode->children) {
        if (!s || s->type != ASTNodeType::VAR_DECL) continue;
        const std::string& name = s->value;
        if (localVarIndices.count(name)) continue; // skip params
        localVarIndices[name] = nextLocalIndex++;
        uint8_t wt = 0x7f;
        if (!s->children.empty() && s->children[0] &&
            s->children[0]->type == ASTNodeType::PRIMITIVE_TYPE) {
            wt = mapPrimitiveToWasm(s->children[0]->value);
        }
        locals.push_back({1, wt});
    }

    writeLeb128(buf, static_cast<uint32_t>(locals.size()));
    for (auto& p : locals) {
        writeLeb128(buf, p.first);
        buf.push_back(p.second);
    }
    return buf;
}

void WasmCompiler::generateLocalInitializers(std::vector<uint8_t>& body, const FuncInfo& F) {
    std::shared_ptr<ASTNode> bodyNode = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
    }
    if (!bodyNode) return;

    for (auto& s : bodyNode->children) {
        if (!s || s->type != ASTNodeType::VAR_DECL) continue;
        const std::string& name = s->value;
        auto it = localVarIndices.find(name);
        if (it == localVarIndices.end()) continue;

        // children: [TYPE_NODE, optional INIT_EXPR]
        if (s->children.size() >= 2 && s->children[1]) {
            generateExpression(body, s->children[1], F);
            body.push_back(0x21); // local.set
            writeLeb128(body, static_cast<uint32_t>(it->second));
        }
    }
}

void WasmCompiler::generateFunctionBody(std::vector<uint8_t>& body, const FuncInfo& F) {
    std::shared_ptr<ASTNode> bodyNode = nullptr;
    for (auto& ch : F.node->children) {
        if (ch && ch->type == ASTNodeType::BODY) { bodyNode = ch; break; }
    }
    if (!bodyNode) return;

    for (auto& s : bodyNode->children) {
        if (!s) continue;
        switch (s->type) {
            case ASTNodeType::VAR_DECL: // already handled
                break;
            case ASTNodeType::ASSIGNMENT:
                generateAssignment(body, s, F);
                break;
            case ASTNodeType::IF_STMT:
                generateIfStatement(body, s, F);
                break;
            case ASTNodeType::WHILE_LOOP:
                generateWhileLoop(body, s, F);
                break;
            case ASTNodeType::RETURN_STMT:
                generateReturn(body, s, F);
                break;
            default:
                std::cout << "  ⚠️ Unhandled stmt in "
                          << F.name << ": " << tname(s->type) << "\n";
                break;
        }
    }
}

// ======================================================================
// Statements
// ======================================================================

void WasmCompiler::generateAssignment(std::vector<uint8_t>& body,
                                      std::shared_ptr<ASTNode> a,
                                      const FuncInfo& F) {
    (void)F;
    if (!a || a->children.size() != 2) return;
    auto lhs = a->children[0];
    auto rhs = a->children[1];
    if (!lhs || lhs->type != ASTNodeType::IDENTIFIER) {
        std::cout << "⚠️ Only simple identifier assignments supported\n";
        return;
    }
    generateExpression(body, rhs, F);
    emitLocalSet(body, lhs->value);
}

void WasmCompiler::generateIfStatement(std::vector<uint8_t>& body,
                                       std::shared_ptr<ASTNode> ifs,
                                       const FuncInfo& F) {
    if (!ifs || ifs->children.size() < 2) return;
    auto cond = ifs->children[0];
    auto thenB = ifs->children[1];
    std::shared_ptr<ASTNode> elseB =
        (ifs->children.size() > 2) ? ifs->children[2] : nullptr;

    generateExpression(body, cond, F);
    body.push_back(0x04); // if
    body.push_back(0x40); // empty block type
    if (thenB && thenB->type == ASTNodeType::BODY) {
        for (auto& s : thenB->children) {
            if (!s) continue;
            switch (s->type) {
                case ASTNodeType::ASSIGNMENT: generateAssignment(body, s, F); break;
                case ASTNodeType::IF_STMT:     generateIfStatement(body, s, F); break;
                case ASTNodeType::WHILE_LOOP:  generateWhileLoop(body, s, F); break;
                case ASTNodeType::RETURN_STMT: generateReturn(body, s, F); break;
                case ASTNodeType::VAR_DECL:    break;
                default:
                    std::cout << "  ⚠️ Unhandled THEN stmt: " << tname(s->type) << "\n";
                    break;
            }
        }
    }
    if (elseB) {
        body.push_back(0x05); // else
        if (elseB->type == ASTNodeType::BODY) {
            for (auto& s : elseB->children) {
                if (!s) continue;
                switch (s->type) {
                    case ASTNodeType::ASSIGNMENT: generateAssignment(body, s, F); break;
                    case ASTNodeType::IF_STMT:     generateIfStatement(body, s, F); break;
                    case ASTNodeType::WHILE_LOOP:  generateWhileLoop(body, s, F); break;
                    case ASTNodeType::RETURN_STMT: generateReturn(body, s, F); break;
                    case ASTNodeType::VAR_DECL:    break;
                    default:
                        std::cout << "  ⚠️ Unhandled ELSE stmt: " << tname(s->type) << "\n";
                        break;
                }
            }
        }
    }
    body.push_back(0x0b); // end
}

void WasmCompiler::generateWhileLoop(std::vector<uint8_t>& body,
                                     std::shared_ptr<ASTNode> w,
                                     const FuncInfo& F) {
    if (!w || w->children.size() < 2) return;
    auto cond = w->children[0];
    auto loopB = w->children[1];

    body.push_back(0x02); // block
    body.push_back(0x40);
    body.push_back(0x03); // loop
    body.push_back(0x40);

    generateExpression(body, cond, F);
    body.push_back(0x45); // i32.eqz
    body.push_back(0x0d); // br_if
    body.push_back(0x01); // to outer block

    if (loopB && loopB->type == ASTNodeType::BODY) {
        for (auto& s : loopB->children) {
            if (!s) continue;
            switch (s->type) {
                case ASTNodeType::ASSIGNMENT: generateAssignment(body, s, F); break;
                case ASTNodeType::IF_STMT:     generateIfStatement(body, s, F); break;
                case ASTNodeType::WHILE_LOOP:  generateWhileLoop(body, s, F); break;
                case ASTNodeType::RETURN_STMT: generateReturn(body, s, F); break;
                case ASTNodeType::VAR_DECL:    break;
                default:
                    std::cout << "  ⚠️ Unhandled WHILE stmt: " << tname(s->type) << "\n";
                    break;
            }
        }
    }

    body.push_back(0x0c); // br
    body.push_back(0x00); // to loop
    body.push_back(0x0b); // end loop
    body.push_back(0x0b); // end block
}

void WasmCompiler::generateReturn(std::vector<uint8_t>& body,
                                  std::shared_ptr<ASTNode> r,
                                  const FuncInfo& F) {
    if (!r) return;
    if (!F.resultTypes.empty()) {
        if (!r->children.empty() && r->children[0]) {
            generateExpression(body, r->children[0], F);
        } else {
            if (F.resultTypes[0] == 0x7f) emitI32Const(body, 0);
            else                          emitF64Const(body, 0.0);
        }
    }
    body.push_back(0x0f); // return
}

// ======================================================================
// Expressions
// ======================================================================

void WasmCompiler::generateExpression(std::vector<uint8_t>& body,
                                      std::shared_ptr<ASTNode> e,
                                      const FuncInfo& F) {
    if (!e) return;
    switch (e->type) {
        case ASTNodeType::LITERAL_INT: {
            try { emitI32Const(body, std::stoi(e->value)); }
            catch (...) { emitI32Const(body, 0); }
            break;
        }
        case ASTNodeType::LITERAL_BOOL:
            emitI32Const(body, e->value == "true" ? 1 : 0);
            break;
        case ASTNodeType::LITERAL_REAL: {
            try { emitF64Const(body, std::stod(e->value)); }
            catch (...) { emitF64Const(body, 0.0); }
            break;
        }
        case ASTNodeType::IDENTIFIER:
            emitLocalGet(body, e->value);
            break;
        case ASTNodeType::BINARY_OP:
            generateBinaryOp(body, e, F);
            break;
        case ASTNodeType::UNARY_OP: {
            const std::string& op = e->value;
            if (op == "not" && !e->children.empty()) {
                generateExpression(body, e->children[0], F);
                body.push_back(0x45); // i32.eqz
            } else {
                emitI32Const(body, 0);
            }
            break;
        }
        case ASTNodeType::ROUTINE_CALL:
            generateCall(body, e, F);
            break;
        default:
            std::cout << "  ⚠️ Unhandled expr: " << tname(e->type) << "\n";
            emitI32Const(body, 0);
            break;
    }
}

void WasmCompiler::generateBinaryOp(std::vector<uint8_t>& body,
                                    std::shared_ptr<ASTNode> bin,
                                    const FuncInfo& F) {
    (void)F;
    if (!bin || bin->children.size() != 2) { emitI32Const(body, 0); return; }
    auto L = bin->children[0];
    auto R = bin->children[1];
    generateExpression(body, L, F);
    generateExpression(body, R, F);

    const std::string& op = bin->value;
    if      (op == "+")  body.push_back(0x6a);
    else if (op == "-")  body.push_back(0x6b);
    else if (op == "*")  body.push_back(0x6c);
    else if (op == "/")  body.push_back(0x6d);
    else if (op == "and") body.push_back(0x71);
    else if (op == "or")  body.push_back(0x72);
    else if (op == "xor") body.push_back(0x73);
    else if (op == "<")   body.push_back(0x48);
    else if (op == "<=")  body.push_back(0x4c);
    else if (op == ">")   body.push_back(0x4a);
    else if (op == ">=")  body.push_back(0x4e);
    else if (op == "=")   body.push_back(0x46);
    else if (op == "/=")  body.push_back(0x47);
    else {
        std::cout << "  ⚠️ Unhandled binop: " << op << "\n";
    }
}

void WasmCompiler::generateCall(std::vector<uint8_t>& body,
                                std::shared_ptr<ASTNode> call,
                                const FuncInfo& F) {
    (void)F;
    // ROUTINE_CALL.value = callee name, children = arguments (or ARGUMENT_LIST child)
    std::vector<std::shared_ptr<ASTNode>> args;

    for (auto& ch : call->children) {
        if (!ch) continue;
        if (ch->type == ASTNodeType::ARGUMENT_LIST) {
            for (auto& a : ch->children) if (a) args.push_back(a);
        } else {
            args.push_back(ch);
        }
    }

    for (auto& a : args) generateExpression(body, a, F);

    auto it = funcIndexByName.find(call->value);
    if (it == funcIndexByName.end()) {
        std::cout << "  ⚠️ Unknown callee: " << call->value << " (push 0)\n";
        emitI32Const(body, 0);
        return;
    }
    body.push_back(0x10); // call
    writeLeb128(body, it->second);
}

// ======================================================================
// Emit helpers
// ======================================================================

void WasmCompiler::emitI32Const(std::vector<uint8_t>& body, int v) {
    body.push_back(0x41);
    writeLeb128(body, static_cast<uint32_t>(v));
}

void WasmCompiler::emitF64Const(std::vector<uint8_t>& body, double d) {
    body.push_back(0x44);
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(double));
    for (int i = 0; i < 8; ++i) {
        body.push_back(static_cast<uint8_t>(bits & 0xff));
        bits >>= 8;
    }
}

void WasmCompiler::emitLocalGet(std::vector<uint8_t>& body, const std::string& name) {
    auto it = localVarIndices.find(name);
    if (it == localVarIndices.end()) {
        std::cout << "  ⚠️ Unknown local get: " << name << " (use 0)\n";
        emitI32Const(body, 0);
        return;
    }
    body.push_back(0x20);
    writeLeb128(body, static_cast<uint32_t>(it->second));
}

void WasmCompiler::emitLocalSet(std::vector<uint8_t>& body, const std::string& name) {
    auto it = localVarIndices.find(name);
    if (it == localVarIndices.end()) {
        std::cout << "  ⚠️ Unknown local set: " << name << "\n";
        return;
    }
    body.push_back(0x21);
    writeLeb128(body, static_cast<uint32_t>(it->second));
}
