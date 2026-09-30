// jvm_mod.c - Java 26 SE 解释器模块 (完整 Java 子集: 词法→语法→求值)
#include "mod_abi.h"

extern void* memset(void* d, int v, size_t n);

__attribute__((section(".modhdr"), used))
static const mod_header_t g_mod_hdr = {
    MOD_MAGIC, MOD_ABI_VERSION, MOD_HDRSZ, 0, 0, 0, {0}
};

static void iprint(const mod_syscalls_t* sc, long v) {
    char buf[24]; int i = 0;
    if (v < 0) { sc->putc('-'); v = -v; }
    if (v == 0) buf[i++] = '0';
    else { char t[24]; int n = 0; while (v) { t[n++] = '0' + (v % 10); v /= 10; } while (n) buf[i++] = t[--n]; }
    buf[i] = 0;
    sc->puts(buf);
}

// ============================================================
// Token
// ============================================================
typedef enum {
    TK_EOF, TK_NUM, TK_ID, TK_STR,
    TK_KW_CLASS, TK_KW_PUBLIC, TK_KW_STATIC, TK_KW_VOID, TK_KW_INT,
    TK_KW_IF, TK_KW_ELSE, TK_KW_WHILE, TK_KW_FOR, TK_KW_RETURN, TK_KW_STRING,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_ASSIGN, TK_EQ, TK_NE, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_AND, TK_OR, TK_NOT,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_SEMI, TK_COMMA, TK_DOT,
    TK_LBRACKET, TK_RBRACKET
} TokKind;

typedef struct { TokKind k; long ival; const char* s; int slen; int line; } Tok;

#define MAX_TOK 1024
typedef struct { Tok toks[MAX_TOK]; int n; } TokArr;

// ============================================================
// AST
// ============================================================
typedef enum {
    NK_METHOD, NK_VARDECL, NK_IF, NK_WHILE, NK_FOR, NK_RETURN, NK_BLOCK, NK_EXPRSTMT,
    NK_NUM, NK_STR, NK_ID, NK_ASSIGN, NK_CALL, NK_BINOP, NK_UNOP, NK_PRINT
} NodeKind;

typedef struct Node {
    NodeKind kind;
    long ival;
    int line;
    const char* name; int namelen;
    const char* str; int slen;
    struct Node *a, *b, *c, *d, *next;
} Node;

#define MAX_NODES 512
typedef struct { Node pool[MAX_NODES]; int used; } NodePool;

static Node* nk(NodePool* p, NodeKind k, int line) {
    if (p->used >= MAX_NODES) return 0;
    Node* n = &p->pool[p->used++];
    n->kind = k; n->ival = 0; n->line = line; n->name = 0; n->namelen = 0; n->str = 0; n->slen = 0;
    n->a = n->b = n->c = n->d = n->next = 0;
    return n;
}

// ============================================================
// Lexer
// ============================================================
static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alnum(int c) { return is_alpha(c) || is_digit(c); }

static TokKind kw_lookup(const char* s, int len) {
    if (len==5 && s[0]=='c'&&s[1]=='l'&&s[2]=='a'&&s[3]=='s'&&s[4]=='s') return TK_KW_CLASS;
    if (len==6 && s[0]=='p'&&s[1]=='u'&&s[2]=='b'&&s[3]=='l'&&s[4]=='i'&&s[5]=='c') return TK_KW_PUBLIC;
    if (len==6 && s[0]=='s'&&s[1]=='t'&&s[2]=='a'&&s[3]=='t'&&s[4]=='i'&&s[5]=='c') return TK_KW_STATIC;
    if (len==4 && s[0]=='v'&&s[1]=='o'&&s[2]=='i'&&s[3]=='d') return TK_KW_VOID;
    if (len==3 && s[0]=='i'&&s[1]=='n'&&s[2]=='t') return TK_KW_INT;
    if (len==2 && s[0]=='i'&&s[1]=='f') return TK_KW_IF;
    if (len==4 && s[0]=='e'&&s[1]=='l'&&s[2]=='s'&&s[3]=='e') return TK_KW_ELSE;
    if (len==5 && s[0]=='w'&&s[1]=='h'&&s[2]=='i'&&s[3]=='l'&&s[4]=='e') return TK_KW_WHILE;
    if (len==3 && s[0]=='f'&&s[1]=='o'&&s[2]=='r') return TK_KW_FOR;
    if (len==6 && s[0]=='r'&&s[1]=='e'&&s[2]=='t'&&s[3]=='u'&&s[4]=='r'&&s[5]=='n') return TK_KW_RETURN;
    if (len==6 && s[0]=='S'&&s[1]=='t'&&s[2]=='r'&&s[3]=='i'&&s[4]=='n'&&s[5]=='g') return TK_KW_STRING;
    return TK_ID;
}

