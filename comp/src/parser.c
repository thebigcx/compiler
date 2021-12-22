#include <parser.h>
#include <lexer.h>
#include <asm.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

struct parser
{
    struct token *toks;
    size_t i;
    struct symtable *currscope;
    struct ast      *currfunc;
};

static struct parser s_parser;

static struct sym *s_typedefs = NULL;
static unsigned int s_typedefcnt = 0;

static struct token *next()
{
    return &s_parser.toks[++s_parser.i];
}

static struct token *back()
{
    return &s_parser.toks[--s_parser.i];
}

static struct token *curr()
{
    return &s_parser.toks[s_parser.i];
}

// Acts like post increment '++'
static struct token *postnext()
{
    return &s_parser.toks[s_parser.i++];
}

static void error(const char *msg, ...)
{
    printf("\033[1;31merror: \033[37mat line %d: \033[22m", curr()->line);

    va_list list;
    va_start(list, msg);
    vprintf(msg, list);
    va_end(list);

    exit(-1);
}

static struct ast *mkast(int type)
{
    struct ast *ast = calloc(1, sizeof(struct ast));
    ast->type = type;
    return ast;
}

static struct ast *mkunary(int op, struct ast *val)
{
    struct ast *ast = mkast(A_UNARY);
    ast->unary.op   = op;
    ast->unary.val  = val;
    return ast;
}

// Terminator
static int termin()
{
    int t = curr()->type;
    return t == T_SEMI || t == T_RPAREN || t == T_COMMA || t == T_RBRACK;
}

static const char *tokstrs[] =
{
    [T_EOF]     = "EOF",
    [T_PLUS]    = "+",
    [T_MINUS]   = "-",
    [T_STAR]    = "*",
    [T_SLASH]   = "/",
    [T_INTLIT]  = "int literal",
    [T_STRLIT]  = "string literal",
    [T_SEMI]    = ";",
    [T_COMMA]   = ",",
    [T_AMP]     = "&",
    [T_COLON]   = ":",
    [T_DOT]     = ".",
    [T_ARROW]   = "->",
    [T_IDENT]   = "identifer",
    [T_EQ]      = "=",
    [T_EQEQ]    = "==",
    [T_NEQ]     = "!=",
    [T_GT]      = ">",
    [T_LT]      = "<",
    [T_GTE]     = ">=",
    [T_LTE]     = "<=",
    [T_NOT]     = "!",
    [T_LAND]    = "&&",
    [T_LOR]     = "||",
    [T_BITOR]   = "|",
    [T_BITXOR]  = "^",
    [T_COMP]    = "~",
    [T_TERNARY] = "?",
    [T_INC]     = "++",
    [T_DEC]     = "--",
    [T_INT8]    = "int8",
    [T_INT16]   = "int16",
    [T_INT32]   = "int32",
    [T_INT64]   = "int64",
    [T_UINT8]   = "uint8",
    [T_UINT16]  = "uint16",
    [T_UINT32]  = "uint32",
    [T_UINT64]  = "uint64",
    [T_FLOAT32] = "float32",
    [T_FLOAT64] = "float64",
    [T_LPAREN]  = "(",
    [T_RPAREN]  = ")",
    [T_LBRACK]  = "[",
    [T_RBRACK]  = "]",
    [T_LBRACE]  = "{",
    [T_RBRACE]  = "}",
    [T_ASM]     = "asm",
    [T_RETURN]  = "return",
    [T_WHILE]   = "while",
    [T_IF]      = "if",
    [T_ELSE]    = "else",
    [T_FOR]     = "for",
    [T_FUNC]    = "fn",
    [T_VAR]     = "var",
    [T_SIZEOF]  = "sizeof",
    [T_GOTO]    = "goto",
    [T_LABEL]   = "label"
};

static int expectcurr(int t)
{
    if (curr()->type != t)
    {
        error("Expected '%s', got '%s'\n", tokstrs[t], tokstrs[curr()->type]);
        return -1;
    }
    return 0;
}

static int expect(int t)
{
    expectcurr(t);
    next();
    return 0;
}

static int operator(int tok)
{
    switch (tok)
    {
        case T_PLUS:  return OP_PLUS;
        case T_MINUS: return OP_MINUS;
        case T_STAR:  return OP_MUL;
        case T_SLASH: return OP_DIV;
        case T_EQ:    return OP_ASSIGN;
        case T_EQEQ:  return OP_EQUAL;
        case T_NEQ:   return OP_NEQUAL;
        case T_GT:    return OP_GT;
        case T_LT:    return OP_LT;
        case T_GTE:   return OP_GTE;
        case T_LTE:   return OP_LTE;
        case T_LAND:  return OP_LAND;
        case T_LOR:   return OP_LOR;
    }
    return -1;
}

