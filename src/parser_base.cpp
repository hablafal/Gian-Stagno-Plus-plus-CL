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
        if (current_.kind == TokenKind::Newline) { advance(); return; }
        if (current_.kind == TokenKind::RBrace) return;
        if (current_.kind == TokenKind::Dedent) return;
        if (current_.kind == TokenKind::Func || current_.kind == TokenKind::Def || current_.kind == TokenKind::Fn) return;
        if (current_.kind == TokenKind::Struct || current_.kind == TokenKind::Class) return;
        advance();
    }
}

} // namespace gspp
