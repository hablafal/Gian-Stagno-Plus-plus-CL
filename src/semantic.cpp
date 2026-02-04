#include "semantic.h"
#include <sstream>

namespace gspp {

SemanticAnalyzer::SemanticAnalyzer(Program* program) : program_(program) {}

void SemanticAnalyzer::pushScope() {
    scopes_.emplace_back();
}

void SemanticAnalyzer::popScope() {
    scopes_.pop_back();
}

void SemanticAnalyzer::addVar(const std::string& name, const Type& type, bool isParam) {
    VarSymbol sym;
    sym.name = name;
    sym.type = type;
    sym.isParam = isParam;
    if (!scopes_.empty()) {
        if (isParam)
            sym.frameOffset = 0;  // set later from param index
        else {
            nextFrameOffset_ += 8;  // 8 bytes per local on x64
            sym.frameOffset = -nextFrameOffset_;
        }
        scopes_.back()[name] = sym;
        if (currentFuncSymbol_)
            currentFuncSymbol_->locals[name] = sym;
    }
}

VarSymbol* SemanticAnalyzer::lookupVar(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto i = it->find(name);
        if (i != it->end()) return &i->second;
    }
    return nullptr;
}

void SemanticAnalyzer::error(const std::string& msg, SourceLoc loc) {
    std::ostringstream os;
    os << ":" << loc.line << ":" << loc.column << ": error: " << msg;
    errors_.push_back(os.str());
}

StructDef* SemanticAnalyzer::getStruct(const std::string& name) {
    auto i = structs_.find(name);
    return i == structs_.end() ? nullptr : &i->second;
}

FuncSymbol* SemanticAnalyzer::getFunc(const std::string& name) {
    auto i = functions_.find(name);
    return i == functions_.end() ? nullptr : &i->second;
}

Type SemanticAnalyzer::resolveType(const Type& t) {
    if (t.kind != Type::Kind::StructRef) return t;
    StructDef* sd = getStruct(t.structName);
    if (!sd) return t;
    Type r = t;
    r.kind = Type::Kind::StructRef;
    r.structName = t.structName;
    return r;
}

void SemanticAnalyzer::analyzeStruct(const StructDecl& s) {
    StructDef def;
    def.name = s.name;
    size_t offset = 0;
    for (size_t i = 0; i < s.members.size(); i++) {
        const auto& m = s.members[i];
        Type ty = resolveType(m.type);
        def.members.push_back({m.name, ty});
        def.memberIndex[m.name] = i;
        if (ty.kind == Type::Kind::Int || ty.kind == Type::Kind::Float || ty.kind == Type::Kind::Bool)
            offset += 8;
        else if (ty.kind == Type::Kind::StructRef) {
            StructDef* sd = getStruct(ty.structName);
            offset += sd ? sd->sizeBytes : 8;
        }
    }
    def.sizeBytes = (offset + 7) & ~7;
    structs_[s.name] = std::move(def);
}

void SemanticAnalyzer::analyzeFunc(const FuncDecl& f) {
    FuncSymbol sym;
    sym.name = f.name;
    sym.returnType = resolveType(f.returnType);
    sym.decl = &f;
    for (const auto& p : f.params)
        sym.paramTypes.push_back(resolveType(p.type));
    functions_[f.name] = std::move(sym);
    FuncSymbol& fs = functions_[f.name];

    pushScope();
    currentFunc_ = const_cast<FuncDecl*>(&f);
    currentFuncSymbol_ = &fs;
    nextFrameOffset_ = 0;
    // Windows x64: first 4 args in RCX, RDX, R8, R9. We spill to [RBP+16], [RBP+24], ...
    int paramOffset = 16;
    for (size_t i = 0; i < f.params.size(); i++) {
        Type pt = resolveType(f.params[i].type);
        addVar(f.params[i].name, pt, true);
        VarSymbol* vs = lookupVar(f.params[i].name);
        if (vs) {
            vs->frameOffset = paramOffset;
            fs.locals[f.params[i].name] = *vs;
        }
        paramOffset += 8;
    }
    if (f.body) analyzeStmt(f.body.get());
    popScope();
    currentFunc_ = nullptr;
    currentFuncSymbol_ = nullptr;
}