static int istype(int token)
{
    return token == T_INT8  || token == T_INT16  || token == T_INT32  || token == T_INT64  || 
           token == T_UINT8 || token == T_UINT16 || token == T_UINT32 || token == T_UINT64 || 
           token == T_FLOAT32 || token == T_FLOAT64;
}

static struct type parsetype()
{
    struct type t = { 0 };

    switch (curr()->type)
    {
        case T_INT8:    t.name = TYPE_INT8; break;
        case T_INT16:   t.name = TYPE_INT16; break;
        case T_INT32:   t.name = TYPE_INT32; break;
        case T_INT64:   t.name = TYPE_INT64; break;
        case T_UINT8:   t.name = TYPE_UINT8; break;
        case T_UINT16:  t.name = TYPE_UINT16; break;
        case T_UINT32:  t.name = TYPE_UINT32; break;
        case T_UINT64:  t.name = TYPE_UINT64; break;
        case T_FLOAT32: t.name = TYPE_FLOAT32; break;
        case T_FLOAT64: t.name = TYPE_FLOAT64; break;
        case T_STAR:    t.name = TYPE_VOID; break;
        case T_FUNC: // Parse function signature
        {
            t.name = TYPE_FUNC;
            t.func.ret = calloc(1, sizeof(struct type));
            
            while (next()->type == T_STAR) t.ptr++;
            expect(T_LPAREN);
            
            while (curr()->type != T_RPAREN)
            {
                t.func.params[t.func.paramcnt] = calloc(1, sizeof(struct type));
                *t.func.params[t.func.paramcnt++] = parsetype();
                if (curr()->type != T_RPAREN) expect(T_COMMA);
            }

            next();
            if (curr()->type == T_ARROW)
            {
                expect(T_ARROW);

                *t.func.ret = parsetype();
            }
            goto array;
        }
        case T_IDENT:
        {
            // TODO: need to account for pointer and stuff. Should use a
            // base type with modifications to it
            for (unsigned int i = 0; i < s_typedefcnt; i++)
            {
                if (!strcmp(s_typedefs[i].name, curr()->v.sval))
                {
                    next();
                    return s_typedefs[i].type;
                }
            }
            goto error;
        }

        default:
        error:
            error("Expected type, got '%s'\n", tokstrs[curr()->type]);
    }

    while (next()->type == T_STAR) t.ptr++;

array:
    if (curr()->type == T_LBRACK)
    {
        next();
        t.arrlen = curr()->v.ival;
        expect(T_INTLIT);
        expect(T_RBRACK);
    }

    return t;
}

static int isintegral(struct type t)
{
    return t.name != TYPE_STRUCT && t.name != TYPE_UNION && t.name != TYPE_FUNC && !t.ptr && !t.arrlen;
}

static struct type mktype(int name, int arrlen, int ptr)
{
    return (struct type) { .name = name, .arrlen = arrlen, .ptr = ptr };
}

static int type_compatible(struct type t1, struct type t2)
{
    if (t1.ptr && t2.ptr) return 1;
    if (isintegral(t1) && isintegral(t2)) return 1;

    return 0;
}

static void add_typedef(const char *name, struct type type)
{
    s_typedefs = realloc(s_typedefs, (s_typedefcnt + 1) * sizeof(struct sym));
    s_typedefs[s_typedefcnt++] = (struct sym)
    {
        .name = strdup(name),
        .type = type
    };
}

static struct ast *binexpr();
static struct ast *primary();
static struct ast *pre();
static struct ast *post(struct ast *ast);

static struct ast *parenexpr()
{
    next();

    struct ast *ast;
    if (istype(curr()->type))
    {
        struct type t = parsetype();
        if (!isintegral(t) || t.arrlen || t.ptr)
            error("Cannot cast to non-integral type\n");

        expect(T_RPAREN);
        struct ast *val = pre();

        ast = mkast(A_CAST);
        ast->vtype = t;
        ast->cast.type = t;
        ast->cast.val  = val;
        return ast;
    }
    else
    {
        ast = binexpr(0);
    }

    return ast;
}

static struct ast *pre()
{
    struct ast *ast;

