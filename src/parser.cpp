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
    if (match(TokenKind::Float) || match(TokenKind::Decimal)) { ty->kind = Type::Kind::Float; return ty; }
    if (match(TokenKind::Bool)) { ty->kind = Type::Kind::Bool; return ty; }
    if (match(TokenKind::String) || match(TokenKind::Text)) { ty->kind = Type::Kind::String; return ty; }
    if (match(TokenKind::Arr)) { ty->kind = Type::Kind::List; return ty; }
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
    if (match(TokenKind::Nil)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::IntLit;
        e->intVal = 0;
        e->exprType.kind = Type::Kind::Void;
        e->loc = l;
        return e;
    }
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
        SourceLoc loc = l;
        if (check(TokenKind::RBracket)) {
            advance();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::ListLit;
            e->loc = loc;
            return e;
        }
        auto first = parseExpr();
        if (match(TokenKind::For)) {
            // List comprehension: [ expr for var in list (if cond) ]
            if (!check(TokenKind::Ident)) { error("expected variable name in comprehension"); }
            std::string var = current_.text;
            advance();
            expect(TokenKind::In, "expected 'in' in comprehension");
            auto listExpr = parseExpr();
            std::unique_ptr<Expr> cond;
            if (match(TokenKind::If)) cond = parseExpr();
            expect(TokenKind::RBracket, "expected ']'");
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Comprehension;
            e->ident = var;
            e->left = std::move(first);
            e->right = std::move(listExpr);
            e->cond = std::move(cond);
            e->loc = loc;
            return e;
        } else {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::ListLit;
            e->loc = loc;
            e->args.push_back(std::move(first));
            while (match(TokenKind::Comma)) {
                if (check(TokenKind::RBracket)) break;
                e->args.push_back(parseExpr());
            }
            expect(TokenKind::RBracket, "expected ']' after list elements");
            return e;
        }
    }
    if (match(TokenKind::LBrace)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::DictLit;
        e->loc = l;
        if (!check(TokenKind::RBrace)) {
            do {
                auto key = parseExpr();
                expect(TokenKind::Colon, "expected ':' after key");
                auto val = parseExpr();
                e->args.push_back(std::move(key));
                e->args.push_back(std::move(val));
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RBrace, "expected '}' after dict literal");
        return e;
    }
    error("expected expression");
advance(); // Progress!
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
            auto first = parseExpr();
            if (match(TokenKind::Colon)) {
                auto second = parseExpr();
                expect(TokenKind::RBracket, "expected ']' after slice");
                auto e = std::make_unique<Expr>();
                e->kind = Expr::Kind::Slice;
                e->left = std::move(base);
                e->args.push_back(std::move(first));
                e->args.push_back(std::move(second));
                e->loc = l;
                base = std::move(e);
            } else {
                expect(TokenKind::RBracket, "expected ']' after index");
                auto e = std::make_unique<Expr>();
                e->kind = Expr::Kind::Index;
                e->left = std::move(base);
                e->right = std::move(first);
                e->loc = l;
                base = std::move(e);
            }
        } else if ((check(TokenKind::Lt) && lexer_.peekForGenericEnd()) || check(TokenKind::LParen)) {
            std::vector<Type> typeArgs;
            if (match(TokenKind::Lt)) {
                do {
                    typeArgs.push_back(*parseType());
                } while (match(TokenKind::Comma));
                expect(TokenKind::Gt, "expected '>' after type arguments");
            }

            if (!match(TokenKind::LParen)) {
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
                c->left = std::move(base->left);
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
    SourceLoc l = loc();
    if (match(TokenKind::If)) {
        auto condition = parseExpr();
        expect(TokenKind::Then, "expected 'then' after if condition");
        auto thenExpr = parseExpr();
        expect(TokenKind::Else, "expected 'else' in ternary expression");
        auto elseExpr = parseExpr();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Ternary;
        e->cond = std::move(condition);
        e->left = std::move(thenExpr);
        e->right = std::move(elseExpr);
        e->loc = l;
        return e;
    }
    return parseBinary(0);
}

std::unique_ptr<Stmt> Parser::parseBlock() {
    while (check(TokenKind::Newline)) advance();
    auto block = std::make_unique<Stmt>();
    block->kind = Stmt::Kind::Block;
    block->loc = loc();

    if (match(TokenKind::LBrace)) {
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            if (check(TokenKind::Newline)) { advance(); continue; }
            block->blockStmts.push_back(parseStmt());
        }
        expect(TokenKind::RBrace, "expected '}'");
    } else if (match(TokenKind::Indent)) {
        while (!check(TokenKind::Dedent) && !check(TokenKind::Eof)) {
            if (check(TokenKind::Newline)) { advance(); continue; }
            block->blockStmts.push_back(parseStmt());
        }
        expect(TokenKind::Dedent, "expected dedent");
    } else if (match(TokenKind::Colon)) {
        block->blockStmts.push_back(parseStmt());
    } else {
        block->blockStmts.push_back(parseStmt());
    }
    return block;
}

