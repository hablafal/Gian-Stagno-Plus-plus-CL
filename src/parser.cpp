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
    SourceLoc l;
    l.line = current_.line;
    l.column = current_.column;
    return l;
}

void Parser::error(const std::string& msg) {
    std::ostringstream os;
    os << lexer_.filename() << ":" << current_.line << ":" << current_.column << ": error: " << msg;
    errors_.push_back(os.str());
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
    auto ty = std::make_unique<Type>();
    ty->loc = l;
    if (match(TokenKind::Int)) { ty->kind = Type::Kind::Int; return ty; }
    if (match(TokenKind::Float)) { ty->kind = Type::Kind::Float; return ty; }
    if (match(TokenKind::Bool)) { ty->kind = Type::Kind::Bool; return ty; }
    if (check(TokenKind::Ident)) {
        ty->kind = Type::Kind::StructRef;
        ty->structName = current_.text;
        advance();
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
    error("expected expression");
    return Expr::makeIntLit(0, l);
}

std::unique_ptr<Expr> Parser::parsePostfix(std::unique_ptr<Expr> base) {
    SourceLoc l = loc();
    while (match(TokenKind::Dot)) {
        if (!check(TokenKind::Ident)) { error("expected member name"); break; }
        std::string mem = current_.text;
        advance();
        auto m = std::make_unique<Expr>();
        m->kind = Expr::Kind::Member;
        m->left = std::move(base);
        m->member = mem;
        m->loc = l;
        base = std::move(m);
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
    expect(TokenKind::Semicolon, "expected ';' after variable declaration");
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseIf() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::If;
    stmt->loc = loc();
    advance(); // if
    expect(TokenKind::LParen, "expected '(' after 'if'");
    stmt->condition = parseExpr();
    expect(TokenKind::RParen, "expected ')'");
    stmt->thenBranch = parseBlock();
    if (match(TokenKind::Else)) {
        if (check(TokenKind::If)) {
            stmt->elseBranch = parseIf();
        } else {
            stmt->elseBranch = parseBlock();
        }
    }
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseWhile() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::While;
    stmt->loc = loc();
    advance();
    expect(TokenKind::LParen, "expected '(' after 'while'");
    stmt->condition = parseExpr();
    expect(TokenKind::RParen, "expected ')'");
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseFor() {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::For;
    stmt->loc = loc();
    advance();
    expect(TokenKind::LParen, "expected '(' after 'for'");
    stmt->initStmt = parseStmt();
    stmt->condition = parseExpr();
    expect(TokenKind::Semicolon, "expected ';' in for");
    stmt->stepStmt = parseStmt();
    expect(TokenKind::RParen, "expected ')'");
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
    if (check(TokenKind::Return)) return parseReturn();
    // assignment or expression statement
    auto expr = parseExpr();
    if (check(TokenKind::Assign)) {
        advance();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Assign;
        stmt->loc = l;
        stmt->assignTarget = std::move(expr);
        stmt->assignValue = parseExpr();
        expect(TokenKind::Semicolon, "expected ';' after assignment");
        return stmt;
    }
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::ExprStmt;
    stmt->loc = l;
    stmt->expr = std::move(expr);
    expect(TokenKind::Semicolon, "expected ';' after expression");
    return stmt;
}

StructDecl Parser::parseStructDecl() {
    StructDecl s;
    s.loc = loc();
    advance(); // struct or class
    if (!check(TokenKind::Ident)) { error("expected struct/class name"); sync(); return s; }
    s.name = current_.text;
    advance();
    expect(TokenKind::LBrace, "expected '{'");
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        if (!check(TokenKind::Ident)) { error("expected member name"); sync(); break; }
        StructMember m;
        m.name = current_.text;
        m.loc = loc();
        advance();
        expect(TokenKind::Colon, "expected ':'");
        auto ty = parseType();
        m.type = *ty;
        s.members.push_back(std::move(m));
        expect(TokenKind::Semicolon, "expected ';'");
    }
    expect(TokenKind::RBrace, "expected '}'");
    return s;
}

FuncDecl Parser::parseFuncDecl() {
    FuncDecl f;
    f.loc = loc();
    advance(); // func
    if (!check(TokenKind::Ident)) { error("expected function name"); sync(); return f; }
    f.name = current_.text;
    advance();
    expect(TokenKind::LParen, "expected '('");
    if (!check(TokenKind::RParen)) {
        do {
            if (!check(TokenKind::Ident)) { error("expected parameter name"); break; }
            FuncParam p;
            p.name = current_.text;
            p.loc = loc();
            advance();
            expect(TokenKind::Colon, "expected ':'");
            auto ty = parseType();
            p.type = *ty;
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
    f.body = parseBlock();
    return f;
}

std::unique_ptr<Program> Parser::parseProgram() {
    auto prog = std::make_unique<Program>();
    prog->loc = loc();
    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::Struct) || check(TokenKind::Class)) {
            prog->structs.push_back(parseStructDecl());
        } else if (check(TokenKind::Func)) {
            prog->functions.push_back(parseFuncDecl());
        } else if (check(TokenKind::Import)) {
            advance();
            if (check(TokenKind::StringLit) || check(TokenKind::Ident)) advance();
            expect(TokenKind::Semicolon, "expected ';' after import");
        } else {
            error("expected 'struct', 'class', 'func'/'def', or 'import' at top level");
            sync();
        }
    }
    return prog;
}

} // namespace gspp