    switch (curr()->type)
    {
        case T_AMP: // TODO: type checking: must be an lvalue
            next();
            ast = mkunary(OP_ADDROF, pre());
            ast->vtype = ast->unary.val->vtype;
            ast->vtype.ptr++;
            return ast;
        case T_STAR:
            next();
            ast = mkunary(OP_DEREF, pre());
            if (!ast->unary.val->vtype.ptr)
                error("Cannot dereference non-pointer type.\n");
            ast->vtype = ast->unary.val->vtype;
            ast->vtype.ptr--;
            return ast;
        case T_NOT:
            next();
            ast = mkunary(OP_NOT, pre());
            ast->vtype = ast->unary.val->vtype;
            return ast;
        case T_MINUS:
            next();
            ast = mkunary(OP_MINUS, pre());
            ast->vtype = ast->unary.val->vtype;
            return ast;
        default:
            return post(primary());
    }
}

static struct ast *memaccess(struct ast *ast)
{
    int ptr = curr()->type == T_ARROW;
    
    struct ast *address;
    if (ptr)
    {
        //if (!ast->vtype.ptr)
            //error("Use of arrow operator '->' on non-pointer type.\n");
        address = ast;
    }
    else
    {
        //if (ast->vtype.ptr)
            //error("Use of dot operator '.' on pointer type\n");
        address            = mkast(A_UNARY);
        address->vtype     = mktype(TYPE_UINT64, 0, 0);
        address->unary.op  = OP_ADDROF;
        address->unary.val = ast;
    }

    struct type structype = ast->vtype;
    ast = address;

    struct structmem *member;
    while (curr()->type == T_DOT || curr()->type == T_ARROW)
    {
        if (structype.name != TYPE_STRUCT)
            error("Member access of non-struct type.\n");

        ptr = curr()->type == T_ARROW;

        if (ptr && !structype.ptr)
            error("Use of arrow operator '->' on non-pointer to struct. Use '.' instead\n");
        else if (!ptr && structype.ptr)
            error("Use of dot operator '.' on pointer to struct. Use '->' instead.\n");

        next();
        const char *name = curr()->v.sval;
        expect(T_IDENT);

        member = NULL;
        for (unsigned i = 0; i < ast->vtype.struc.memcnt; i++)
        {
            if (!strcmp(name, ast->vtype.struc.members[i].name))
            {
                member = &ast->vtype.struc.members[i];
                break;
            }
        }

        /*

        x.y.y = 10;
        *(&x + offsetof(y) + offsetof(y)) = 10;

        x->y.y = 10;
        *(x + offsetof(y, x) + offsetof(y, y)) = 10;

        */

        if (!member)
            error("Struct does not contain member '%s'\n", name);

        struct ast *offset  = mkast(A_INTLIT);
        offset->vtype       = ast->vtype;
        offset->intlit.ival = member->offset;

        struct ast *add     = mkast(A_BINOP);
        add->vtype          = offset->vtype;
        add->binop.op       = OP_PLUS;
        add->binop.lhs      = ast;
        add->binop.rhs      = offset;

        structype = member->type;
        ast = add;
    }

    struct ast *deref   = mkast(A_UNARY);
    deref->vtype        = member->type;
    deref->unary.op     = OP_DEREF;
    deref->unary.val    = ast;

    if (curr()->type == T_DOT)
        return memaccess(deref);

    return deref;
}

static struct ast *post(struct ast *ast)
{
    switch (curr()->type)
    {
        case T_LBRACK:
        {
            next();

            struct ast *binop = mkast(A_BINOP);
            binop->binop.op   = OP_PLUS;
            binop->binop.lhs  = ast;
            binop->binop.rhs  = pre();
            binop->vtype      = mktype(binop->binop.lhs->vtype.name, 0, 1);

            struct ast *access = mkunary(OP_DEREF, binop);
            access->vtype = binop->vtype;
            access->vtype.ptr--;
            expect(T_RBRACK);
            return access;
        }
        case T_LPAREN:
        {
            if (ast->vtype.name != TYPE_FUNC)
                error("Call of non-function or function-pointer type.\n");

            struct ast *call = mkast(A_CALL);

            if (ast->vtype.ptr)
                call->call.ast = ast;
            else
            {
                call->call.ast            = mkast(A_UNARY);
                call->call.ast->unary.op  = OP_ADDROF;
                call->call.ast->unary.val = ast;
            }

            call->vtype = *ast->vtype.func.ret;

            next();
            unsigned int i;
            for (i = 0; curr()->type != T_RPAREN; i++)
            {
                call->call.params = realloc(call->call.params, (ast->call.paramcnt + 1) * sizeof(struct ast*));
                call->call.params[call->call.paramcnt++] = binexpr(0);

                if (curr()->type != T_RPAREN) expect(T_COMMA);
            }
            expect(T_RPAREN);

            if (i < ast->vtype.func.paramcnt)
                error("Too few parameters in call to function\n");
            else if (i > ast->vtype.func.paramcnt)
                error("Too many parameters in call to function\n");

            return call;
        }
        case T_DOT:
        case T_ARROW: return memaccess(ast);
        default: return ast;
    }
}

