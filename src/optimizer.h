#ifndef GSPP_OPTIMIZER_H
#define GSPP_OPTIMIZER_H

#include "ast.h"

namespace gspp {

class Optimizer {
public:
    explicit Optimizer(Program* program) : program_(program) {}
    void optimize();

private:
    void optimizeExpr(Expr* expr);
    void optimizeStmt(Stmt* stmt);
    void optimizeFunc(FuncDecl& f);

    Program* program_;
};

} // namespace gspp

#endif