static int lex(const mod_syscalls_t* sc, const char* src, TokArr* ta, int* err_line, const char** err_msg) {
    const char* p = src; int line = 1; ta->n = 0;
    while (*p) {
        if (*p == '\n') { line++; p++; continue; }
        if (*p == ' ' || *p == '\t' || *p == '\r') { p++; continue; }
        if (p[0]=='/' && p[1]=='/') { while (*p && *p!='\n') p++; continue; }
        if (p[0]=='/' && p[1]=='*') { p+=2; while (*p && !(p[0]=='*'&&p[1]=='/')) { if (*p=='\n') line++; p++; } if (*p) p+=2; continue; }
        if (*p == '"') {
            p++; const char* start = p;
            while (*p && *p != '"') { if (*p == '\\') p++; p++; }
            if (*p != '"') { *err_line = line; *err_msg = "unterminated string"; return -1; }
            int slen = (int)(p - start);
            p++;
            if (ta->n >= MAX_TOK) { *err_line = line; *err_msg = "too many tokens"; return -1; }
            ta->toks[ta->n].k = TK_STR; ta->toks[ta->n].s = start; ta->toks[ta->n].slen = slen; ta->toks[ta->n].line = line; ta->n++;
            continue;
        }
        if (is_alpha(*p)) {
            const char* start = p; while (is_alnum(*p)) p++; int len = (int)(p - start);
            TokKind k = kw_lookup(start, len);
            if (ta->n >= MAX_TOK) { *err_line = line; *err_msg = "too many tokens"; return -1; }
            ta->toks[ta->n].k = k; ta->toks[ta->n].s = start; ta->toks[ta->n].slen = len; ta->toks[ta->n].line = line; ta->n++;
            continue;
        }
        if (is_digit(*p)) {
            long v = 0; while (is_digit(*p)) { v = v*10 + (*p - '0'); p++; }
            if (ta->n >= MAX_TOK) { *err_line = line; *err_msg = "too many tokens"; return -1; }
            ta->toks[ta->n].k = TK_NUM; ta->toks[ta->n].ival = v; ta->toks[ta->n].line = line; ta->n++;
            continue;
        }
        TokKind k = TK_EOF; int adv = 1;
        switch (*p) {
            case '+': k=TK_PLUS; break; case '-': k=TK_MINUS; break;
            case '*': k=TK_STAR; break; case '/': k=TK_SLASH; break;
            case '%': k=TK_PERCENT; break;
            case '(': k=TK_LPAREN; break; case ')': k=TK_RPAREN; break;
            case '{': k=TK_LBRACE; break; case '}': k=TK_RBRACE; break;
            case ';': k=TK_SEMI; break; case ',': k=TK_COMMA; break;
            case '.': k=TK_DOT; break;
            case '[': k=TK_LBRACKET; break; case ']': k=TK_RBRACKET; break;
            case '=': if (p[1]=='=') { k=TK_EQ; adv=2; } else k=TK_ASSIGN; break;
            case '!': if (p[1]=='=') { k=TK_NE; adv=2; } else k=TK_NOT; break;
            case '<': if (p[1]=='=') { k=TK_LE; adv=2; } else k=TK_LT; break;
            case '>': if (p[1]=='=') { k=TK_GE; adv=2; } else k=TK_GT; break;
            case '&': if (p[1]=='&') { k=TK_AND; adv=2; } else { *err_line=line; *err_msg="unexpected '&'"; return -1; } break;
            case '|': if (p[1]=='|') { k=TK_OR; adv=2; } else { *err_line=line; *err_msg="unexpected '|'"; return -1; } break;
            default: { *err_line = line; *err_msg = "illegal character"; return -1; }
        }
        if (ta->n >= MAX_TOK) { *err_line = line; *err_msg = "too many tokens"; return -1; }
        ta->toks[ta->n].k = k; ta->toks[ta->n].line = line; ta->n++;
        p += adv;
    }
    ta->toks[ta->n].k = TK_EOF; ta->toks[ta->n].line = line; ta->n++;
    return 0;
}