Type SemanticAnalyzer::analyzeExpr(Expr* expr) {
    if (!expr) return Type{};
    switch (expr->kind) {
        case Expr::Kind::IntLit:
            expr->exprType.kind = Type::Kind::Int;
            return expr->exprType;
        case Expr::Kind::FloatLit:
            expr->exprType.kind = Type::Kind::Float;
            return expr->exprType;
        case Expr::Kind::BoolLit:
            expr->exprType.kind = Type::Kind::Bool;
            return expr->exprType;
        case Expr::Kind::Var: {
            VarSymbol* vs = lookupVar(expr->ident);
            if (!vs) {
                error("undefined variable '" + expr->ident + "'", expr->loc);
                expr->exprType.kind = Type::Kind::Int;
                return expr->exprType;
            }
            expr->exprType = vs->type;
            return expr->exprType;
        }
        case Expr::Kind::Binary: {
            Type l = analyzeExpr(expr->left.get());
            Type r = analyzeExpr(expr->right.get());
            if (expr->op == "and" || expr->op == "or" || expr->op == "==" || expr->op == "!=" ||
                expr->op == "<" || expr->op == ">" || expr->op == "<=" || expr->op == ">=") {
                expr->exprType.kind = Type::Kind::Bool;
                return expr->exprType;
            }
            if (expr->op == "+" || expr->op == "-" || expr->op == "*" || expr->op == "/" || expr->op == "%") {
                expr->exprType = l;
                return expr->exprType;
            }
            expr->exprType.kind = Type::Kind::Int;
            return expr->exprType;
        }
        case Expr::Kind::Unary: {
            Type o = analyzeExpr(expr->right.get());
            if (expr->op == "not") {
                expr->exprType.kind = Type::Kind::Bool;
                return expr->exprType;
            }
            expr->exprType = o;
            return expr->exprType;
        }
        case Expr::Kind::Call: {
            FuncSymbol* fs = getFunc(expr->ident);
            if (!fs) {
                error("undefined function '" + expr->ident + "'", expr->loc);
                expr->exprType.kind = Type::Kind::Int;
                return expr->exprType;
            }
            if (expr->args.size() != fs->paramTypes.size()) {
                error("argument count mismatch for '" + expr->ident + "'", expr->loc);
            }
            for (size_t i = 0; i < expr->args.size(); i++) {
                analyzeExpr(expr->args[i].get());
            }
            expr->exprType = fs->returnType;
            return expr->exprType;
        }
        case Expr::Kind::Member: {
            Type base = analyzeExpr(expr->left.get());
            if (base.kind != Type::Kind::StructRef) {
                error("member access on non-struct type", expr->loc);
                expr->exprType.kind = Type::Kind::Int;
                return expr->exprType;
            }
            StructDef* sd = getStruct(base.structName);
            if (!sd) {
                error("unknown struct '" + base.structName + "'", expr->loc);
                expr->exprType.kind = Type::Kind::Int;
                return expr->exprType;
            }
            auto it = sd->memberIndex.find(expr->member);
            if (it == sd->memberIndex.end()) {
                error("no member '" + expr->member + "' in struct '" + base.structName + "'", expr->loc);
                expr->exprType.kind = Type::Kind::Int;
                return expr->exprType;
            }
            expr->exprType = sd->members[it->second].second;
            return expr->exprType;
        }
        default:
            expr->exprType.kind = Type::Kind::Int;
            return expr->exprType;
    }
}

void SemanticAnalyzer::analyzeStmt(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::Block:
            pushScope();
            for (auto& s : stmt->blockStmts) analyzeStmt(s.get());
            popScope();
            break;
        case Stmt::Kind::VarDecl: {
            Type ty = resolveType(stmt->varType);
            if (stmt->varInit) {
                Type initTy = analyzeExpr(stmt->varInit.get());
                if (ty.kind == Type::Kind::Int && stmt->varType.structName.empty())
                    ty = initTy;  // infer from initializer when no explicit type
                stmt->varType = ty;
            }
            addVar(stmt->varName, ty);
            break;
        }
        case Stmt::Kind::Assign: {
            analyzeExpr(stmt->assignTarget.get());
            analyzeExpr(stmt->assignValue.get());
            break;
        }
        case Stmt::Kind::If:
            analyzeExpr(stmt->condition.get());
            analyzeStmt(stmt->thenBranch.get());
            if (stmt->elseBranch) analyzeStmt(stmt->elseBranch.get());
            break;
        case Stmt::Kind::While:
            analyzeExpr(stmt->condition.get());
            analyzeStmt(stmt->body.get());
            break;
        case Stmt::Kind::For:
            pushScope();
            analyzeStmt(stmt->initStmt.get());
            analyzeExpr(stmt->condition.get());
            analyzeStmt(stmt->stepStmt.get());
            analyzeStmt(stmt->body.get());
            popScope();
            break;
        case Stmt::Kind::Return:
            if (stmt->returnExpr) analyzeExpr(stmt->returnExpr.get());
            break;
        case Stmt::Kind::ExprStmt:
            analyzeExpr(stmt->expr.get());
            break;
    }
}

void SemanticAnalyzer::analyzeProgram() {
    for (const auto& s : program_->structs)
        analyzeStruct(s);
    // Register builtins so they are known during analysis
    FuncSymbol printlnSym;
    printlnSym.name = "println";
    printlnSym.returnType.kind = Type::Kind::Int;
    printlnSym.paramTypes.push_back(Type{});
    printlnSym.paramTypes.back().kind = Type::Kind::Int;
    functions_["println"] = std::move(printlnSym);
    FuncSymbol printSym;
    printSym.name = "print";
    printSym.returnType.kind = Type::Kind::Int;
    printSym.paramTypes.push_back(Type{});
    printSym.paramTypes.back().kind = Type::Kind::Int;
    functions_["print"] = std::move(printSym);
    FuncSymbol pfSym;
    pfSym.name = "print_float";
    pfSym.returnType.kind = Type::Kind::Int;
    pfSym.paramTypes.push_back(Type{});
    pfSym.paramTypes.back().kind = Type::Kind::Float;
    functions_["print_float"] = std::move(pfSym);
    FuncSymbol pflSym;
    pflSym.name = "println_float";
    pflSym.returnType.kind = Type::Kind::Int;
    pflSym.paramTypes.push_back(Type{});
    pflSym.paramTypes.back().kind = Type::Kind::Float;
    functions_["println_float"] = std::move(pflSym);
    for (const auto& f : program_->functions)
        analyzeFunc(f);
}

bool SemanticAnalyzer::analyze() {
    analyzeProgram();
    return errors_.empty();
}

} // namespace gspp
