#include "parser.h"
#include <sstream>

namespace gspp {

Parser::Parser(Lexer& lexer) : lexer_(lexer) {
    current_ = lexer_.next();
}

Token Parser::cur() {
    return current_;
}

Token Parser::advance() {
    Token prev = current_;
    if (current_.kind != TokenKind::Eof)
        current_ = lexer_.next();
    return prev;
}

bool Parser::check(TokenKind k) const {
    return current_.kind == k;
}

bool Parser::match(TokenKind k) {
    if (!check(k)) return false;
    advance();
    return true;
}

bool Parser::expect(TokenKind k, const char* msg) {
    if (check(k)) { advance(); return true; }
    error(msg);
    return false;
}

SourceLoc Parser::loc() const {
    return current_.loc;
}

void Parser::error(const std::string& msg) {
    errors_.push_back(SourceManager::instance().formatError(current_.loc, msg));
}

void Parser::sync() {
    while (current_.kind != TokenKind::Eof) {
        if (current_.kind == TokenKind::Semicolon) { advance(); return; }
        if (current_.kind == TokenKind::RBrace) return;
        if (current_.kind == TokenKind::Func) return;
        if (current_.kind == TokenKind::Struct) return;
        advance();
    }
}

std::unique_ptr<Type> Parser::parseType() {
    SourceLoc l = loc();
    if (match(TokenKind::Star)) {
        auto ty = std::make_unique<Type>();
        ty->kind = Type::Kind::Pointer;
        ty->ptrTo = parseType();
        ty->loc = l;
        return ty;
    }
    auto ty = std::make_unique<Type>();
    ty->loc = l;
    if (match(TokenKind::Int)) { ty->kind = Type::Kind::Int; return ty; }
    if (match(TokenKind::Float)) { ty->kind = Type::Kind::Float; return ty; }
    if (match(TokenKind::Bool)) { ty->kind = Type::Kind::Bool; return ty; }
    if (match(TokenKind::String)) { ty->kind = Type::Kind::String; return ty; }
    if (match(TokenKind::Char)) { ty->kind = Type::Kind::Char; return ty; }
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
        if (match(TokenKind::Lt)) {
            do {
                ty->typeArgs.push_back(*parseType());
            } while (match(TokenKind::Comma));
            expect(TokenKind::Gt, "expected '>' after type arguments");
        }
        return ty;
    }
    error("expected type");
    ty->kind = Type::Kind::Int;
    return ty;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    SourceLoc l = loc();
    if (check(TokenKind::IntLit)) {
        int64_t v = current_.intVal;
        advance();
        return Expr::makeIntLit(v, l);
    }
    if (check(TokenKind::FloatLit)) {
        double v = current_.floatVal;
        advance();
        return Expr::makeFloatLit(v, l);
    }
    if (match(TokenKind::True)) return Expr::makeBoolLit(true, l);
    if (match(TokenKind::False)) return Expr::makeBoolLit(false, l);
    if (check(TokenKind::Ident)) {
        std::string id = current_.text;
        advance();
        if (check(TokenKind::LParen)) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parseExpr());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')' after arguments");
            return Expr::makeCall(id, std::move(args), l);
        }
        return Expr::makeVar(id, l);
    }
    if (match(TokenKind::LParen)) {
        auto e = parseExpr();
        expect(TokenKind::RParen, "expected ')'");
        return e;
    }
    if (match(TokenKind::New)) {
        auto ty = parseType();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::New;
        if (match(TokenKind::LBracket)) {
            e->left = parseExpr();
            expect(TokenKind::RBracket, "expected ']' after array size");
        }
        e->targetType = std::move(ty);
        e->loc = l;
        return e;
    }
    if (check(TokenKind::StringLit)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::StringLit;
        e->ident = current_.text;
        e->loc = l;
        advance();
        return e;
    }
    if (match(TokenKind::LBracket)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::ListLit;
        e->loc = l;
        if (!check(TokenKind::RBracket)) {
            do {
                e->args.push_back(parseExpr());
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RBracket, "expected ']' after list elements");
        return e;
    }
    error("expected expression");
    return Expr::makeIntLit(0, l);
}

