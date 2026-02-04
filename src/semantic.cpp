#include "semantic.h"
#include <sstream>
#include <iostream>

namespace gspp {

SemanticAnalyzer::SemanticAnalyzer(Program* program) : program_(program) {}

void SemanticAnalyzer::addModule(const std::string& name, Program* prog) {
    modules_[name] = prog;
    auto oldStructs = std::move(structs_);
    auto oldFunctions = std::move(functions_);
    auto oldNs = currentNamespace_;
    structs_.clear();
    functions_.clear();
    currentNamespace_ = name;
    for (const auto& s : prog->structs) {
        if (s.typeParams.empty()) analyzeStruct(s);
        else moduleStructTemplates_[name][s.name] = &s;
    }
    for (const auto& f : prog->functions) {
        if (f.typeParams.empty()) analyzeFunc(f);
        else moduleFuncTemplates_[name][f.name] = &f;
    }
    for (const auto& s : prog->structs) {
        for (const auto& m : s.methods) analyzeMethod(s.name, m);
    }
    moduleStructs_[name] = std::move(structs_);
    moduleFunctions_[name] = std::move(functions_);
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
        nextFrameOffset_ += 8;
        sym.frameOffset = -nextFrameOffset_;
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
        case Type::Kind::List: return "list_" + typeName(*t.ptrTo);
        case Type::Kind::TypeParam: return t.structName;
        case Type::Kind::Pointer: return "ptr_" + typeName(*t.ptrTo);
        case Type::Kind::Dict: return "dict_" + typeName(*t.ptrTo) + "_" + typeName(*t.ptrTo->ptrTo);
        case Type::Kind::Tuple: return "tuple";
        case Type::Kind::Set: return "set";
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
    if (e->cond) res->cond = substituteExpr(e->cond.get(), subs);
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
    res->isInclusive = s->isInclusive;
    if (s->varInit) res->varInit = substituteExpr(s->varInit.get(), subs);
    if (s->assignTarget) res->assignTarget = substituteExpr(s->assignTarget.get(), subs);
    if (s->assignValue) res->assignValue = substituteExpr(s->assignValue.get(), subs);
    if (s->condition) res->condition = substituteExpr(s->condition.get(), subs);
    if (s->thenBranch) res->thenBranch = substituteStmt(s->thenBranch.get(), subs);
    if (s->elseBranch) res->elseBranch = substituteStmt(s->elseBranch.get(), subs);
    if (s->body) res->body = substituteStmt(s->body.get(), subs);
    if (s->initStmt) res->initStmt = substituteStmt(s->initStmt.get(), subs);
    if (s->stepStmt) res->stepStmt = substituteStmt(s->stepStmt.get(), subs);
    if (s->startExpr) res->startExpr = substituteExpr(s->startExpr.get(), subs);
    if (s->endExpr) res->endExpr = substituteExpr(s->endExpr.get(), subs);
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
    if (ns.empty() || ns == currentNamespace_) {
        auto i = structs_.find(name);
        if (i != structs_.end()) return &i->second;
    }
    if (ns.empty()) return nullptr;
    auto mi = moduleStructs_.find(ns);
    if (mi == moduleStructs_.end()) return nullptr;
    auto i = mi->second.find(name);
    return i == mi->second.end() ? nullptr : &i->second;
}

FuncSymbol* SemanticAnalyzer::getFunc(const std::string& name, const std::string& ns) {
    if (ns.empty() || ns == currentNamespace_) {
        auto i = functions_.find(name);
        if (i != functions_.end()) return &i->second;
    }
    if (ns.empty()) return nullptr;
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
        if (sd) { Type r = t; r.ns = currentNamespace_; return r; }
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
        offset += 8;
    }
    def.sizeBytes = (offset + 7) & ~7;
    if (def.sizeBytes < 64) def.sizeBytes = 64;
    for (const auto& m : s.methods) {
        FuncSymbol msym;
        msym.name = m.name;
        msym.ns = currentNamespace_;
        msym.mangledName = def.mangledName + "_" + m.name;
        msym.returnType = resolveType(m.returnType);
        msym.decl = &m;
        msym.isMethod = true;
        Type selfTy{Type::Kind::Pointer};
        selfTy.ptrTo = std::make_unique<Type>(Type::Kind::StructRef);
        selfTy.ptrTo->structName = s.name;
        selfTy.ptrTo->ns = currentNamespace_;
        msym.paramTypes.push_back(selfTy);
        for (const auto& p : m.params) msym.paramTypes.push_back(resolveType(p.type));
        def.methods[m.name] = std::move(msym);
    }
    structs_[s.name] = std::move(def);
}

