#include "semantic.h"

namespace gspp {

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
            Type t = analyzeExpr(expr->left.get());
            analyzeExpr(expr->right.get());
            expr->exprType = t;
            return t;
        }
        case Expr::Kind::Unary: {
            Type t = analyzeExpr(expr->right.get());
            expr->exprType = t;
            return t;
        }
        case Expr::Kind::Spawn: {
            if (expr->left->kind != Expr::Kind::Call) {
                error("spawn requires a function call", expr->loc);
                return Type{Type::Kind::Thread};
            }
            analyzeExpr(expr->left.get());
            expr->exprType.kind = Type::Kind::Thread;
            return expr->exprType;
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
                    if (receiverType.kind == Type::Kind::StructRef || (receiverType.kind == Type::Kind::Pointer && receiverType.ptrTo && (receiverType.ptrTo->kind == Type::Kind::StructRef || receiverType.ptrTo->kind == Type::Kind::Int))) {
                        Type base = (receiverType.kind == Type::Kind::Pointer) ? *receiverType.ptrTo : receiverType;
                        if (base.kind == Type::Kind::Int && !base.structName.empty()) base.kind = Type::Kind::StructRef;
                        StructDef* sd = getStruct(base.structName, base.ns);
                        FuncSymbol* ms = getMethod(sd, expr->ident);
                        if (ms) {
                            expr->exprType = ms->returnType;
                            for (auto& a : expr->args) analyzeExpr(a.get());
                            return ms->returnType;
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
                    expr->exprType.ptrTo->kind = Type::Kind::StructRef;
                    FuncSymbol* initSym = getMethod(sd, "init");
                    if (initSym) {
                        for (size_t i = 0; i < expr->args.size() && i < initSym->paramTypes.size() - 1; i++) {
                            Type argTy = analyzeExpr(expr->args[i].get());
                            if (initSym->paramTypes[i+1].kind == Type::Kind::Int && initSym->decl && i < initSym->decl->params.size()) {
                                initSym->paramTypes[i+1] = argTy;
                                std::string paramName = initSym->decl->params[i].name;
                                if (initSym->locals.count(paramName)) initSym->locals[paramName].type = argTy;
                            }
                        }
                    } else for (auto& a : expr->args) analyzeExpr(a.get());
                    return expr->exprType;
                }
            }
            if (expr->ns.empty() && (expr->ident == "print" || expr->ident == "println" || expr->ident == "log")) {
                for (size_t i = 0; i < expr->args.size(); i++) analyzeExpr(expr->args[i].get());
                expr->exprType.kind = Type::Kind::Int;
                return expr->exprType;
            }
            if (!expr->typeArgs.empty()) {
                instantiateFunc(expr->ident, expr->ns, expr->typeArgs);
                expr->ident = mangleGenericName(expr->ident, expr->typeArgs);
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
            if (base.kind == Type::Kind::Int && !base.structName.empty()) base.kind = Type::Kind::StructRef;
            if (base.kind != Type::Kind::StructRef) {
                error("member access on non-struct (kind=" + std::to_string((int)base.kind) + " name=" + base.structName + ")", expr->loc);
                return Type{Type::Kind::Int};
            }
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
            expr->exprType.isMutable = expr->boolVal;
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
            if (listTy.kind == Type::Kind::List && listTy.ptrTo) elemTy = *listTy.ptrTo;
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
            Type t = resolveType(*expr->targetType);
            *expr->targetType = t;
            expr->exprType.kind = Type::Kind::Pointer;
            expr->exprType.ptrTo = std::make_unique<Type>(t);
            for (auto& a : expr->args) analyzeExpr(a.get());
            return expr->exprType;
        }
        case Expr::Kind::Delete: {
            analyzeExpr(expr->right.get());
            expr->exprType.kind = Type::Kind::Void;
            return expr->exprType;
        }
        case Expr::Kind::Cast: {
            analyzeExpr(expr->left.get());
            expr->exprType = resolveType(*expr->targetType);
            return expr->exprType;
        }
        case Expr::Kind::Sizeof: {
            expr->exprType.kind = Type::Kind::Int;
            return expr->exprType;
        }
        case Expr::Kind::Ternary: {
            analyzeExpr(expr->cond.get());
            Type t = analyzeExpr(expr->left.get());
            analyzeExpr(expr->right.get());
            expr->exprType = t;
            return t;
        }
        case Expr::Kind::Receive: {
            Type t = analyzeExpr(expr->right.get());
            if (t.kind != Type::Kind::Chan) {
                error("receive from non-channel type", expr->loc);
                return Type{Type::Kind::Int};
            }
            if (t.typeArgs.empty()) expr->exprType.kind = Type::Kind::Int;
            else expr->exprType = t.typeArgs[0];
            return expr->exprType;
        }
        case Expr::Kind::ChanInit: {
            if (expr->targetType) *expr->targetType = resolveType(*expr->targetType);
            for (auto& a : expr->args) analyzeExpr(a.get());
            expr->exprType.kind = Type::Kind::Chan;
            if (expr->targetType) expr->exprType.typeArgs.push_back(*expr->targetType);
            return expr->exprType;
        }
        case Expr::Kind::Super: {
            if (!currentStruct_ || currentStruct_->baseName.empty()) {
                error("super used outside of derived class", expr->loc);
                return Type{Type::Kind::Int};
            }
            expr->exprType.kind = Type::Kind::Pointer;
            expr->exprType.ptrTo = std::make_unique<Type>(Type::Kind::StructRef);
            expr->exprType.ptrTo->structName = currentStruct_->baseName;
            expr->exprType.ptrTo->kind = Type::Kind::StructRef;
            return expr->exprType;
        }
    }
    return Type{};
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
    for (const auto& ta : e->typeArgs) res->typeArgs.push_back(substitute(ta, subs));
    if (e->targetType) res->targetType = std::make_unique<Type>(substitute(*e->targetType, subs));
    if (e->left) res->left = substituteExpr(e->left.get(), subs);
    if (e->right) res->right = substituteExpr(e->right.get(), subs);
    if (e->cond) res->cond = substituteExpr(e->cond.get(), subs);
    for (const auto& arg : e->args) res->args.push_back(substituteExpr(arg.get(), subs));
    return res;
}

} // namespace gspp
