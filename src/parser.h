#ifndef GSPP_PARSER_H
#define GSPP_PARSER_H

#include "lexer.h"
#include "ast.h"
#include <memory>
#include <string>
#include <vector>

namespace gspp {

class Parser {
public:
    explicit Parser(Lexer& lexer);
    std::unique_ptr<Program> parseProgram();
    const std::vector<std::string>& errors() const { return errors_; }

private:
    Token cur();
    Token advance();
    bool check(TokenKind k) const;
    bool match(TokenKind k);
    bool expect(TokenKind k, const char* msg);
    SourceLoc loc() const;

    std::unique_ptr<Type> parseType();
    std::unique_ptr<Expr> parseExpr();
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parseBinary(int minPrec);
    std::unique_ptr<Expr> parsePostfix(std::unique_ptr<Expr> base);
    int binPrec(const std::string& op);

    std::unique_ptr<Stmt> parseStmt();
    std::unique_ptr<Stmt> parseBlock();
    std::unique_ptr<Stmt> parseVarDecl();
    std::unique_ptr<Stmt> parseIf();
    std::unique_ptr<Stmt> parseWhile();
    std::unique_ptr<Stmt> parseFor();
    std::unique_ptr<Stmt> parseReturn();
    std::unique_ptr<Stmt> parseTry();
    std::unique_ptr<Stmt> parseRaise();

    StructDecl parseStructDecl();
    FuncDecl parseFuncDecl(bool isExtern = false);

    void error(const std::string& msg);
    void sync();

    Lexer& lexer_;
    Token current_;
    std::vector<std::string> errors_;
};

} // namespace gspp

#endif