std::unique_ptr<Expr> Parser::parsePostfix(std::unique_ptr<Expr> base) {
    while (true) {
        SourceLoc l = loc();
        if (match(TokenKind::Dot)) {
            if (!check(TokenKind::Ident)) { error("expected member name"); break; }
            std::string mem = current_.text;
            advance();
            auto m = std::make_unique<Expr>();
            m->kind = Expr::Kind::Member;
            m->left = std::move(base);
            m->member = mem;
            m->loc = l;
            base = std::move(m);
        } else if (match(TokenKind::LBracket)) {
            auto idx = parseExpr();
            expect(TokenKind::RBracket, "expected ']' after index");
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Index;
            e->left = std::move(base);
            e->right = std::move(idx);
            e->loc = l;
            base = std::move(e);
        } else if ((check(TokenKind::Lt) && lexer_.peekForGenericEnd()) || check(TokenKind::LParen)) {
            std::vector<Type> typeArgs;
            if (match(TokenKind::Lt)) {
                do {
                    typeArgs.push_back(*parseType());
                } while (match(TokenKind::Comma));
                expect(TokenKind::Gt, "expected '>' after type arguments");
            }

            if (!match(TokenKind::LParen)) {
                // If it was just <T> but no (args), it might be a type in an expression context?
                // For now, let's assume it MUST be a call if it has type args in postfix.
                error("expected '(' after type arguments");
                break;
            }

            std::vector<std::unique_ptr<Expr>> args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parseExpr());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')' after arguments");

            if (base->kind == Expr::Kind::Member) {
                std::string func = base->member;
                auto c = Expr::makeCall(func, std::move(args), l);
                c->left = std::move(base->left); // The object/namespace
                if (c->left->kind == Expr::Kind::Var) {
                    c->ns = c->left->ident; // Could be a namespace
                }
                c->exprType.typeArgs = typeArgs;
                base = std::move(c);
            } else if (base->kind == Expr::Kind::Var) {
                std::string func = base->ident;
                auto c = Expr::makeCall(func, std::move(args), l);
                c->exprType.typeArgs = typeArgs;
                base = std::move(c);
            } else {
                error("expression is not callable");
            }
        } else {
            break;
        }
    }
    return base;
}

int Parser::binPrec(const std::string& op) {
    if (op == "or") return 1;
    if (op == "and") return 2;
    if (op == "==" || op == "!=") return 3;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 4;
    if (op == "+" || op == "-") return 5;
    if (op == "*" || op == "/" || op == "%") return 6;
    return 0;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    SourceLoc l = loc();
    if (match(TokenKind::Minus)) {
        auto operand = parseUnary();
        return Expr::makeUnary("-", std::move(operand), l);
    }
    if (match(TokenKind::Not)) {
        auto operand = parseUnary();
        return Expr::makeUnary("not", std::move(operand), l);
    }
    if (match(TokenKind::Star)) {
        auto operand = parseUnary();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Deref;
        e->right = std::move(operand);
        e->loc = l;
        return e;
    }
    if (match(TokenKind::Amp)) {
        auto operand = parseUnary();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::AddressOf;
        e->right = std::move(operand);
        e->loc = l;
        return e;
    }
    return parsePostfix(parsePrimary());
}