std::unique_ptr<Stmt> Parser::parseVarDecl() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::VarDecl;
    stmt->loc = loc();
    advance(); // var or let
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
    match(TokenKind::Semicolon);
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseIf() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::If;
    stmt->loc = loc();
    advance(); // if or elif
    bool hasParen = match(TokenKind::LParen);
    stmt->condition = parseExpr();
    if (hasParen) expect(TokenKind::RParen, "expected ')' after condition");
    expect(TokenKind::Colon, "expected ':' after 'if' condition");
    stmt->thenBranch = parseBlock();
    if (match(TokenKind::Else)) {
        match(TokenKind::Colon);
        stmt->elseBranch = parseBlock();
    } else if (check(TokenKind::Elif)) {
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
    if (hasParen) expect(TokenKind::RParen, "expected ')' after condition");
    expect(TokenKind::Colon, "expected ':' after 'while' condition");
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseFor() {
    auto stmt = std::make_unique<Stmt>();
    stmt->loc = loc();
    advance(); // for
    bool hasParen = match(TokenKind::LParen);

    if (check(TokenKind::Ident)) {
        std::string varName = current_.text;
        if (lexer_.peek().kind == TokenKind::In) {
            advance(); // consume ident
            advance(); // consume in
            stmt->kind = Stmt::Kind::RangeFor;
            stmt->varName = varName;
            stmt->startExpr = parseExpr();
            if (match(TokenKind::DotDotDot)) {
                stmt->endExpr = parseExpr();
                stmt->isInclusive = true;
            } else {
                expect(TokenKind::DotDot, "expected '..' or '...' in range for");
                stmt->endExpr = parseExpr();
            }
            if (hasParen) expect(TokenKind::RParen, "expected ')'");
            match(TokenKind::Colon);
            stmt->body = parseBlock();
            return stmt;
        }
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
    if (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !check(TokenKind::Newline))
        stmt->returnExpr = parseExpr();
    match(TokenKind::Semicolon);
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
    while (check(TokenKind::Newline)) advance();
    SourceLoc l = loc();
    if (check(TokenKind::LBrace) || check(TokenKind::Indent)) return parseBlock();

    if (check(TokenKind::Ident) && lexer_.peek().kind == TokenKind::Colon) {
        // x: int = 5
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::VarDecl;
        stmt->loc = l;
        stmt->varName = current_.text;
        advance(); // consume ident
        advance(); // consume :
        auto ty = parseType();
        stmt->varType = *ty;
        if (match(TokenKind::Assign)) stmt->varInit = parseExpr();
        match(TokenKind::Semicolon);
        return stmt;
    }
    if (check(TokenKind::Var) || check(TokenKind::Let)) return parseVarDecl();
    if (check(TokenKind::If) || check(TokenKind::Elif)) return parseIf();
    if (check(TokenKind::While)) return parseWhile();
    if (check(TokenKind::For)) return parseFor();
    if (check(TokenKind::Check)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Switch;
        stmt->loc = l;
        advance(); // check
        stmt->condition = parseExpr();
        match(TokenKind::Colon);
        stmt->body = parseBlock();
        return stmt;
    }
    if (match(TokenKind::Case)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Case;
        stmt->loc = l;
        stmt->condition = parseExpr();
        match(TokenKind::Colon);
        stmt->body = parseBlock();
        return stmt;
    }
    if (check(TokenKind::Loop)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::ForEach;
        stmt->loc = l;
        advance(); // loop
        stmt->expr = parseExpr(); // the list
        if (match(TokenKind::As)) {
            if (check(TokenKind::Ident)) {
                stmt->varName = current_.text;
                advance();
            }
        }
        match(TokenKind::Colon);
        stmt->body = parseBlock();
        return stmt;
    }
    if (check(TokenKind::Repeat)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Repeat;
        stmt->loc = l;
        advance(); // repeat
        stmt->condition = parseExpr();
        match(TokenKind::Colon);
        stmt->body = parseBlock();
        return stmt;
    }
    if (check(TokenKind::Return)) return parseReturn();
    if (match(TokenKind::Defer)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Defer;
        stmt->loc = l;
        stmt->body = parseStmt();
        return stmt;
    }
    if (match(TokenKind::Delete)) {
        auto expr = parseExpr();
        match(TokenKind::Semicolon);
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
    auto expr = parseExpr();
    if (check(TokenKind::Assign)) {
        advance();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Assign;
        stmt->loc = l;
        stmt->assignTarget = std::move(expr);
        stmt->assignValue = parseExpr();
        match(TokenKind::Semicolon);
        return stmt;
    }
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::ExprStmt;
    stmt->loc = l;
    stmt->expr = std::move(expr);
    match(TokenKind::Semicolon);
    return stmt;
}

StructDecl Parser::parseStructDecl() {
    StructDecl s;
    s.loc = loc();
    advance(); // struct or class or data
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
    match(TokenKind::Colon);
    while (check(TokenKind::Newline)) advance();
    if (match(TokenKind::LBrace)) {
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            if (check(TokenKind::Newline)) { advance(); continue; }
            if (check(TokenKind::Func) || check(TokenKind::Def) || check(TokenKind::Fn)) {
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
            if (check(TokenKind::Newline)) { advance(); continue; }
            if (check(TokenKind::Func) || check(TokenKind::Def) || check(TokenKind::Fn)) {
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
                auto ty = parseType();
                p.type = *ty;
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
            auto ty = parseType();
            f.returnType = *ty;
        } else {
            f.returnType.kind = Type::Kind::Int;
        }
    } else {
        f.returnType.kind = Type::Kind::Int;
    }
    if (isExtern && match(TokenKind::Semicolon)) {
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