void SemanticAnalyzer::analyzeMethod(const std::string& structName, const FuncDecl& f) {
    StructDef* sd = getStruct(structName, currentNamespace_);
    if (!sd) return;
    FuncSymbol& sym = sd->methods[f.name];
    auto oldFunc = currentFunc_;
    auto oldFuncSym = currentFuncSymbol_;
    auto oldOffset = nextFrameOffset_;
    pushScope();
    currentFunc_ = const_cast<FuncDecl*>(&f);
    currentFuncSymbol_ = &sym;
    nextFrameOffset_ = 0;
    addVar("self", sym.paramTypes[0], true);
    sym.locals["self"] = *lookupVar("self");
    for (size_t i = 0; i < f.params.size(); i++) {
        addVar(f.params[i].name, sym.paramTypes[i+1], true);
        sym.locals[f.params[i].name] = *lookupVar(f.params[i].name);
    }
    if (f.body) analyzeStmt(f.body.get());
    popScope();
    currentFunc_ = oldFunc;
    currentFuncSymbol_ = oldFuncSym;
    nextFrameOffset_ = oldOffset;
}

void SemanticAnalyzer::analyzeFunc(const FuncDecl& f) {
    FuncSymbol sym;
    sym.name = f.name;
    sym.ns = currentNamespace_;
    sym.isExtern = f.isExtern;
    if (f.isExtern) sym.mangledName = f.name;
    else sym.mangledName = currentNamespace_.empty() ? f.name : currentNamespace_ + "_" + f.name;
    sym.returnType = resolveType(f.returnType);
    sym.decl = &f;
    for (const auto& p : f.params) sym.paramTypes.push_back(resolveType(p.type));
    functions_[f.name] = std::move(sym);
    FuncSymbol& fs = functions_[f.name];
    auto oldFunc = currentFunc_;
    auto oldFuncSym = currentFuncSymbol_;
    auto oldOffset = nextFrameOffset_;
    pushScope();
    currentFunc_ = const_cast<FuncDecl*>(&f);
    currentFuncSymbol_ = &fs;
    nextFrameOffset_ = 0;
    for (size_t i = 0; i < f.params.size(); i++) {
        Type pt = resolveType(f.params[i].type);
        addVar(f.params[i].name, pt, true);
        if (auto vs = lookupVar(f.params[i].name)) fs.locals[f.params[i].name] = *vs;
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
                if (expr->ident == "File" || expr->ident == "math" || expr->ident == "os") {
                    expr->exprType.kind = Type::Kind::Void;
                    return expr->exprType;
                }
                error("undefined variable '" + expr->ident + "'", expr->loc);
                return Type{Type::Kind::Int};
            }
            expr->exprType = vs->type;
            return vs->type;
        }
        case Expr::Kind::Binary: {
            Type l = analyzeExpr(expr->left.get());
            Type r = analyzeExpr(expr->right.get());
            if (expr->op == "and" || expr->op == "or" || expr->op == "==" || expr->op == "!=" ||
                expr->op == "<" || expr->op == ">" || expr->op == "<=" || expr->op == ">=") {
                expr->exprType.kind = Type::Kind::Bool;
                return expr->exprType;
            }
            if ((l.kind == Type::Kind::Set || l.kind == Type::Kind::Dict) && (expr->op == "|" || expr->op == "&")) {
                expr->exprType = l;
                return l;
            }
            expr->exprType = l;
            return l;
        }
        case Expr::Kind::Unary: {
            Type o = analyzeExpr(expr->right.get());
            if (expr->op == "not") { expr->exprType.kind = Type::Kind::Bool; return expr->exprType; }
            expr->exprType = o;
            return o;
        }
        case Expr::Kind::Ternary: {
            analyzeExpr(expr->cond.get());
            Type t = analyzeExpr(expr->left.get());
            analyzeExpr(expr->right.get());
            expr->exprType = t;
            return t;
        }
        case Expr::Kind::Call: {
            if (expr->left && expr->ns.empty()) {
                if (expr->left->kind == Expr::Kind::Var) {
                    std::string possibleNs = expr->left->ident;
                    if (moduleFunctions_.count(possibleNs) || modules_.count(possibleNs) || possibleNs == "File" || possibleNs == "math" || possibleNs == "os") {
                        expr->ns = possibleNs;
                    }
                }
                if (expr->ns.empty()) {
                    Type receiverType = analyzeExpr(expr->left.get());
                    if (receiverType.kind == Type::Kind::String) {
                        if (expr->ident == "len") { expr->exprType.kind = Type::Kind::Int; return expr->exprType; }
                    }
                    if (receiverType.kind == Type::Kind::List) {
                        if (expr->ident == "append") { if (!expr->args.empty()) analyzeExpr(expr->args[0].get()); expr->exprType.kind = Type::Kind::Void; return expr->exprType; }
                        if (expr->ident == "len") { expr->exprType.kind = Type::Kind::Int; return expr->exprType; }
                    }
                    if (receiverType.kind == Type::Kind::Set) {
                        if (expr->ident == "len") { expr->exprType.kind = Type::Kind::Int; return expr->exprType; }
                    }
                    if (receiverType.kind == Type::Kind::Dict) {
                        if (expr->ident == "len") { expr->exprType.kind = Type::Kind::Int; return expr->exprType; }
                        if (expr->ident == "get") { for(auto& a: expr->args) analyzeExpr(a.get()); expr->exprType.kind = Type::Kind::Int; return expr->exprType; }
                        if (expr->ident == "pop") { for(auto& a: expr->args) analyzeExpr(a.get()); expr->exprType.kind = Type::Kind::Int; return expr->exprType; }
                        if (expr->ident == "remove") { for(auto& a: expr->args) analyzeExpr(a.get()); expr->exprType.kind = Type::Kind::Void; return expr->exprType; }
                        if (expr->ident == "clear") { expr->exprType.kind = Type::Kind::Void; return expr->exprType; }
                        if (expr->ident == "keys") { expr->exprType.kind = Type::Kind::List; return expr->exprType; }
                        if (expr->ident == "values") { expr->exprType.kind = Type::Kind::List; return expr->exprType; }
                    }
                    if (receiverType.kind == Type::Kind::StructRef || (receiverType.kind == Type::Kind::Pointer && receiverType.ptrTo && receiverType.ptrTo->kind == Type::Kind::StructRef)) {
                        Type& base = (receiverType.kind == Type::Kind::Pointer) ? *receiverType.ptrTo : receiverType;
                        StructDef* sd = getStruct(base.structName, base.ns);
                        if (sd && sd->methods.count(expr->ident)) {
                            FuncSymbol& ms = sd->methods[expr->ident];
                            expr->exprType = ms.returnType;
                            for (auto& a : expr->args) analyzeExpr(a.get());
                            return ms.returnType;
                        }
                    }
                }
            }
            if (expr->ns.empty()) {
                StructDef* sd = getStruct(expr->ident, "");
                if (sd) {
                    expr->exprType.kind = Type::Kind::Pointer;
                    expr->exprType.ptrTo = std::make_unique<Type>(Type::Kind::StructRef);
                    expr->exprType.ptrTo->structName = expr->ident;
                    if (sd->methods.count("init")) {
                        FuncSymbol& initSym = sd->methods["init"];
                        for (size_t i = 0; i < expr->args.size() && i < initSym.paramTypes.size() - 1; i++) {
                            Type argTy = analyzeExpr(expr->args[i].get());
                            if (initSym.paramTypes[i+1].kind == Type::Kind::Int && initSym.decl && i < initSym.decl->params.size()) {
                                initSym.paramTypes[i+1] = argTy;
                                std::string paramName = initSym.decl->params[i].name;
                                if (initSym.locals.count(paramName)) initSym.locals[paramName].type = argTy;
                            }
                        }
                    } else for (auto& a : expr->args) analyzeExpr(a.get());
                    return expr->exprType;
                }
            }
            std::string targetNs = expr->ns;
            if (expr->ns.empty() && (expr->ident == "print" || expr->ident == "println" || expr->ident == "log")) {
                for (size_t i = 0; i < expr->args.size(); i++) analyzeExpr(expr->args[i].get());
                expr->exprType.kind = Type::Kind::Int;
                return expr->exprType;
            }
            FuncSymbol* fs = getFunc(expr->ident, expr->ns);
            if (!fs) { error("undefined function '" + expr->ident + "'", expr->loc); return Type{Type::Kind::Int}; }
            for (auto& a : expr->args) analyzeExpr(a.get());
            expr->exprType = fs->returnType;
            return fs->returnType;
        }
        case Expr::Kind::Member: {
            Type base = analyzeExpr(expr->left.get());
            if (base.kind == Type::Kind::Pointer) base = *base.ptrTo;
            if (base.kind != Type::Kind::StructRef) { error("member access on non-struct", expr->loc); return Type{Type::Kind::Int}; }
            StructDef* sd = getStruct(base.structName, base.ns);
            if (!sd) { error("unknown struct '" + base.structName + "'", expr->loc); return Type{Type::Kind::Int}; }
            auto it = sd->memberIndex.find(expr->member);
            if (it == sd->memberIndex.end()) { error("no member '" + expr->member + "'", expr->loc); return Type{Type::Kind::Int}; }
            expr->exprType = sd->members[it->second].second;
            return expr->exprType;
        }
        case Expr::Kind::Index: {
            Type baseTy = analyzeExpr(expr->left.get());
            analyzeExpr(expr->right.get());
            if (baseTy.kind == Type::Kind::List || baseTy.kind == Type::Kind::Pointer) expr->exprType = *baseTy.ptrTo;
            else if (baseTy.kind == Type::Kind::String) expr->exprType.kind = Type::Kind::Char;
            else if (baseTy.kind == Type::Kind::Dict) expr->exprType = *baseTy.ptrTo->ptrTo;
            else if (baseTy.kind == Type::Kind::Tuple) expr->exprType.kind = Type::Kind::Int;
            return expr->exprType;
        }
        case Expr::Kind::Slice: {
            Type baseTy = analyzeExpr(expr->left.get());
            for (auto& a : expr->args) analyzeExpr(a.get());
            expr->exprType = baseTy;
            return expr->exprType;
        }
        case Expr::Kind::ListLit: {
            Type elemType{Type::Kind::Int};
            if (!expr->args.empty()) {
                elemType = analyzeExpr(expr->args[0].get());
                for (size_t i = 1; i < expr->args.size(); i++) analyzeExpr(expr->args[i].get());
            }
            expr->exprType.kind = Type::Kind::List;
            expr->exprType.ptrTo = std::make_unique<Type>(elemType);
            return expr->exprType;
        }
        case Expr::Kind::DictLit: {
            Type keyType{Type::Kind::String};
            Type valType{Type::Kind::Int};
            if (!expr->args.empty()) {
                keyType = analyzeExpr(expr->args[0].get());
                valType = analyzeExpr(expr->args[1].get());
                for (size_t i = 2; i < expr->args.size(); i += 2) {
                    analyzeExpr(expr->args[i].get());
                    analyzeExpr(expr->args[i+1].get());
                }
            }
            expr->exprType.kind = Type::Kind::Dict;
            expr->exprType.ptrTo = std::make_unique<Type>(keyType);
            expr->exprType.ptrTo->ptrTo = std::make_unique<Type>(valType);
            return expr->exprType;
        }
        case Expr::Kind::TupleLit: {
            expr->exprType.kind = Type::Kind::Tuple;
            expr->exprType.isMutable = expr->boolVal; // parser set this
            for (auto& a : expr->args) analyzeExpr(a.get());
            return expr->exprType;
        }
        case Expr::Kind::SetLit: {
            expr->exprType.kind = Type::Kind::Set;
            for (auto& a : expr->args) analyzeExpr(a.get());
            return expr->exprType;
        }
        case Expr::Kind::Comprehension: {
            pushScope();
            Type listTy = analyzeExpr(expr->right.get());
            Type elemTy{Type::Kind::Int};
            if (listTy.kind == Type::Kind::List) elemTy = *listTy.ptrTo;
            addVar(expr->ident, elemTy);
            if (expr->cond) analyzeExpr(expr->cond.get());
            Type resElemTy = analyzeExpr(expr->left.get());
            popScope();
            expr->exprType.kind = Type::Kind::List;
            expr->exprType.ptrTo = std::make_unique<Type>(resElemTy);
            return expr->exprType;
        }
        case Expr::Kind::AddressOf: {
            Type operandTy = analyzeExpr(expr->right.get());
            expr->exprType.kind = Type::Kind::Pointer;
            expr->exprType.ptrTo = std::make_unique<Type>(operandTy);
            return expr->exprType;
        }
        case Expr::Kind::Deref: {
            Type operandTy = analyzeExpr(expr->right.get());
            if (operandTy.kind != Type::Kind::Pointer) {
                error("dereferencing non-pointer type", expr->loc);
                return Type{Type::Kind::Int};
            }
            expr->exprType = *operandTy.ptrTo;
            return expr->exprType;
        }
        case Expr::Kind::New: {
            Type target = resolveType(*expr->targetType);
            if (expr->left) { // Array size
                analyzeExpr(expr->left.get());
            }
            expr->exprType.kind = Type::Kind::Pointer;
            expr->exprType.ptrTo = std::make_unique<Type>(target);
            return expr->exprType;
        }
        case Expr::Kind::Delete: {
            analyzeExpr(expr->right.get());
            expr->exprType.kind = Type::Kind::Void;
            return expr->exprType;
        }
        case Expr::Kind::Cast: {
            analyzeExpr(expr->right.get());
            expr->exprType = resolveType(*expr->targetType);
            return expr->exprType;
        }
        case Expr::Kind::Sizeof: {
            if (expr->targetType) {
                resolveType(*expr->targetType);
            } else {
                analyzeExpr(expr->right.get());
            }
            expr->exprType.kind = Type::Kind::Int;
            return expr->exprType;
        }
        default: return Type{Type::Kind::Int};
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
                if (ty.kind == Type::Kind::Int && stmt->varType.structName.empty()) ty = initTy;
                stmt->varType = ty;
            }
            addVar(stmt->varName, ty);
            break;
        }
        case Stmt::Kind::Assign: {
            if (stmt->assignTarget->kind == Expr::Kind::Member && stmt->assignTarget->left->kind == Expr::Kind::Var && stmt->assignTarget->left->ident == "self") {
                Type receiverTy = analyzeExpr(stmt->assignTarget->left.get());
                Type& baseTy = *receiverTy.ptrTo;
                StructDef* sd = getStruct(baseTy.structName, baseTy.ns);
                if (sd) {
                    Type valTy = analyzeExpr(stmt->assignValue.get());
                    if (sd->memberIndex.find(stmt->assignTarget->member) == sd->memberIndex.end()) {
                        sd->memberIndex[stmt->assignTarget->member] = sd->members.size();
                        sd->members.push_back({stmt->assignTarget->member, valTy});
                        sd->sizeBytes += 8;
                    } else sd->members[sd->memberIndex[stmt->assignTarget->member]].second = valTy;
                }
            }
            if (stmt->assignTarget->kind == Expr::Kind::Index) {
                Type baseTy = analyzeExpr(stmt->assignTarget->left.get());
                if (baseTy.kind == Type::Kind::Tuple && !baseTy.isMutable) {
                    error("cannot assign to immutable tuple", stmt->assignTarget->loc);
                }
            }
            if (stmt->assignTarget->kind == Expr::Kind::Deref) {
                analyzeExpr(stmt->assignTarget.get());
            }
            if (stmt->assignTarget->kind == Expr::Kind::Var) {
                VarSymbol* vs = lookupVar(stmt->assignTarget->ident);
                if (!vs) {
                    Type valTy = analyzeExpr(stmt->assignValue.get());
                    if (valTy.kind == Type::Kind::Void) valTy.kind = Type::Kind::Pointer, valTy.ptrTo = std::make_unique<Type>(Type::Kind::Int);
                    addVar(stmt->assignTarget->ident, valTy);
                    vs = lookupVar(stmt->assignTarget->ident);
                }
                stmt->assignTarget->exprType = vs->type;
                analyzeExpr(stmt->assignValue.get());
            } else { analyzeExpr(stmt->assignTarget.get()); analyzeExpr(stmt->assignValue.get()); }
            break;
        }
        case Stmt::Kind::If: analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->thenBranch.get()); if (stmt->elseBranch) analyzeStmt(stmt->elseBranch.get()); break;
        case Stmt::Kind::While: analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::For: pushScope(); analyzeStmt(stmt->initStmt.get()); analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->stepStmt.get()); analyzeStmt(stmt->body.get()); popScope(); break;
        case Stmt::Kind::Repeat: analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::RangeFor: pushScope(); analyzeExpr(stmt->startExpr.get()); analyzeExpr(stmt->endExpr.get()); addVar(stmt->varName, Type{Type::Kind::Int}); analyzeStmt(stmt->body.get()); popScope(); break;
        case Stmt::Kind::ForEach: {
            pushScope();
            Type listTy = analyzeExpr(stmt->expr.get());
            Type elemTy{Type::Kind::Int};
            if (listTy.kind == Type::Kind::List && listTy.ptrTo) elemTy = *listTy.ptrTo;
            addVar(stmt->varName, elemTy);
            analyzeStmt(stmt->body.get());
            popScope();
            break;
        }
        case Stmt::Kind::Switch: analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::Case: analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::Defer: analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::Return: if (stmt->returnExpr) analyzeExpr(stmt->returnExpr.get()); break;
        case Stmt::Kind::ExprStmt: analyzeExpr(stmt->expr.get()); break;
        case Stmt::Kind::Unsafe: analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::Asm: break;
    }
}

