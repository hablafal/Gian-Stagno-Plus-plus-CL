#include "semantic.h"
#include <sstream>
#include <iostream>

namespace gspp {

SemanticAnalyzer::SemanticAnalyzer(Program* program) : program_(program) {}

void SemanticAnalyzer::addModule(const std::string& name, Program* prog) {
    modules_[name] = prog;

    // Save current state
    auto oldStructs = std::move(structs_);
    auto oldFunctions = std::move(functions_);
    auto oldNs = currentNamespace_;
    structs_.clear();
    functions_.clear();
    currentNamespace_ = name;

    // Store templates
    for (const auto& s : prog->structs) {
        if (s.typeParams.empty()) analyzeStruct(s);
        else moduleStructTemplates_[name][s.name] = &s;
    }
    for (const auto& f : prog->functions) {
        if (f.typeParams.empty()) analyzeFunc(f);
        else moduleFuncTemplates_[name][f.name] = &f;
    }

    moduleStructs_[name] = std::move(structs_);
    moduleFunctions_[name] = std::move(functions_);

    // Restore state
    structs_ = std::move(oldStructs);
    functions_ = std::move(oldFunctions);
    currentNamespace_ = oldNs;
}

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

std::string SemanticAnalyzer::typeName(const Type& t) {
    switch (t.kind) {
        case Type::Kind::Int: return "int";
        case Type::Kind::Float: return "float";
        case Type::Kind::Bool: return "bool";
        case Type::Kind::String: return "string";
        case Type::Kind::Char: return "char";
        case Type::Kind::Void: return "void";
        case Type::Kind::TypeParam: return t.structName;
        case Type::Kind::Pointer: return "ptr_" + typeName(*t.ptrTo);
        case Type::Kind::StructRef: {
            std::string n = t.ns.empty() ? t.structName : t.ns + "_" + t.structName;
            for (const auto& arg : t.typeArgs) n += "_" + typeName(arg);
            return n;
        }
    }
    return "unknown";
}

std::string SemanticAnalyzer::mangleGenericName(const std::string& name, const std::vector<Type>& args) {
    std::string m = name + "_";
    for (const auto& arg : args) m += typeName(arg) + "_";
    return m;
}

Type SemanticAnalyzer::substitute(const Type& t, const std::unordered_map<std::string, Type>& subs) {
    if (t.kind == Type::Kind::TypeParam || (t.kind == Type::Kind::StructRef && t.ns.empty())) {
        auto it = subs.find(t.structName);
        if (it != subs.end()) return it->second;
        if (t.kind == Type::Kind::TypeParam) return t;
    }
    Type res = t;
    if (t.ptrTo) res.ptrTo = std::make_unique<Type>(substitute(*t.ptrTo, subs));
    res.typeArgs.clear();
    for (const auto& arg : t.typeArgs) res.typeArgs.push_back(substitute(arg, subs));
    return res;
}

std::unique_ptr<Expr> SemanticAnalyzer::substituteExpr(const Expr* e, const std::unordered_map<std::string, Type>& subs) {
    if (!e) return nullptr;
    auto res = std::make_unique<Expr>();
    res->kind = e->kind;
    res->loc = e->loc;
    res->intVal = e->intVal;
    res->floatVal = e->floatVal;
    res->boolVal = e->boolVal;
    res->ident = e->ident;
    res->ns = e->ns;
    res->member = e->member;
    res->op = e->op;
    res->exprType = substitute(e->exprType, subs);
    if (e->targetType) res->targetType = std::make_unique<Type>(substitute(*e->targetType, subs));
    if (e->left) res->left = substituteExpr(e->left.get(), subs);
    if (e->right) res->right = substituteExpr(e->right.get(), subs);
    for (const auto& arg : e->args) res->args.push_back(substituteExpr(arg.get(), subs));
    return res;
}

std::unique_ptr<Stmt> SemanticAnalyzer::substituteStmt(const Stmt* s, const std::unordered_map<std::string, Type>& subs) {
    if (!s) return nullptr;
    auto res = std::make_unique<Stmt>();
    res->kind = s->kind;
    res->loc = s->loc;
    res->varName = s->varName;
    res->varType = substitute(s->varType, subs);
    res->asmCode = s->asmCode;
    if (s->varInit) res->varInit = substituteExpr(s->varInit.get(), subs);
    if (s->assignTarget) res->assignTarget = substituteExpr(s->assignTarget.get(), subs);
    if (s->assignValue) res->assignValue = substituteExpr(s->assignValue.get(), subs);
    if (s->condition) res->condition = substituteExpr(s->condition.get(), subs);
    if (s->thenBranch) res->thenBranch = substituteStmt(s->thenBranch.get(), subs);
    if (s->elseBranch) res->elseBranch = substituteStmt(s->elseBranch.get(), subs);
    if (s->body) res->body = substituteStmt(s->body.get(), subs);
    if (s->initStmt) res->initStmt = substituteStmt(s->initStmt.get(), subs);
    if (s->stepStmt) res->stepStmt = substituteStmt(s->stepStmt.get(), subs);
    if (s->returnExpr) res->returnExpr = substituteExpr(s->returnExpr.get(), subs);
    if (s->expr) res->expr = substituteExpr(s->expr.get(), subs);
    for (const auto& stmt : s->blockStmts) res->blockStmts.push_back(substituteStmt(stmt.get(), subs));
    return res;
}

void SemanticAnalyzer::instantiateStruct(const std::string& name, const std::string& ns, const std::vector<Type>& args) {
    if (args.empty()) return;
    std::string mangled = mangleGenericName(name, args);
    if (getStruct(mangled, ns)) return;

    const StructDecl* tmpl = nullptr;
    if (ns.empty()) {
        if (structTemplates_.count(name)) tmpl = structTemplates_[name];
    } else {
        if (moduleStructTemplates_.count(ns) && moduleStructTemplates_[ns].count(name))
            tmpl = moduleStructTemplates_[ns][name];
    }
    if (!tmpl) return;

    std::unordered_map<std::string, Type> subs;
    for (size_t i = 0; i < tmpl->typeParams.size() && i < args.size(); i++)
        subs[tmpl->typeParams[i]] = args[i];

    auto spec = std::make_unique<StructDecl>();
    spec->name = mangled;
    spec->loc = tmpl->loc;
    for (const auto& m : tmpl->members) {
        StructMember sm = m;
        sm.type = substitute(m.type, subs);
        spec->members.push_back(std::move(sm));
    }

    auto oldNs = currentNamespace_;
    currentNamespace_ = ns;
    analyzeStruct(*spec);
    instantiatedStructDecls_.push_back(std::move(spec));
    if (!ns.empty()) {
        moduleStructs_[ns][mangled] = std::move(structs_[mangled]);
        structs_.erase(mangled);
    }
    currentNamespace_ = oldNs;
}

void SemanticAnalyzer::instantiateFunc(const std::string& name, const std::string& ns, const std::vector<Type>& args) {
    if (args.empty()) return;
    std::string mangled = mangleGenericName(name, args);
    if (getFunc(mangled, ns)) return;

    const FuncDecl* tmpl = nullptr;
    if (ns.empty()) {
        if (funcTemplates_.count(name)) tmpl = funcTemplates_[name];
    } else {
        if (moduleFuncTemplates_.count(ns) && moduleFuncTemplates_[ns].count(name))
            tmpl = moduleFuncTemplates_[ns][name];
    }
    if (!tmpl) return;

    std::unordered_map<std::string, Type> subs;
    for (size_t i = 0; i < tmpl->typeParams.size() && i < args.size(); i++)
        subs[tmpl->typeParams[i]] = args[i];

    auto spec = std::make_unique<FuncDecl>();
    spec->name = mangled;
    spec->loc = tmpl->loc;
    spec->returnType = substitute(tmpl->returnType, subs);
    for (const auto& p : tmpl->params) {
        FuncParam fp = p;
        fp.type = substitute(p.type, subs);
        spec->params.push_back(std::move(fp));
    }
    spec->body = substituteStmt(tmpl->body.get(), subs);

    auto oldNs = currentNamespace_;
    currentNamespace_ = ns;
    analyzeFunc(*spec);
    instantiatedFuncDecls_.push_back(std::move(spec));
    if (!ns.empty()) {
        moduleFunctions_[ns][mangled] = std::move(functions_[mangled]);
        functions_.erase(mangled);
    }
    currentNamespace_ = oldNs;
}

void SemanticAnalyzer::error(const std::string& msg, SourceLoc loc) {
    errors_.push_back(SourceManager::instance().formatError(loc, msg));
}

StructDef* SemanticAnalyzer::getStruct(const std::string& name, const std::string& ns) {
    if (ns.empty()) {
        auto i = structs_.find(name);
        return i == structs_.end() ? nullptr : &i->second;
    }
    auto mi = moduleStructs_.find(ns);
    if (mi == moduleStructs_.end()) return nullptr;
    auto i = mi->second.find(name);
    return i == mi->second.end() ? nullptr : &i->second;
}

FuncSymbol* SemanticAnalyzer::getFunc(const std::string& name, const std::string& ns) {
    if (ns.empty()) {
        auto i = functions_.find(name);
        return i == functions_.end() ? nullptr : &i->second;
    }
    auto mi = moduleFunctions_.find(ns);
    if (mi == moduleFunctions_.end()) return nullptr;
    auto i = mi->second.find(name);
    return i == mi->second.end() ? nullptr : &i->second;
}

Type SemanticAnalyzer::resolveType(const Type& t) {
    if (t.kind == Type::Kind::Pointer) {
        Type r = t;
        r.ptrTo = std::make_unique<Type>(resolveType(*t.ptrTo));
        return r;
    }
    if (t.kind != Type::Kind::StructRef) return t;

    if (!t.typeArgs.empty()) {
        std::vector<Type> resolvedArgs;
        for (const auto& arg : t.typeArgs) resolvedArgs.push_back(resolveType(arg));

        std::string targetNs = t.ns;
        if (targetNs.empty() && !currentNamespace_.empty()) {
            // Check if template exists in current namespace
            if (moduleStructTemplates_.count(currentNamespace_) && moduleStructTemplates_[currentNamespace_].count(t.structName))
                targetNs = currentNamespace_;
        }

        instantiateStruct(t.structName, targetNs, resolvedArgs);
        Type r = t;
        r.ns = targetNs;
        r.structName = mangleGenericName(t.structName, resolvedArgs);
        r.typeArgs.clear();
        return resolveType(r);
    }

    StructDef* sd = getStruct(t.structName, t.ns);
    if (!sd && t.ns.empty() && !currentNamespace_.empty()) {
        sd = getStruct(t.structName, currentNamespace_);
        if (sd) {
            Type r = t;
            r.ns = currentNamespace_;
            return r;
        }
    }
    if (!sd) return t;
    Type r = t;
    r.kind = Type::Kind::StructRef;
    r.structName = t.structName;
    r.ns = t.ns;
    return r;
}

void SemanticAnalyzer::analyzeStruct(const StructDecl& s) {
    StructDef def;
    def.name = s.name;
    def.mangledName = currentNamespace_.empty() ? s.name : currentNamespace_ + "_" + s.name;
    size_t offset = 0;
    for (size_t i = 0; i < s.members.size(); i++) {
        const auto& m = s.members[i];
        Type ty = resolveType(m.type);
        def.members.push_back({m.name, ty});
        def.memberIndex[m.name] = i;
        if (ty.kind == Type::Kind::Int || ty.kind == Type::Kind::Float || ty.kind == Type::Kind::Bool)
            offset += 8;
        else if (ty.kind == Type::Kind::StructRef) {
            StructDef* sd = getStruct(ty.structName, ty.ns);
            offset += sd ? sd->sizeBytes : 8;
        }
    }
    def.sizeBytes = (offset + 7) & ~7;
    structs_[s.name] = std::move(def);
}

void SemanticAnalyzer::analyzeFunc(const FuncDecl& f) {
    FuncSymbol sym;
    sym.name = f.name;
    sym.ns = currentNamespace_;
    if (f.isExtern) sym.mangledName = f.name;
    else sym.mangledName = currentNamespace_.empty() ? f.name : currentNamespace_ + "_" + f.name;
    sym.returnType = resolveType(f.returnType);
    sym.decl = &f;
    for (const auto& p : f.params)
        sym.paramTypes.push_back(resolveType(p.type));

    std::string key = f.name; // Use a unique key if possible
    functions_[key] = std::move(sym);
    FuncSymbol& fs = functions_[key];

    auto oldFunc = currentFunc_;
    auto oldFuncSym = currentFuncSymbol_;
    auto oldOffset = nextFrameOffset_;

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

    currentFunc_ = oldFunc;
    currentFuncSymbol_ = oldFuncSym;
    nextFrameOffset_ = oldOffset;
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
        case Expr::Kind::StringLit:
            expr->exprType.kind = Type::Kind::String;
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
                if (l.kind == Type::Kind::String && expr->op == "+") {
                    expr->exprType.kind = Type::Kind::String;
                    return expr->exprType;
                }
                if (l.kind == Type::Kind::Pointer && (expr->op == "+" || expr->op == "-")) {
                    expr->exprType = l;
                    return expr->exprType;
                }
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
            std::string targetNs = expr->ns;
            if (targetNs.empty() && !currentNamespace_.empty()) {
                if (moduleFuncTemplates_.count(currentNamespace_) && moduleFuncTemplates_[currentNamespace_].count(expr->ident))
                    targetNs = currentNamespace_;
                else if (functions_.count(expr->ident))
                    targetNs = "";
            }

            if (!expr->exprType.typeArgs.empty()) {
                std::vector<Type> resolvedArgs;
                for (const auto& arg : expr->exprType.typeArgs) resolvedArgs.push_back(resolveType(arg));
                instantiateFunc(expr->ident, targetNs, resolvedArgs);
                expr->ident = mangleGenericName(expr->ident, resolvedArgs);
                expr->ns = targetNs;
                expr->exprType.typeArgs.clear();
            }

            if (expr->ns.empty() && (expr->ident == "print" || expr->ident == "println")) {
                for (size_t i = 0; i < expr->args.size(); i++) {
                    analyzeExpr(expr->args[i].get());
                }
                expr->exprType.kind = Type::Kind::Int;
                return expr->exprType;
            }
            FuncSymbol* fs = getFunc(expr->ident, expr->ns);
            if (!fs && expr->ns.empty() && !currentNamespace_.empty()) {
                fs = getFunc(expr->ident, currentNamespace_);
                if (fs) expr->ns = currentNamespace_;
            }

            if (!fs) {
                error("undefined function '" + expr->ident + "' (ns=" + expr->ns + ")", expr->loc);
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
            // Check if it's a module access
            if (expr->left->kind == Expr::Kind::Var) {
                auto it = modules_.find(expr->left->ident);
                if (it != modules_.end()) {
                    // It's a module access!
                    // Check if it's a struct in the module
                    // For now, we only support namespaced types and calls.
                    // If it's a member access and not a call, it might be a constant or global var.
                }
            }

            Type base = analyzeExpr(expr->left.get());
            if (base.kind == Type::Kind::Pointer) {
                // Auto-dereference for pointer to struct
                base = *base.ptrTo;
            }
            if (base.kind != Type::Kind::StructRef) {
                error("member access on non-struct type", expr->loc);
                expr->exprType.kind = Type::Kind::Int;
                return expr->exprType;
            }
            StructDef* sd = getStruct(base.structName, base.ns);
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
        case Expr::Kind::Deref: {
            Type base = analyzeExpr(expr->right.get());
            if (base.kind != Type::Kind::Pointer) {
                error("dereferencing non-pointer type", expr->loc);
                expr->exprType.kind = Type::Kind::Int;
                return expr->exprType;
            }
            expr->exprType = *base.ptrTo;
            return expr->exprType;
        }
        case Expr::Kind::AddressOf: {
            Type base = analyzeExpr(expr->right.get());
            expr->exprType.kind = Type::Kind::Pointer;
            expr->exprType.ptrTo = std::make_unique<Type>(base);
            return expr->exprType;
        }
        case Expr::Kind::New: {
            if (expr->left) analyzeExpr(expr->left.get());
            expr->exprType.kind = Type::Kind::Pointer;
            expr->exprType.ptrTo = std::make_unique<Type>(resolveType(*expr->targetType));
            return expr->exprType;
        }
        case Expr::Kind::Delete: {
            analyzeExpr(expr->right.get());
            expr->exprType.kind = Type::Kind::Void;
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
        case Stmt::Kind::Unsafe:
            analyzeStmt(stmt->body.get());
            break;
        case Stmt::Kind::Asm:
            break;
    }
}

void SemanticAnalyzer::analyzeProgram() {
    for (const auto& s : program_->structs) {
        if (s.typeParams.empty()) analyzeStruct(s);
        else structTemplates_[s.name] = &s;
    }
    // Register builtins so they are known during analysis
    FuncSymbol printlnSym;
    printlnSym.name = "println";
    printlnSym.mangledName = "println";
    printlnSym.returnType.kind = Type::Kind::Int;
    printlnSym.paramTypes.push_back(Type{});
    printlnSym.paramTypes.back().kind = Type::Kind::Int;
    functions_["println"] = std::move(printlnSym);
    FuncSymbol printSym;
    printSym.name = "print";
    printSym.mangledName = "print";
    printSym.returnType.kind = Type::Kind::Int;
    printSym.paramTypes.push_back(Type{});
    printSym.paramTypes.back().kind = Type::Kind::Int;
    functions_["print"] = std::move(printSym);
    FuncSymbol pfSym;
    pfSym.name = "print_float";
    pfSym.mangledName = "print_float";
    pfSym.returnType.kind = Type::Kind::Int;
    pfSym.paramTypes.push_back(Type{});
    pfSym.paramTypes.back().kind = Type::Kind::Float;
    functions_["print_float"] = std::move(pfSym);
    FuncSymbol pflSym;
    pflSym.name = "println_float";
    pflSym.mangledName = "println_float";
    pflSym.returnType.kind = Type::Kind::Int;
    pflSym.paramTypes.push_back(Type{});
    pflSym.paramTypes.back().kind = Type::Kind::Float;
    functions_["println_float"] = std::move(pflSym);
    FuncSymbol psSym;
    psSym.name = "print_string";
    psSym.mangledName = "print_string";
    psSym.returnType.kind = Type::Kind::Int;
    psSym.paramTypes.push_back(Type{Type::Kind::String});
    functions_["print_string"] = std::move(psSym);
    FuncSymbol plsSym;
    plsSym.name = "println_string";
    plsSym.mangledName = "println_string";
    plsSym.returnType.kind = Type::Kind::Int;
    plsSym.paramTypes.push_back(Type{Type::Kind::String});
    functions_["println_string"] = std::move(plsSym);
    for (const auto& f : program_->functions) {
        if (f.typeParams.empty()) analyzeFunc(f);
        else funcTemplates_[f.name] = &f;
    }
}

bool SemanticAnalyzer::analyze() {
    analyzeProgram();
    return errors_.empty();
}

} // namespace gspp