// ============================================================
// Parser
// ============================================================
typedef struct { TokArr* ta; int pos; NodePool* np; int err; int err_line; const char* err_msg; } Parser;

static Tok* cur(Parser* ps) { return &ps->ta->toks[ps->pos]; }
static void advance(Parser* ps) { ps->pos++; }
static int accept(Parser* ps, TokKind k) { if (cur(ps)->k == k) { advance(ps); return 1; } return 0; }
static void expect(Parser* ps, TokKind k, const char* what) {
    if (cur(ps)->k != k) { ps->err = 1; ps->err_line = cur(ps)->line; ps->err_msg = what; } else advance(ps);
}

static Node* parse_expr(Parser* ps);
static Node* parse_block(Parser* ps);
static Node* parse_stmt(Parser* ps);

static Node* parse_primary(Parser* ps) {
    Tok* t = cur(ps);
    if (t->k == TK_NUM) { advance(ps); Node* n = nk(ps->np, NK_NUM, t->line); n->ival = t->ival; return n; }
    if (t->k == TK_STR) { advance(ps); Node* n = nk(ps->np, NK_STR, t->line); n->str = t->s; n->slen = t->slen; return n; }
    if (t->k == TK_ID) {
        if (t->slen==6 && t->s[0]=='S'&&t->s[1]=='y'&&t->s[2]=='s'&&t->s[3]=='t'&&t->s[4]=='e'&&t->s[5]=='m') {
            advance(ps);
            expect(ps, TK_DOT, "expected '.'");
            Tok* out = cur(ps); expect(ps, TK_ID, "expected 'out'");
            expect(ps, TK_DOT, "expected '.'");
            Tok* m = cur(ps); expect(ps, TK_ID, "expected println/print");
            int is_println = (m->slen==7 && m->s[0]=='p'&&m->s[1]=='r'&&m->s[2]=='i'&&m->s[3]=='n'&&m->s[4]=='t'&&m->s[5]=='l'&&m->s[6]=='n');
            expect(ps, TK_LPAREN, "expected '('");
            Node* n = nk(ps->np, NK_PRINT, t->line); n->ival = is_println;
            if (cur(ps)->k != TK_RPAREN) n->a = parse_expr(ps);
            expect(ps, TK_RPAREN, "expected ')'");
            return n;
        }
        advance(ps);
        if (cur(ps)->k == TK_LPAREN) {
            advance(ps);
            Node* n = nk(ps->np, NK_CALL, t->line); n->name = t->s; n->namelen = t->slen;
            Node* last = 0;
            if (cur(ps)->k != TK_RPAREN) {
                for (;;) {
                    Node* arg = parse_expr(ps);
                    if (!last) n->a = arg; else last->next = arg; last = arg;
                    if (!accept(ps, TK_COMMA)) break;
                }
            }
            expect(ps, TK_RPAREN, "expected ')'");
            return n;
        }
        Node* n = nk(ps->np, NK_ID, t->line); n->name = t->s; n->namelen = t->slen; return n;
    }
    if (accept(ps, TK_LPAREN)) { Node* e = parse_expr(ps); expect(ps, TK_RPAREN, "expected ')'"); return e; }
    ps->err = 1; ps->err_line = t->line; ps->err_msg = "expected expression"; return 0;
}

