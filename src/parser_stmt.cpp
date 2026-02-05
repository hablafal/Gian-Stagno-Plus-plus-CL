#include "parser.h"

namespace gspp {

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
        stmt->varType = *parseType();
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
    if (hasParen) expect(TokenKind::RParen, "expected ')'");
    match(TokenKind::Colon);
    stmt->thenBranch = parseBlock();
    if (match(TokenKind::Else) || check(TokenKind::Elif)) {
        if (check(TokenKind::Elif) || check(TokenKind::If)) {
            stmt->elseBranch = parseIf();
        } else {
            match(TokenKind::Colon);
            stmt->elseBranch = parseBlock();
        }
    }
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseWhile() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::While;
    stmt->loc = loc();
    advance(); // while
    bool hasParen = match(TokenKind::LParen);
    stmt->condition = parseExpr();
    if (hasParen) expect(TokenKind::RParen, "expected ')'");
    match(TokenKind::Colon);
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseFor() {
    auto stmt = std::make_unique<Stmt>();
    stmt->loc = loc();
    advance(); // for
    bool hasParen = match(TokenKind::LParen);

    if (check(TokenKind::Ident) && lexer_.peek().kind == TokenKind::In) {
        stmt->kind = Stmt::Kind::ForEach;
        stmt->varName = current_.text;
        advance(); // ident
        advance(); // in
        if (check(TokenKind::Ident) && current_.text == "range") {
            advance(); // range
            expect(TokenKind::LParen, "expected '(' after range");
            stmt->kind = Stmt::Kind::RangeFor;
            stmt->startExpr = parseExpr();
            expect(TokenKind::Comma, "expected ','");
            stmt->endExpr = parseExpr();
            expect(TokenKind::RParen, "expected ')'");
        } else {
            stmt->expr = parseExpr();
        }
    } else {
        stmt->kind = Stmt::Kind::For;
        stmt->initStmt = parseStmt();
        stmt->condition = parseExpr();
        match(TokenKind::Semicolon);
        stmt->stepStmt = parseStmt();
    }

    if (hasParen) expect(TokenKind::RParen, "expected ')'");
    match(TokenKind::Colon);
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseReturn() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::Return;
    stmt->loc = loc();
    advance(); // return
    if (!check(TokenKind::Semicolon) && !check(TokenKind::Newline) && !check(TokenKind::Dedent) && !check(TokenKind::RBrace)) {
        stmt->returnExpr = parseExpr();
    }
    match(TokenKind::Semicolon);
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
    while (check(TokenKind::Newline)) advance();
    SourceLoc l = loc();
    if (check(TokenKind::LBrace) || check(TokenKind::Indent)) return parseBlock();

    if (check(TokenKind::Ident) && lexer_.peek().kind == TokenKind::Colon) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::VarDecl;
        stmt->loc = l;
        stmt->varName = current_.text;
        advance(); advance(); // ident :
        stmt->varType = *parseType();
        if (match(TokenKind::Assign)) stmt->varInit = parseExpr();
        match(TokenKind::Semicolon);
        return stmt;
    }
    if (check(TokenKind::Var) || check(TokenKind::Let)) return parseVarDecl();
    if (match(TokenKind::Mutex)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::VarDecl;
        stmt->loc = l;
        if (!check(TokenKind::Ident)) { error("expected variable name after 'mutex'"); }
        stmt->varName = current_.text;
        advance();
        stmt->varType.kind = Type::Kind::Mutex;
        match(TokenKind::Semicolon);
        return stmt;
    }
    if (check(TokenKind::If) || check(TokenKind::Elif)) return parseIf();
    if (check(TokenKind::While)) return parseWhile();
    if (check(TokenKind::For)) return parseFor();
    if (match(TokenKind::Join)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Join;
        stmt->loc = l;
        stmt->expr = parseExpr();
        match(TokenKind::Semicolon);
        return stmt;
    }
    if (match(TokenKind::Lock)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Lock;
        stmt->loc = l;
        stmt->expr = parseExpr();
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
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::ExprStmt;
        stmt->loc = l;
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Delete;
        e->right = parseExpr();
        e->loc = l;
        stmt->expr = std::move(e);
        match(TokenKind::Semicolon);
        return stmt;
    }
    if (match(TokenKind::Unsafe)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Unsafe;
        stmt->loc = l;
        match(TokenKind::Colon);
        stmt->body = parseBlock();
        return stmt;
    }
    if (match(TokenKind::Asm)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Asm;
        stmt->loc = l;
        expect(TokenKind::LBrace, "expected '{' after asm");
        if (check(TokenKind::StringLit)) {
            stmt->asmCode = current_.text;
            advance();
        }
        expect(TokenKind::RBrace, "expected '}' after asm");
        return stmt;
    }

    auto expr = parseExpr();
    if (match(TokenKind::Assign)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Assign;
        stmt->loc = l;
        stmt->assignTarget = std::move(expr);
        stmt->assignValue = parseExpr();
        match(TokenKind::Semicolon);
        return stmt;
    }
    if (match(TokenKind::ArrowLeft)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Send;
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

} // namespace gspp