void SemanticAnalyzer::analyzeProgram() {
    for (const auto& imp : program_->imports) {
        if (!imp.importNames.empty()) {
            std::string ns = imp.alias.empty() ? imp.name : imp.alias;
            for (const auto& name : imp.importNames) {
                if (moduleFunctions_.count(ns) && moduleFunctions_[ns].count(name)) {
                    functions_[name] = moduleFunctions_[ns][name];
                } else if (moduleStructs_.count(ns) && moduleStructs_[ns].count(name)) {
                    structs_[name] = moduleStructs_[ns][name];
                } else {
                    error("name '" + name + "' not found in module '" + ns + "'", imp.loc);
                }
            }
        }
    }
    for (const auto& s : program_->structs) {
        if (s.typeParams.empty()) analyzeStruct(s);
        else structTemplates_[s.name] = &s;
    }
    for (const auto& s : program_->structs) {
        StructDef* sd = getStruct(s.name, currentNamespace_);
        for (const auto& m : s.methods) if (m.body) for (const auto& stmt : m.body->blockStmts) if (stmt->kind == Stmt::Kind::Assign && stmt->assignTarget->kind == Expr::Kind::Member) if (stmt->assignTarget->left->kind == Expr::Kind::Var && stmt->assignTarget->left->ident == "self") if (sd->memberIndex.find(stmt->assignTarget->member) == sd->memberIndex.end()) { sd->memberIndex[stmt->assignTarget->member] = sd->members.size(); sd->members.push_back({stmt->assignTarget->member, Type{Type::Kind::Int}}); sd->sizeBytes += 8; }
    }
    FuncSymbol printlnSym; printlnSym.name = "println"; printlnSym.mangledName = "println"; printlnSym.returnType.kind = Type::Kind::Int; printlnSym.paramTypes.push_back(Type{Type::Kind::Int}); printlnSym.isExtern = true; functions_["println"] = std::move(printlnSym);
    FuncSymbol printSym; printSym.name = "print"; printSym.mangledName = "print"; printSym.returnType.kind = Type::Kind::Int; printSym.paramTypes.push_back(Type{Type::Kind::Int}); printSym.isExtern = true; functions_["print"] = std::move(printSym);
    FuncSymbol logSym; logSym.name = "log"; logSym.mangledName = "println"; logSym.returnType.kind = Type::Kind::Int; logSym.paramTypes.push_back(Type{Type::Kind::Int}); logSym.isExtern = true; functions_["log"] = std::move(logSym);
    FuncSymbol pfSym; pfSym.name = "print_float"; pfSym.mangledName = "print_float"; pfSym.returnType.kind = Type::Kind::Int; pfSym.paramTypes.push_back(Type{Type::Kind::Float}); pfSym.isExtern = true; functions_["print_float"] = std::move(pfSym);
    FuncSymbol plfSym; plfSym.name = "println_float"; plfSym.mangledName = "println_float"; plfSym.returnType.kind = Type::Kind::Int; plfSym.paramTypes.push_back(Type{Type::Kind::Float}); plfSym.isExtern = true; functions_["println_float"] = std::move(plfSym);
    FuncSymbol psSym; psSym.name = "print_string"; psSym.mangledName = "print_string"; psSym.returnType.kind = Type::Kind::Int; psSym.paramTypes.push_back(Type{Type::Kind::String}); psSym.isExtern = true; functions_["print_string"] = std::move(psSym);
    FuncSymbol plsSym; plsSym.name = "println_string"; plsSym.mangledName = "println_string"; plsSym.returnType.kind = Type::Kind::Int; plsSym.paramTypes.push_back(Type{Type::Kind::String}); plsSym.isExtern = true; functions_["println_string"] = std::move(plsSym);
    FuncSymbol inputSym; inputSym.name = "input"; inputSym.mangledName = "gspp_input"; inputSym.returnType.kind = Type::Kind::String; functions_["input"] = std::move(inputSym);
    FuncSymbol rfSym; rfSym.name = "read_file"; rfSym.mangledName = "gspp_read_file"; rfSym.returnType.kind = Type::Kind::String; rfSym.paramTypes.push_back(Type{Type::Kind::String}); rfSym.isExtern = true; functions_["read_file"] = std::move(rfSym);
    FuncSymbol wfSym; wfSym.name = "write_file"; wfSym.mangledName = "gspp_write_file"; wfSym.returnType.kind = Type::Kind::Void; wfSym.paramTypes.push_back(Type{Type::Kind::String}); wfSym.paramTypes.push_back(Type{Type::Kind::String}); wfSym.isExtern = true; functions_["write_file"] = std::move(wfSym);
    FuncSymbol execSym; execSym.name = "exec"; execSym.mangledName = "gspp_exec"; execSym.returnType.kind = Type::Kind::Int; execSym.paramTypes.push_back(Type{Type::Kind::String}); execSym.isExtern = true; functions_["exec"] = std::move(execSym);
    FuncSymbol frSym; frSym.name = "read"; frSym.mangledName = "gspp_read_file"; frSym.returnType.kind = Type::Kind::String; frSym.paramTypes.push_back(Type{Type::Kind::String}); frSym.isExtern = true; moduleFunctions_["File"]["read"] = std::move(frSym);
    FuncSymbol fwSym; fwSym.name = "write"; fwSym.mangledName = "gspp_write_file"; fwSym.returnType.kind = Type::Kind::Void; fwSym.paramTypes.push_back(Type{Type::Kind::String}); fwSym.paramTypes.push_back(Type{Type::Kind::String}); fwSym.isExtern = true; moduleFunctions_["File"]["write"] = std::move(fwSym);
    FuncSymbol absSym; absSym.name = "abs"; absSym.mangledName = "abs"; absSym.returnType.kind = Type::Kind::Int; absSym.paramTypes.push_back(Type{Type::Kind::Int}); absSym.isExtern = true; functions_["abs"] = std::move(absSym);
    FuncSymbol sqrtSym; sqrtSym.name = "sqrt"; sqrtSym.mangledName = "sqrt"; sqrtSym.returnType.kind = Type::Kind::Float; sqrtSym.paramTypes.push_back(Type{Type::Kind::Float}); sqrtSym.isExtern = true; functions_["sqrt"] = std::move(sqrtSym);

    // os module
    FuncSymbol exitSym; exitSym.name = "exit"; exitSym.mangledName = "exit"; exitSym.returnType.kind = Type::Kind::Void; exitSym.paramTypes.push_back(Type{Type::Kind::Int}); exitSym.isExtern = true; moduleFunctions_["os"]["exit"] = std::move(exitSym);
    FuncSymbol sleepSym; sleepSym.name = "sleep"; sleepSym.mangledName = "usleep"; sleepSym.returnType.kind = Type::Kind::Void; sleepSym.paramTypes.push_back(Type{Type::Kind::Int}); sleepSym.isExtern = true; moduleFunctions_["os"]["sleep"] = std::move(sleepSym);

    // math module
    FuncSymbol sinSym; sinSym.name = "sin"; sinSym.mangledName = "sin"; sinSym.returnType.kind = Type::Kind::Float; sinSym.paramTypes.push_back(Type{Type::Kind::Float}); sinSym.isExtern = true; moduleFunctions_["math"]["sin"] = std::move(sinSym);
    FuncSymbol cosSym; cosSym.name = "cos"; cosSym.mangledName = "cos"; cosSym.returnType.kind = Type::Kind::Float; cosSym.paramTypes.push_back(Type{Type::Kind::Float}); cosSym.isExtern = true; moduleFunctions_["math"]["cos"] = std::move(cosSym);
    FuncSymbol tanSym; tanSym.name = "tan"; tanSym.mangledName = "tan"; tanSym.returnType.kind = Type::Kind::Float; tanSym.paramTypes.push_back(Type{Type::Kind::Float}); tanSym.isExtern = true; moduleFunctions_["math"]["tan"] = std::move(tanSym);
    FuncSymbol powSym; powSym.name = "pow"; powSym.mangledName = "pow"; powSym.returnType.kind = Type::Kind::Float; powSym.paramTypes.push_back(Type{Type::Kind::Float}); powSym.paramTypes.push_back(Type{Type::Kind::Float}); powSym.isExtern = true; moduleFunctions_["math"]["pow"] = std::move(powSym);

    for (const auto& f : program_->functions) { if (f.typeParams.empty()) analyzeFunc(f); else funcTemplates_[f.name] = &f; }
    if (!program_->topLevelStmts.empty()) {
        auto syntheticMain = std::make_unique<FuncDecl>(); syntheticMain->name = "main"; syntheticMain->returnType.kind = Type::Kind::Int;
        auto body = std::make_unique<Stmt>(); body->kind = Stmt::Kind::Block; for (auto& s : program_->topLevelStmts) body->blockStmts.push_back(std::move(s));
        syntheticMain->body = std::move(body); analyzeFunc(*syntheticMain); instantiatedFuncDecls_.push_back(std::move(syntheticMain));
    }
    for (const auto& s : program_->structs) {
        for (const auto& m : s.methods) if (m.name == "init") analyzeMethod(s.name, m);
        for (const auto& m : s.methods) if (m.name != "init") analyzeMethod(s.name, m);
    }
}

bool SemanticAnalyzer::analyze() { analyzeProgram(); return errors_.empty(); }

} // namespace gspp