static struct ast *intlit()
{
    struct ast *ast = mkast(A_INTLIT);

    if (curr()->v.ival < UINT32_MAX)
        ast->vtype = mktype(TYPE_UINT32, 0, 0);
    else
        ast->vtype = mktype(TYPE_UINT64, 0, 0);

    ast->intlit.ival = curr()->v.ival;
    return ast;
}

static struct ast *primary()
{
    struct ast *ast;

    switch (curr()->type)
    {
        case T_SIZEOF:
            ast = mkast(A_SIZEOF);
            ast->vtype = mktype(TYPE_UINT64, 0, 0);
            ast->sizeofop.t = parsetype();
            next();
            return ast;
        case T_INTLIT:
            ast = intlit();
            next();
            return ast;
        case T_STRLIT:
            ast = mkast(A_STRLIT);
            ast->vtype = mktype(TYPE_INT8, 0, 1); // int8*
            ast->strlit.str = strdup(curr()->v.sval);
            next();
            return ast;
        case T_LPAREN: return parenexpr();

        case T_IDENT:
        {
            char *name = curr()->v.sval;

            struct sym *sym = sym_lookup(s_parser.currscope, name);
            if (!sym)
                error("Use of undeclared symbol '%s'\n", name);
            
            next();
            ast             = mkast(A_IDENT);
            ast->vtype      = sym->type;
            ast->ident.name = strdup(name);
            return ast;
        }

        default:
            error("Expected primary expression, got '%s'\n", tokstrs[curr()->type]);
    }

    return NULL;
}

static int rightassoc(int op)
{
    return op == OP_ASSIGN;
}

static int opprec[] =
{
    [OP_ASSIGN] = 1,
    [OP_LOR]    = 2,
    [OP_LAND]   = 3,
    [OP_PLUS]   = 4,
    [OP_MINUS]  = 4,
    [OP_MUL]    = 5,
    [OP_DIV]    = 5,
    [OP_EQUAL]  = 6,
    [OP_NEQUAL] = 6,
    [OP_LT]     = 7,
    [OP_GT]     = 7,
    [OP_LTE]    = 7,
    [OP_GTE]    = 7,
};

static struct ast *binexpr(int ptp)
{
    struct ast *rhs;
    struct ast *lhs = pre();

    if (termin()) return lhs;

    int op;
    if ((op = operator(curr()->type)) == -1)
        error("Expected operator, got '%s'\n", tokstrs[curr()->type]);

    while (opprec[op] > ptp || (rightassoc(op) && opprec[op] == ptp))
    {
        next();
        rhs = binexpr(opprec[op]);

        struct ast *expr = mkast(A_BINOP);
        expr->binop.lhs = lhs;
        expr->binop.rhs = rhs;
        expr->binop.op  = op;

        if (!type_compatible(lhs->vtype, rhs->vtype))
            error("Incompatible types in binary expression.\n");

        expr->vtype = lhs->vtype; // TODO: weak type conversion

        lhs->lvalue = 1;
        rhs->lvalue = 0;

        lhs = expr;
        if (termin()) return lhs;

        if ((op = operator(curr()->type)) == -1)
            error("Expected operator, got '%s'\n", tokstrs[curr()->type]);
    }

    return lhs;
}

static struct ast *inlineasm()
{
    struct ast *ast = mkast(A_ASM);
    ast->inasm.code = strdup(curr()->v.sval);
    next();
    return ast;
}

static struct ast *block();
static struct ast *vardecl();

static struct ast *funcdecl()
{
    struct sym sym = { 0 };
    sym.type.name = TYPE_FUNC;
    sym.type.func.ret = calloc(1, sizeof(struct type));

    if (curr()->type == T_PUBLIC)
    {
        sym.attr |= SYM_PUBLIC;
        next();
    }

    if (curr()->type == T_EXTERN)
    {
        sym.attr |= SYM_EXTERN;
        next();
    }

