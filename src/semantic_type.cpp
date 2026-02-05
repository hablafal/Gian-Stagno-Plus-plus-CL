#include "semantic.h"

namespace gspp {

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
        case Type::Kind::Mutex: return "mutex";
        case Type::Kind::Thread: return "thread";
        case Type::Kind::Chan: return "chan_" + (t.typeArgs.empty() ? "any" : typeName(t.typeArgs[0]));
        case Type::Kind::StructRef: {
            std::string n = t.ns.empty() ? t.structName : t.ns + "_" + t.structName;
            for (const auto& arg : t.typeArgs) n += "_" + typeName(arg);
            return n;
        }
    }
    return "unknown";
}

Type SemanticAnalyzer::resolveType(const Type& t) {
    if (t.kind == Type::Kind::Pointer) {
        Type r = t;
        r.ptrTo = std::make_unique<Type>(resolveType(*t.ptrTo));
        return r;
    }
    if (t.kind == Type::Kind::Chan) {
        Type r = t;
        for (auto& arg : r.typeArgs) arg = resolveType(arg);
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

} // namespace gspp