std::unique_ptr<Expr> Parser::parseBinary(int minPrec) {
    auto left = parseUnary();
    for (;;) {
        std::string op;
        if (check(TokenKind::Plus)) op = "+";
        else if (check(TokenKind::Minus)) op = "-";
        else if (check(TokenKind::Star)) op = "*";
        else if (check(TokenKind::Slash)) op = "/";
        else if (check(TokenKind::Percent)) op = "%";
        else if (check(TokenKind::Eq)) op = "==";
        else if (check(TokenKind::Ne)) op = "!=";
        else if (check(TokenKind::Lt)) op = "<";
        else if (check(TokenKind::Gt)) op = ">";
        else if (check(TokenKind::Le)) op = "<=";
        else if (check(TokenKind::Ge)) op = ">=";
        else if (check(TokenKind::And)) op = "and";
        else if (check(TokenKind::Or)) op = "or";
        else break;
        int prec = binPrec(op);
        if (prec < minPrec) break;
        advance();
        auto right = parseBinary(prec + 1);
        left = Expr::makeBinary(std::move(left), op, std::move(right), loc());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseExpr() {
    return parseBinary(0);
}

std::unique_ptr<Stmt> Parser::parseBlock() {
    auto block = std::make_unique<Stmt>();
    block->kind = Stmt::Kind::Block;
    block->loc = loc();
    if (!expect(TokenKind::LBrace, "expected '{'")) return block;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        block->blockStmts.push_back(parseStmt());
    }
    expect(TokenKind::RBrace, "expected '}'");
    return block;
}

std::unique_ptr<Stmt> Parser::parseVarDecl() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::VarDecl;
    stmt->loc = loc();
    advance(); // var
    if (!check(TokenKind::Ident)) { error("expected variable name"); sync(); return stmt; }
    stmt->varName = current_.text;
    advance();
    if (match(TokenKind::Colon)) {
        auto ty = parseType();
        stmt->varType = *ty;
    }
    if (match(TokenKind::Assign)) {
        stmt->varInit = parseExpr();
    }
    match(TokenKind::Semicolon); // optional
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseIf() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::If;
    stmt->loc = loc();
    advance(); // if
    bool hasParen = match(TokenKind::LParen);
    stmt->condition = parseExpr();
    if (hasParen) expect(TokenKind::RParen, "expected ')'");
    stmt->thenBranch = parseBlock();
    if (match(TokenKind::Else)) {
        if (check(TokenKind::If)) {
            stmt->elseBranch = parseIf();
        } else {
            stmt->elseBranch = parseBlock();
        }
    } else if (check(TokenKind::Elif)) {
        current_.kind = TokenKind::If; // Treat 'elif' as 'if' for recursion
        stmt->elseBranch = parseIf();
    }
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseWhile() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::While;
    stmt->loc = loc();
    advance();
    bool hasParen = match(TokenKind::LParen);
    stmt->condition = parseExpr();
    if (hasParen) expect(TokenKind::RParen, "expected ')'");
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseFor() {
    auto stmt = std::make_unique<Stmt>();
    stmt->loc = loc();
    advance(); // for
    bool hasParen = match(TokenKind::LParen);

    // If no parentheses, it MUST be a range for or an error (Python style)
    // If parentheses, we check if it's a range for (optional parens) or standard C for
    if (!hasParen) {
        if (!check(TokenKind::Ident)) {
            error("expected identifier in for loop");
            return stmt;
        }
        stmt->kind = Stmt::Kind::RangeFor;
        stmt->varName = current_.text;
        advance();
        expect(TokenKind::In, "expected 'in' in range for");
        stmt->startExpr = parseExpr();
        expect(TokenKind::DotDot, "expected '..' in range for");
        stmt->endExpr = parseExpr();
        stmt->body = parseBlock();
        return stmt;
    }

    // Has parentheses. Check if it's (i in 0..10) or (i=0; i<10; i++)
    // We can use a simple heuristic: if there's a semicolon, it's standard C for.
    // But we don't have multi-token peek.
    // However, if we see 'Ident' and then 'In', it's definitely RangeFor.
    // We can use the lexer's peek() to see the NEXT token.
    if (check(TokenKind::Ident)) {
        // We need to peek past the Ident. Lexer only has 1 token peek.
        // I'll add a TokenKind::In check after Ident.
        // Wait, I can just try to parse a statement and see if it was an assignment,
        // but that's messy.
        // Better: allow range for to NOT have parentheses, and standard for MUST have them.
        // (Which is what I did with the !hasParen check above).
        // For the case with parentheses, let's assume it's a standard C for for now,
        // unless we want to support (i in 1..5).
        // Let's just support standard C for if there are parentheses.
    }

    stmt->kind = Stmt::Kind::For;
    stmt->initStmt = parseStmt();
    stmt->condition = parseExpr();
    expect(TokenKind::Semicolon, "expected ';' in for");
    stmt->stepStmt = parseStmt();
    if (hasParen) expect(TokenKind::RParen, "expected ')'");
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseReturn() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::Return;
    stmt->loc = loc();
    advance();
    if (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace))
        stmt->returnExpr = parseExpr();
    expect(TokenKind::Semicolon, "expected ';' after return");
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
    SourceLoc l = loc();
    if (check(TokenKind::LBrace)) return parseBlock();
    if (check(TokenKind::Var) || check(TokenKind::Let)) return parseVarDecl();
    if (check(TokenKind::If)) return parseIf();
    if (check(TokenKind::While)) return parseWhile();
    if (check(TokenKind::For)) return parseFor();
    if (check(TokenKind::Repeat)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Repeat;
        stmt->loc = loc();
        advance(); // repeat
        stmt->condition = parseExpr();
        stmt->body = parseBlock();
        return stmt;
    }
    if (check(TokenKind::Return)) return parseReturn();
    if (match(TokenKind::Delete)) {
        auto expr = parseExpr();
        expect(TokenKind::Semicolon, "expected ';' after delete");
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::ExprStmt;
        s->loc = l;
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Delete;
        e->right = std::move(expr);
        e->loc = l;
        s->expr = std::move(e);
        return s;
    }
    if (match(TokenKind::Unsafe)) {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Unsafe;
        s->loc = l;
        s->body = parseBlock();
        return s;
    }
    if (match(TokenKind::Asm)) {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Asm;
        s->loc = l;
        expect(TokenKind::LBrace, "expected '{' after asm");
        if (check(TokenKind::StringLit)) {
            s->asmCode = current_.text;
            advance();
        }
        expect(TokenKind::RBrace, "expected '}' after asm code");
        return s;
    }
    // assignment or expression statement
    auto expr = parseExpr();
    if (check(TokenKind::Assign)) {
        advance();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Assign;
        stmt->loc = l;
        stmt->assignTarget = std::move(expr);
        stmt->assignValue = parseExpr();
        match(TokenKind::Semicolon); // optional semicolon
        return stmt;
    }
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::ExprStmt;
    stmt->loc = l;
    stmt->expr = std::move(expr);
    match(TokenKind::Semicolon); // optional semicolon
    return stmt;
}