static Node* parse_unary(Parser* ps) {
    Tok* t = cur(ps);
    if (t->k == TK_MINUS) { advance(ps); Node* n = nk(ps->np, NK_UNOP, t->line); n->ival = TK_MINUS; n->a = parse_unary(ps); return n; }
    if (t->k == TK_NOT) { advance(ps); Node* n = nk(ps->np, NK_UNOP, t->line); n->ival = TK_NOT; n->a = parse_unary(ps); return n; }
    return parse_primary(ps);
}

static Node* parse_mul(Parser* ps) {
    Node* l = parse_unary(ps);
    while (!ps->err && (cur(ps)->k==TK_STAR||cur(ps)->k==TK_SLASH||cur(ps)->k==TK_PERCENT)) {
        Tok* t=cur(ps); advance(ps); Node* r=parse_unary(ps); Node* n=nk(ps->np,NK_BINOP,t->line); n->ival=t->k; n->a=l; n->b=r; l=n;
    }
    return l;
}

static Node* parse_add(Parser* ps) {
    Node* l = parse_mul(ps);
    while (!ps->err && (cur(ps)->k==TK_PLUS||cur(ps)->k==TK_MINUS)) {
        Tok* t=cur(ps); advance(ps); Node* r=parse_mul(ps); Node* n=nk(ps->np,NK_BINOP,t->line); n->ival=t->k; n->a=l; n->b=r; l=n;
    }
    return l;
}

static Node* parse_cmp(Parser* ps) {
    Node* l = parse_add(ps);
    while (!ps->err && (cur(ps)->k==TK_LT||cur(ps)->k==TK_GT||cur(ps)->k==TK_LE||cur(ps)->k==TK_GE)) {
        Tok* t=cur(ps); advance(ps); Node* r=parse_add(ps); Node* n=nk(ps->np,NK_BINOP,t->line); n->ival=t->k; n->a=l; n->b=r; l=n;
    }
    return l;
}

static Node* parse_eq(Parser* ps) {
    Node* l = parse_cmp(ps);
    while (!ps->err && (cur(ps)->k==TK_EQ||cur(ps)->k==TK_NE)) {
        Tok* t=cur(ps); advance(ps); Node* r=parse_cmp(ps); Node* n=nk(ps->np,NK_BINOP,t->line); n->ival=t->k; n->a=l; n->b=r; l=n;
    }
    return l;
}

static Node* parse_and(Parser* ps) {
    Node* l = parse_eq(ps);
    while (!ps->err && cur(ps)->k==TK_AND) { Tok* t=cur(ps); advance(ps); Node* r=parse_eq(ps); Node* n=nk(ps->np,NK_BINOP,t->line); n->ival=TK_AND; n->a=l; n->b=r; l=n; }
    return l;
}

static Node* parse_or(Parser* ps) {
    Node* l = parse_and(ps);
    while (!ps->err && cur(ps)->k==TK_OR) { Tok* t=cur(ps); advance(ps); Node* r=parse_and(ps); Node* n=nk(ps->np,NK_BINOP,t->line); n->ival=TK_OR; n->a=l; n->b=r; l=n; }
    return l;
}

static Node* parse_expr(Parser* ps) {
    Node* l = parse_or(ps);
    if (!ps->err && cur(ps)->k == TK_ASSIGN) {
        Tok* t=cur(ps); advance(ps); Node* r=parse_expr(ps); Node* n=nk(ps->np,NK_ASSIGN,t->line); n->a=l; n->b=r; return n;
    }
    return l;
}

