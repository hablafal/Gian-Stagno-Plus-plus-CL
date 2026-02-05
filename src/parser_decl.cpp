#include "parser.h"

namespace gspp {

StructDecl Parser::parseStructDecl() {
    StructDecl s;
    s.loc = loc();
    advance(); // struct or class or data
    if (!check(TokenKind::Ident)) { error("expected name"); sync(); return s; }
    s.name = current_.text;
    advance();
    if (match(TokenKind::LParen)) {
        if (check(TokenKind::Ident)) {
            s.baseName = current_.text;
            advance();
        }
        expect(TokenKind::RParen, "expected ')' after base class");
    }
    if (match(TokenKind::Lt)) {
        do {
            if (!check(TokenKind::Ident)) { error("expected type parameter name"); }
            s.typeParams.push_back(current_.text);
            advance();
        } while (match(TokenKind::Comma));
        expect(TokenKind::Gt, "expected '>' after type parameters");
    }
    match(TokenKind::Colon);
    while (check(TokenKind::Newline)) advance();

    if (match(TokenKind::LBrace)) {
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            while (check(TokenKind::Newline)) advance();
            if (check(TokenKind::RBrace)) break;
            if (check(TokenKind::Func) || check(TokenKind::Def) || check(TokenKind::Fn)) {
                s.methods.push_back(parseFuncDecl(false));
            } else if (check(TokenKind::Ident)) {
                StructMember m;
                m.name = current_.text;
                m.loc = loc();
                advance();
                if (match(TokenKind::Colon)) {
                    m.type = *parseType();
                } else {
                    m.type.kind = Type::Kind::Int;
                }
                s.members.push_back(std::move(m));
                match(TokenKind::Semicolon);
            } else {
                error("expected member or method");
                sync();
            }
        }
        expect(TokenKind::RBrace, "expected '}'");
    } else if (match(TokenKind::Indent)) {
        while (!check(TokenKind::Dedent) && !check(TokenKind::Eof)) {
            while (check(TokenKind::Newline)) advance();
            if (check(TokenKind::Dedent)) break;
            if (check(TokenKind::Func) || check(TokenKind::Def) || check(TokenKind::Fn)) {
                s.methods.push_back(parseFuncDecl(false));
            } else if (check(TokenKind::Ident)) {
                StructMember m;
                m.name = current_.text;
                m.loc = loc();
                advance();
                if (match(TokenKind::Colon)) {
                    m.type = *parseType();
                } else {
                    m.type.kind = Type::Kind::Int;
                }
                s.members.push_back(std::move(m));
                match(TokenKind::Semicolon);
            } else {
                error("expected member or method");
                sync();
            }
        }
        expect(TokenKind::Dedent, "expected dedent");
    }
    return s;
}

FuncDecl Parser::parseFuncDecl(bool isExtern) {
    FuncDecl f;
    f.loc = loc();
    advance(); // func or fn or def
    if (!check(TokenKind::Ident)) { error("expected function name"); sync(); return f; }
    f.name = current_.text;
    advance();
    if (match(TokenKind::Lt)) {
        do {
            if (!check(TokenKind::Ident)) { error("expected type parameter name"); }
            f.typeParams.push_back(current_.text);
            advance();
        } while (match(TokenKind::Comma));
        expect(TokenKind::Gt, "expected '>' after type parameters");
    }
    expect(TokenKind::LParen, "expected '('");
    if (!check(TokenKind::RParen)) {
        do {
            if (!check(TokenKind::Ident)) { error("expected parameter name"); break; }
            FuncParam p;
            p.name = current_.text;
            p.loc = loc();
            advance();
            if (match(TokenKind::Colon)) {
                p.type = *parseType();
            } else {
                p.type.kind = Type::Kind::Int;
            }
            f.params.push_back(std::move(p));
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "expected ')'");
    if (match(TokenKind::Arrow) || match(TokenKind::Colon)) {
        while (check(TokenKind::Newline)) advance();
        if (cur().kind != TokenKind::LBrace && cur().kind != TokenKind::Indent) {
            f.returnType = *parseType();
        } else {
            f.returnType.kind = Type::Kind::Int;
        }
    } else {
        f.returnType.kind = Type::Kind::Int;
    }
    match(TokenKind::Colon);
    if (isExtern && (check(TokenKind::Semicolon) || check(TokenKind::Newline))) {
        advance();
    } else {
        f.body = parseBlock();
    }
    return f;
}

std::unique_ptr<Program> Parser::parseProgram() {
    auto prog = std::make_unique<Program>();
    prog->loc = loc();
    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::Newline)) { advance(); continue; }
        if (check(TokenKind::Struct) || check(TokenKind::Class) || check(TokenKind::Data)) {
            prog->structs.push_back(parseStructDecl());
        } else if (check(TokenKind::Func) || check(TokenKind::Def) || check(TokenKind::Fn)) {
            prog->functions.push_back(parseFuncDecl(false));
        } else if (match(TokenKind::Import) || match(TokenKind::Use)) {
            Import imp;
            imp.loc = loc();
            if (check(TokenKind::StringLit)) {
                imp.path = current_.text;
                size_t slash = imp.path.find_last_of("/\\");
                std::string filename = (slash == std::string::npos) ? imp.path : imp.path.substr(slash + 1);
                size_t dot = filename.find_last_of('.');
                imp.name = (dot == std::string::npos) ? filename : filename.substr(0, dot);
                advance();
            } else if (check(TokenKind::Ident)) {
                imp.name = current_.text;
                imp.path = imp.name + ".gs";
                advance();
            } else {
                error("expected string literal or identifier after 'import' or 'use'");
            }
            if (match(TokenKind::As)) {
                if (check(TokenKind::Ident)) {
                    imp.alias = current_.text;
                    advance();
                } else {
                    error("expected alias name after 'as'");
                }
            }
            match(TokenKind::Semicolon);
            prog->imports.push_back(std::move(imp));
        } else if (match(TokenKind::From)) {
            Import imp;
            imp.loc = loc();
            if (check(TokenKind::Ident)) {
                imp.name = current_.text;
                imp.path = imp.name + ".gs";
                advance();
            } else if (check(TokenKind::StringLit)) {
                imp.path = current_.text;
                size_t slash = imp.path.find_last_of("/\\");
                std::string filename = (slash == std::string::npos) ? imp.path : imp.path.substr(slash + 1);
                size_t dot = filename.find_last_of('.');
                imp.name = (dot == std::string::npos) ? filename : filename.substr(0, dot);
                advance();
            }
            expect(TokenKind::Import, "expected 'import' after module name");
            do {
                if (check(TokenKind::Ident)) {
                    imp.importNames.push_back(current_.text);
                    advance();
                } else {
                    error("expected name to import");
                }
            } while (match(TokenKind::Comma));
            match(TokenKind::Semicolon);
            prog->imports.push_back(std::move(imp));
        } else if (match(TokenKind::Extern)) {
            std::string lib = "C";
            if (check(TokenKind::StringLit)) {
                lib = current_.text;
                advance();
            }
            if (!check(TokenKind::Func) && !check(TokenKind::Def) && !check(TokenKind::Fn)) {
                error("expected 'func', 'fn' or 'def' after extern");
            } else {
                FuncDecl f = parseFuncDecl(true);
                f.isExtern = true;
                f.externLib = lib;
                prog->functions.push_back(std::move(f));
            }
        } else {
            prog->topLevelStmts.push_back(parseStmt());
        }
    }
    return prog;
}

} // namespace gspp