    struct ast *ast = mkast(A_FUNCDEF);
    ast->funcdef.name = strdup(curr()->v.sval);
    sym.name = strdup(ast->funcdef.name);    

    expect(T_IDENT);
    expect(T_LPAREN);

    while (curr()->type != T_RPAREN)
    {
        next();
        expect(T_COLON);
        sym.type.func.params[sym.type.func.paramcnt] = calloc(1, sizeof(struct type));
        *sym.type.func.params[sym.type.func.paramcnt++] = parsetype();
        if (curr()->type != T_RPAREN) expect(T_COMMA);
    }
    
    expect(T_RPAREN);

    if (curr()->type == T_ARROW)
    {
        next();
        *sym.type.func.ret = parsetype();
    }
    else *sym.type.func.ret = (struct type) { .name = TYPE_VOID };

    struct sym *prev = sym_lookup(s_parser.currscope, sym.name);
    if (prev)
    {
        if (!(sym.attr & SYM_EXTERN) && !(prev->attr & SYM_EXTERN))
            error("Multiple definition of function '%s'\n", sym.name);
    }

    sym_putglob(s_parser.currscope, &sym);

    if (curr()->type == T_LBRACE)
    {
        if (sym.attr & SYM_EXTERN)
            error("Definition of function marked 'extern'.\n");

        s_parser.currfunc = ast;
        
        next();
        ast->funcdef.block = block(SYMTAB_FUNC);
        expect(T_RBRACE);
    
        s_parser.currfunc = NULL;
    }
    else
    {
        if (!(sym.attr & SYM_EXTERN))
            error("No definition of function not marked 'extern'\n");

        expect(T_SEMI);
        free(ast);
        return NULL;
    }

    return ast;
}

static struct ast *vardecl()
{
    const char *name = curr()->v.sval;
    expect(T_IDENT);
    struct type t;
    struct ast *ast;
    int autov = 0; // Auto type

    if (curr()->type != T_COLON)
    {
        if (curr()->type != T_EQ)
            error("Auto variable must be initialized.\n");
        autov = 1;
    }
    else
    {
        expect(T_COLON);
        t = parsetype();
        ast = mkast(A_VARDEF);
    }

    if (curr()->type == T_EQ)
    {
        expect(T_EQ);
        struct ast *init = binexpr(0);

        ast = mkast(A_BINOP);
        ast->binop.op = OP_ASSIGN;
        ast->binop.rhs = init;
        ast->binop.lhs = mkast(A_IDENT);
        ast->binop.lhs->ident.name = strdup(name);

        ast->binop.rhs->lvalue = 0;
        ast->binop.lhs->lvalue = 1;

        if (autov)
            t = ast->binop.rhs->vtype;
        else
        {
            if (!type_compatible(ast->binop.rhs->vtype, t))
                error("Incompatible types in variable initialization\n");
        }
    }

    sym_put(s_parser.currscope, name, t, 0);
    return ast;
}

static struct ast *struct_declaration()
{
    struct type struc = mktype(TYPE_STRUCT, 0, 0);

    expect(T_STRUCT);
    const char *name = curr()->v.sval;
    expect(T_IDENT);
    expect(T_LBRACE);

    size_t offset = 0;
    while (curr()->type != T_RBRACE)
    {
        const char *memname = curr()->v.sval;
        expect(T_IDENT);
        
        expect(T_COLON);
        struct type type = parsetype();

        struc.struc.members = realloc(struc.struc.members, (struc.struc.memcnt + 1) * sizeof(struct structmem));
        struc.struc.members[struc.struc.memcnt++] = (struct structmem)
        {
            .name = strdup(memname),
            .type = type,
            .offset = offset
        };

        offset += asm_sizeof(type);

        if (curr()->type != T_RBRACE)
            expect(T_COMMA);
    }

    struc.struc.size = offset;

    expect(T_RBRACE);
    expect(T_SEMI);

    add_typedef(name, struc);
    return NULL;
}

static struct ast *return_statement()
{
    next();
    struct ast *ast = mkast(A_RETURN);
    ast->ret.func   = s_parser.currfunc;
    
    if (curr()->type != T_SEMI)
    {
        struct sym *sym = sym_lookup(s_parser.currscope, ast->ret.func->funcdef.name);
        struct type t = *sym->type.func.ret;

        if (t.name == TYPE_VOID && !t.ptr)
            error("Returning value from void function.\n");
        
        ast->ret.val = binexpr(0);

        if (!type_compatible(ast->ret.val->vtype, t))
            error("Incompatible return type in function '%s'.\n", sym->name);
    }

