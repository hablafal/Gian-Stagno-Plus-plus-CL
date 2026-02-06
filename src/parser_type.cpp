#include "parser.h"

namespace gspp {

std::unique_ptr<Type> Parser::parseType() {
    SourceLoc l = loc();
    if (match(TokenKind::Star) || match(TokenKind::Ptr)) {
        auto ty = std::make_unique<Type>();
        ty->kind = Type::Kind::Pointer;
        if (match(TokenKind::LBracket)) {
            ty->ptrTo = parseType();
            expect(TokenKind::RBracket, "expected ']' after ptr type");
        } else {
            ty->ptrTo = parseType();
        }
        ty->loc = l;
        return ty;
    }
    auto ty = std::make_unique<Type>();
    ty->loc = l;
    if (match(TokenKind::Int)) { ty->kind = Type::Kind::Int; return ty; }
    if (match(TokenKind::Float) || match(TokenKind::Decimal)) { ty->kind = Type::Kind::Float; return ty; }
    if (match(TokenKind::Bool)) { ty->kind = Type::Kind::Bool; return ty; }
    if (match(TokenKind::String) || match(TokenKind::Text)) { ty->kind = Type::Kind::String; return ty; }
    if (match(TokenKind::Arr)) {
        ty->kind = Type::Kind::List;
        if (match(TokenKind::LBracket)) {
            ty->ptrTo = parseType();
            expect(TokenKind::RBracket, "expected ']' after Arr type");
        }
        return ty;
    }
    if (match(TokenKind::Char)) { ty->kind = Type::Kind::Char; return ty; }
    if (match(TokenKind::Tuple)) { ty->kind = Type::Kind::Tuple; return ty; }
    if (match(TokenKind::Mutex)) { ty->kind = Type::Kind::Mutex; return ty; }
    if (match(TokenKind::Thread)) { ty->kind = Type::Kind::Thread; return ty; }
    if (match(TokenKind::Chan)) {
        ty->kind = Type::Kind::Chan;
        if (match(TokenKind::LBracket)) {
            ty->typeArgs.push_back(*parseType());
            expect(TokenKind::RBracket, "expected ']' after chan type");
        }
        return ty;
    }

    if (check(TokenKind::Ident)) {
        std::string id = current_.text;
        advance();
        if (match(TokenKind::Dot)) {
            if (!check(TokenKind::Ident)) { error("expected type name after '.'"); }
            ty->kind = Type::Kind::StructRef;
            ty->ns = id;
            ty->structName = current_.text;
            advance();
        } else {
            ty->kind = Type::Kind::StructRef;
            ty->structName = id;
        }
        bool isBracket = false;
        if (match(TokenKind::Lt) || (isBracket = match(TokenKind::LBracket))) {
            TokenKind closing = isBracket ? TokenKind::RBracket : TokenKind::Gt;
            do {
                ty->typeArgs.push_back(*parseType());
            } while (match(TokenKind::Comma));
            expect(closing, isBracket ? "expected ']'" : "expected '>'");
        }
        return ty;
    }
    error("expected type");
    ty->kind = Type::Kind::Int;
    return ty;
}

} // namespace gspp