static Node* parse_stmt(Parser* ps) {
    Tok* t = cur(ps);
    if (t->k == TK_KW_INT) {
        advance(ps); Tok* id=cur(ps); expect(ps, TK_ID, "expected variable name");
        Node* n = nk(ps->np, NK_VARDECL, id->line); n->name=id->s; n->namelen=id->slen;
        if (accept(ps, TK_ASSIGN)) n->a = parse_expr(ps);
        expect(ps, TK_SEMI, "expected ';'");
        return n;
    }
    if (t->k == TK_KW_IF) {
        advance(ps); expect(ps, TK_LPAREN, "expected '('"); Node* cond=parse_expr(ps); expect(ps, TK_RPAREN, "expected ')'");
        Node* n = nk(ps->np, NK_IF, t->line); n->a=cond; n->b=parse_stmt(ps);
        if (accept(ps, TK_KW_ELSE)) n->c = parse_stmt(ps);
        return n;
    }
    if (t->k == TK_KW_WHILE) {
        advance(ps); expect(ps, TK_LPAREN, "expected '('"); Node* cond=parse_expr(ps); expect(ps, TK_RPAREN, "expected ')'");
        Node* n = nk(ps->np, NK_WHILE, t->line); n->a=cond; n->b=parse_stmt(ps); return n;
    }
    if (t->k == TK_KW_FOR) {
        advance(ps); expect(ps, TK_LPAREN, "expected '('");
        Node* n = nk(ps->np, NK_FOR, t->line);
        if (cur(ps)->k != TK_SEMI) n->a = parse_stmt(ps); else { advance(ps); n->a = 0; }
        if (cur(ps)->k != TK_SEMI) n->b = parse_expr(ps); expect(ps, TK_SEMI, "expected ';'");
        if (cur(ps)->k != TK_RPAREN) n->c = parse_expr(ps); expect(ps, TK_RPAREN, "expected ')'");
        n->d = parse_stmt(ps); return n;
    }
    if (t->k == TK_KW_RETURN) {
        advance(ps); Node* n = nk(ps->np, NK_RETURN, t->line);
        if (cur(ps)->k != TK_SEMI) n->a = parse_expr(ps);
        expect(ps, TK_SEMI, "expected ';'");
        return n;
    }
    if (t->k == TK_LBRACE) return parse_block(ps);
    { Node* e = parse_expr(ps); expect(ps, TK_SEMI, "expected ';'"); Node* n = nk(ps->np, NK_EXPRSTMT, t->line); n->a = e; return n; }
}

static Node* parse_block(Parser* ps) {
    Tok* t = cur(ps); expect(ps, TK_LBRACE, "expected '{'");
    Node* blk = nk(ps->np, NK_BLOCK, t->line); Node* last = 0;
    while (!ps->err && cur(ps)->k != TK_RBRACE && cur(ps)->k != TK_EOF) {
        Node* s = parse_stmt(ps);
        if (!last) blk->a = s; else last->next = s; last = s;
    }
    expect(ps, TK_RBRACE, "expected '}'");
    return blk;
}

static Node* parse_method(Parser* ps) {
    Tok* t = cur(ps);
    accept(ps, TK_KW_PUBLIC); accept(ps, TK_KW_STATIC);
    accept(ps, TK_KW_VOID); accept(ps, TK_KW_INT);
    Tok* id = cur(ps); expect(ps, TK_ID, "expected method name");
    Node* m = nk(ps->np, NK_METHOD, id->line); m->name=id->s; m->namelen=id->slen;
    expect(ps, TK_LPAREN, "expected '('");
    Node* last = 0;
    if (cur(ps)->k != TK_RPAREN) {
        for (;;) {
            if (cur(ps)->k == TK_KW_STRING) { advance(ps); expect(ps, TK_LBRACKET, "expected '['"); expect(ps, TK_RBRACKET, "expected ']'"); expect(ps, TK_ID, "expected parameter name"); }
            else { accept(ps, TK_KW_INT); Tok* pid=cur(ps); expect(ps, TK_ID, "expected parameter name"); Node* param=nk(ps->np,NK_VARDECL,pid->line); param->name=pid->s; param->namelen=pid->slen; if (!last) m->a=param; else last->next=param; last=param; }
            if (!accept(ps, TK_COMMA)) break;
        }
    }
    expect(ps, TK_RPAREN, "expected ')'");
    m->b = parse_block(ps);
    return m;
}