    return ast;
}

static struct ast *if_statement()
{
    next();
    expect(T_LPAREN);

    struct ast *ast = calloc(1, sizeof(struct ast));
    ast->type = A_IFELSE;

    ast->ifelse.cond = binexpr(0);
    expect(T_RPAREN);

    expect(T_LBRACE);
    ast->ifelse.ifblock = block(SYMTAB_BLOCK);
    expect(T_RBRACE);

    if (curr()->type == T_ELSE)
    {
        next();
        expect(T_LBRACE);
        ast->ifelse.elseblock = block(SYMTAB_BLOCK);
        expect(T_RBRACE);
    }

    return ast;
}

static struct ast *while_statement()
{
    next();
    expect(T_LPAREN);

    struct ast *ast = mkast(A_WHILE);

    ast->whileloop.cond = binexpr(0);
    expect(T_RPAREN);

    expect(T_LBRACE);
    ast->whileloop.body = block(SYMTAB_BLOCK);
    expect(T_RBRACE);

    return ast;
}

static struct ast *statement();

static struct ast *for_statement()
{
    next();
    expect(T_LPAREN);

    struct ast *ast = mkast(A_FOR);

    ast->forloop.init = statement();
    expect(T_SEMI);

    ast->forloop.cond = binexpr(0);
    expect(T_SEMI);

    ast->forloop.update = statement();
    expect(T_RPAREN);

    expect(T_LBRACE);
    ast->forloop.body = block(SYMTAB_BLOCK);
    expect(T_RBRACE);

    return ast;
}

static struct ast *label()
{
    expect(T_LABEL);
    struct ast *ast = mkast(A_LABEL);
    ast->label.name = strdup(curr()->v.sval);
    
    next();
    expect(T_COLON);
    return ast;
}

static struct ast *gotolbl()
{
    expect(T_GOTO);
    struct ast *ast = mkast(A_GOTO);
    ast->gotolbl.label = strdup(curr()->v.sval);

    next();
    return ast;
}

static struct ast *typedef_statement()
{
    expect(T_TYPEDEF);

    const char *name = curr()->v.sval;
    
    expect(T_IDENT);
    expect(T_EQ);

    struct type type = parsetype();

    add_typedef(name, type);

    expect(T_SEMI);
    return NULL;
}

static struct ast *statement()
{
    struct ast *ast;

    if (curr()->type == T_ASM)
        ast = inlineasm();
    else if (curr()->type == T_FUNC)
    {
        next();
        ast = funcdecl();
    }
    else if (curr()->type == T_VAR)
    {
        next();
        ast = vardecl();
    }
    else if (curr()->type == T_RETURN)
        ast = return_statement();
    else if (curr()->type == T_IF)
        ast = if_statement();
    else if (curr()->type == T_WHILE)
        ast = while_statement();
    else if (curr()->type == T_FOR)
        ast = for_statement();
    else if (curr()->type == T_LABEL)
        ast = label();
    else if (curr()->type == T_GOTO)
        ast = gotolbl();
    else if (curr()->type == T_TYPEDEF)
        ast = typedef_statement();
    else if (curr()->type == T_STRUCT)
        ast = struct_declaration();
    else
        ast = binexpr(0);
    
    return ast;
}

static struct ast *block(int type)
{
    struct ast *block = mkast(A_BLOCK);
    block->block.symtab.type = type;

    block->block.symtab.parent = s_parser.currscope;
    s_parser.currscope = &block->block.symtab;

    while (curr()->type != T_RBRACE && curr()->type != T_EOF)
    {
        struct ast *ast = statement();
        if (!ast) continue; // Could be a declaration - TODO: distinguish global block from function blocks

        if (ast->type != A_FUNCDEF && ast->type != A_ASM && ast->type != A_IFELSE
            && ast->type != A_FOR && ast->type != A_WHILE && ast->type != A_LABEL)
            expect(T_SEMI);

        block->block.statements = realloc(block->block.statements, ++block->block.cnt * sizeof(struct ast*));
        block->block.statements[block->block.cnt - 1] = ast;
    }
    
    s_parser.currscope = s_parser.currscope->parent;
    return block;
}

int parse(struct token *toks, struct ast *ast)
{
    s_parser.toks      = toks;
    s_parser.i         = 0;
    s_parser.currscope = NULL;
    
    struct ast *tree = block(SYMTAB_GLOB);
    memcpy(ast, tree, sizeof(struct ast));

    return 0;
}