StructDecl Parser::parseStructDecl() {
    StructDecl s;
    s.loc = loc();
    advance(); // struct or class
    if (!check(TokenKind::Ident)) { error("expected struct/class name"); sync(); return s; }
    s.name = current_.text;
    advance();
    if (match(TokenKind::Lt)) {
        do {
            if (!check(TokenKind::Ident)) { error("expected type parameter name"); }
            s.typeParams.push_back(current_.text);
            advance();
        } while (match(TokenKind::Comma));
        expect(TokenKind::Gt, "expected '>' after type parameters");
    }
    expect(TokenKind::LBrace, "expected '{'");
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        if (check(TokenKind::Func) || check(TokenKind::Def)) {
            s.methods.push_back(parseFuncDecl(false));
        } else if (check(TokenKind::Ident)) {
            StructMember m;
            m.name = current_.text;
            m.loc = loc();
            advance();
            if (match(TokenKind::Colon)) {
                auto ty = parseType();
                m.type = *ty;
            } else {
                // Infer type? For members we need at least a hint or default to int
                m.type.kind = Type::Kind::Int;
            }
            s.members.push_back(std::move(m));
            match(TokenKind::Semicolon); // optional
        } else {
            error("expected member or method");
            sync();
        }
    }
    expect(TokenKind::RBrace, "expected '}'");
    return s;
}

FuncDecl Parser::parseFuncDecl(bool isExtern) {
    FuncDecl f;
    f.loc = loc();
    advance(); // func
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
                auto ty = parseType();
                p.type = *ty;
            } else {
                p.type.kind = Type::Kind::Int;
            }
            f.params.push_back(std::move(p));
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "expected ')'");
    if (match(TokenKind::Arrow)) {
        auto ty = parseType();
        f.returnType = *ty;
    } else {
        f.returnType.kind = Type::Kind::Int;
    }
    if (isExtern && check(TokenKind::Semicolon)) {
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
        if (check(TokenKind::Struct) || check(TokenKind::Class)) {
            prog->structs.push_back(parseStructDecl());
        } else if (check(TokenKind::Func)) {
            prog->functions.push_back(parseFuncDecl(false));
        } else if (match(TokenKind::Extern)) {
            std::string lib = "C";
            if (check(TokenKind::StringLit)) {
                lib = current_.text;
                advance();
            }
            if (!check(TokenKind::Func)) {
                error("expected 'func' or 'def' after extern");
            } else {
                FuncDecl f = parseFuncDecl(true);
                f.isExtern = true;
                f.externLib = lib;
                prog->functions.push_back(std::move(f));
            }
        } else if (check(TokenKind::Import)) {
            Import imp;
            imp.loc = loc();
            advance(); // import
            if (check(TokenKind::StringLit)) {
                imp.path = current_.text;
                // auto-derive name from filename
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
                error("expected string literal or identifier after 'import'");
            }
            expect(TokenKind::Semicolon, "expected ';' after import");
            prog->imports.push_back(std::move(imp));
        } else {
            // Treat anything else as a top-level statement
            prog->topLevelStmts.push_back(parseStmt());
        }
    }
    return prog;
}

} // namespace gspp