static Node* parse_program(Parser* ps) {
    Node* prog = nk(ps->np, NK_BLOCK, 1); Node* last = 0;
    expect(ps, TK_KW_CLASS, "expected 'class'");
    expect(ps, TK_ID, "expected class name");
    expect(ps, TK_LBRACE, "expected '{'");
    while (!ps->err && cur(ps)->k != TK_RBRACE && cur(ps)->k != TK_EOF) {
        Node* m = parse_method(ps);
        if (!last) prog->a = m; else last->next = m; last = m;
    }
    expect(ps, TK_RBRACE, "expected '}'");
    return prog;
}

// ============================================================
// Evaluator
// ============================================================
#define MAX_VARS 64
#define MAX_FUNCS 16
#define MAX_DEPTH 64

typedef struct { const char* name; int namelen; long val; } Var;
typedef struct { const char* name; int namelen; Node* fn; } FuncSlot;

typedef struct {
    const mod_syscalls_t* sc;
    FuncSlot funcs[MAX_FUNCS]; int nfuncs;
    Var vars[MAX_VARS]; int nvars;
    int err; int err_line; const char* err_msg;
    int depth;
} Eval;

static int name_eq(const char* a, int alen, const char* b, int blen) {
    if (alen != blen) return 0;
    for (int i = 0; i < alen; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void eval_err(Eval* ev, int line, const char* msg) { ev->err = 1; ev->err_line = line; ev->err_msg = msg; }

static long eval_node(Eval* ev, Node* n);

static long eval_call(Eval* ev, Node* n, int line) {
    for (int i = 0; i < ev->nfuncs; i++) {
        if (name_eq(ev->funcs[i].name, ev->funcs[i].namelen, n->name, n->namelen)) {
            Node* fn = ev->funcs[i].fn;
            if (ev->depth >= MAX_DEPTH) { eval_err(ev, line, "call stack overflow"); return 0; }
            ev->depth++;
            int saved_nvars = ev->nvars;
            Node* param = fn->a; Node* arg = n->a;
            while (param && arg) {
                long av = eval_node(ev, arg); if (ev->err) { ev->depth--; return 0; }
                if (ev->nvars >= MAX_VARS) { eval_err(ev, line, "too many variables"); ev->depth--; return 0; }
                ev->vars[ev->nvars].name=param->name; ev->vars[ev->nvars].namelen=param->namelen; ev->vars[ev->nvars].val=av; ev->nvars++;
                param = param->next; arg = arg->next;
            }
            long ret = 0; int has_ret = 0;
            Node* stmt = fn->b ? fn->b->a : 0;
            while (stmt && !ev->err && !has_ret) {
                if (stmt->kind == NK_RETURN) { ret = stmt->a ? eval_node(ev, stmt->a) : 0; has_ret = 1; }
                else if (stmt->kind == NK_IF) {
                    long c = eval_node(ev, stmt->a); if (ev->err) break;
                    Node* branch = c ? stmt->b : stmt->c;
                    if (branch) { if (branch->kind==NK_RETURN) { ret=branch->a?eval_node(ev,branch->a):0; has_ret=1; } else eval_node(ev, branch); }
                }
                else if (stmt->kind == NK_WHILE) {
                    while (!ev->err) { long c=eval_node(ev,stmt->a); if (ev->err||!c) break; if (stmt->b->kind==NK_RETURN) { ret=stmt->b->a?eval_node(ev,stmt->b->a):0; has_ret=1; break; } eval_node(ev,stmt->b); }
                }
                else if (stmt->kind == NK_FOR) {
                    if (stmt->a) eval_node(ev, stmt->a);
                    while (!ev->err && !has_ret) { if (stmt->b) { long c=eval_node(ev,stmt->b); if (ev->err||!c) break; } if (stmt->d->kind==NK_RETURN) { ret=stmt->d->a?eval_node(ev,stmt->d->a):0; has_ret=1; break; } eval_node(ev,stmt->d); if (stmt->c) eval_node(ev,stmt->c); }
                }
                else eval_node(ev, stmt);
                stmt = stmt->next;
            }
            ev->nvars = saved_nvars; ev->depth--;
            return ret;
        }
    }
    eval_err(ev, line, "undefined method"); return 0;
}

static long eval_node(Eval* ev, Node* n) {
    if (!n || ev->err) return 0;
    if (ev->sc->poll && ev->sc->poll() < 0) {
        eval_err(ev, n->line, "execution timeout");
        return 0;
    }
    switch (n->kind) {
        case NK_NUM: return n->ival;
        case NK_STR: { for (int i = 0; i < n->slen; i++) ev->sc->putc(n->str[i]); return 0; }
        case NK_ID:
            for (int i = ev->nvars - 1; i >= 0; i--)
                if (name_eq(ev->vars[i].name, ev->vars[i].namelen, n->name, n->namelen)) return ev->vars[i].val;
            eval_err(ev, n->line, "undefined variable"); return 0;
        case NK_ASSIGN: {
            long v = eval_node(ev, n->b); if (ev->err) return 0;
            for (int i = ev->nvars - 1; i >= 0; i--)
                if (name_eq(ev->vars[i].name, ev->vars[i].namelen, n->a->name, n->a->namelen)) { ev->vars[i].val = v; return v; }
            eval_err(ev, n->line, "assign to undefined variable"); return 0;
        }
        case NK_BINOP: {
            long l = eval_node(ev, n->a); if (ev->err) return 0;
            long r = eval_node(ev, n->b); if (ev->err) return 0;
            switch (n->ival) {
                case TK_PLUS: return l+r; case TK_MINUS: return l-r; case TK_STAR: return l*r;
                case TK_SLASH: if (r==0) { eval_err(ev,n->line,"division by zero"); return 0; } return l/r;
                case TK_PERCENT: if (r==0) { eval_err(ev,n->line,"modulo by zero"); return 0; } return l%r;
                case TK_EQ: return l==r; case TK_NE: return l!=r;
                case TK_LT: return l<r; case TK_GT: return l>r; case TK_LE: return l<=r; case TK_GE: return l>=r;
                case TK_AND: return l&&r; case TK_OR: return l||r;
            }
            return 0;
        }
        case NK_UNOP: {
            long v = eval_node(ev, n->a); if (ev->err) return 0;
            if (n->ival==TK_MINUS) return -v; if (n->ival==TK_NOT) return !v; return 0;
        }
        case NK_PRINT: {
            if (n->a) { if (n->a->kind == NK_STR) eval_node(ev, n->a); else { long v = eval_node(ev, n->a); if (ev->err) return 0; iprint(ev->sc, v); } }
            if (n->ival) ev->sc->putc('\n');
            return 0;
        }
        case NK_CALL: return eval_call(ev, n, n->line);
        case NK_VARDECL: {
            long v = n->a ? eval_node(ev, n->a) : 0; if (ev->err) return 0;
            if (ev->nvars >= MAX_VARS) { eval_err(ev, n->line, "too many variables"); return 0; }
            ev->vars[ev->nvars].name=n->name; ev->vars[ev->nvars].namelen=n->namelen; ev->vars[ev->nvars].val=v; ev->nvars++;
            return 0;
        }
        case NK_EXPRSTMT: eval_node(ev, n->a); return 0;
        case NK_IF: { long c=eval_node(ev,n->a); if (ev->err) return 0; if (c) eval_node(ev,n->b); else if (n->c) eval_node(ev,n->c); return 0; }
        case NK_WHILE: { while (!ev->err) { long c=eval_node(ev,n->a); if (ev->err||!c) break; eval_node(ev,n->b); } return 0; }
        case NK_FOR: { if (n->a) eval_node(ev,n->a); while (!ev->err) { if (n->b) { long c=eval_node(ev,n->b); if (ev->err||!c) break; } eval_node(ev,n->d); if (n->c) eval_node(ev,n->c); } return 0; }
        case NK_RETURN: return 0;
        case NK_BLOCK: { Node* s=n->a; while (s&&!ev->err) { eval_node(ev,s); s=s->next; } return 0; }
        default: return 0;
    }
}

// ============================================================
// Entry
// ============================================================
static const char* BUILTIN_SRC =
    "class Main {\n"
    "    public static void main(String[] args) {\n"
    "        int i; int sum;\n"
    "        sum = 0;\n"
    "        for (i = 1; i <= 10; i = i + 1) {\n"
    "            sum = sum + i;\n"
    "        }\n"
    "        System.out.println(sum);\n"
    "    }\n"
    "}\n";

__attribute__((section(".module_entry")))
int module_entry(const mod_syscalls_t* sc, const char* src, const char* proc_name) {
    (void)proc_name;
    sc->puts("== FSOS Java 26 SE (jvm module) ==\n");
    if (!src) src = BUILTIN_SRC;

    TokArr* ta = (TokArr*)sc->malloc(sizeof(TokArr));
    NodePool* np = (NodePool*)sc->malloc(sizeof(NodePool));
    if (!ta || !np) { sc->puts("[jvm] err 0: out of memory\n"); if (ta) sc->free(ta); if (np) sc->free(np); return -1; }

    int err_line = 0; const char* err_msg = 0;
    if (lex(sc, src, ta, &err_line, &err_msg) != 0) {
        sc->puts("[jvm] err "); iprint(sc, err_line); sc->puts(": "); sc->puts(err_msg); sc->puts("\n");
        sc->free(ta); sc->free(np); return -1;
    }

    Parser ps = { ta, 0, np, 0, 0, 0 };
    Node* prog = parse_program(&ps);
    if (ps.err) {
        sc->puts("[jvm] err "); iprint(sc, ps.err_line); sc->puts(": "); sc->puts(ps.err_msg); sc->puts("\n");
        sc->free(ta); sc->free(np); return -1;
    }

    Eval ev; memset(&ev, 0, sizeof(ev));
    ev.sc = sc;
    Node* m = prog->a;
    while (m) {
        if (m->kind == NK_METHOD) {
            if (ev.nfuncs >= MAX_FUNCS) { sc->puts("[jvm] err 0: too many methods\n"); sc->free(ta); sc->free(np); return -1; }
            ev.funcs[ev.nfuncs].name=m->name; ev.funcs[ev.nfuncs].namelen=m->namelen; ev.funcs[ev.nfuncs].fn=m; ev.nfuncs++;
        }
        m = m->next;
    }

    Node* main_fn = 0;
    for (int i = 0; i < ev.nfuncs; i++) {
        if (name_eq(ev.funcs[i].name, ev.funcs[i].namelen, "main", 4)) { main_fn = ev.funcs[i].fn; break; }
    }
    if (!main_fn) { sc->puts("[jvm] err 0: no main method\n"); sc->free(ta); sc->free(np); return -1; }

    Node call_node; call_node.kind=NK_CALL; call_node.name="main"; call_node.namelen=4; call_node.a=0; call_node.line=1;
    eval_call(&ev, &call_node, 1);

    if (ev.err) { sc->puts("[jvm] err "); iprint(sc, ev.err_line); sc->puts(": "); sc->puts(ev.err_msg); sc->puts("\n"); sc->free(ta); sc->free(np); return -1; }

    sc->puts("JVM_OK\n");
    sc->free(ta); sc->free(np);
    return 0;
}
