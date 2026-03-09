/*
 * ts_lexer.h — OpenSCAD tokenizer
 *
 * Converts OpenSCAD source text into a stream of tokens.
 * Single-pass, no allocations (tokens point into source string).
 *
 * Handles: C-style comments, strings with escapes, $-prefixed
 * special variables, all OpenSCAD operators and keywords.
 */
#ifndef TS_LEXER_H
#define TS_LEXER_H

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * TOKEN TYPES
 * ================================================================ */
typedef enum {
    /* Literals */
    TS_TOK_NUMBER,
    TS_TOK_STRING,
    TS_TOK_IDENT,
    TS_TOK_TRUE,
    TS_TOK_FALSE,
    TS_TOK_UNDEF,

    /* Keywords */
    TS_TOK_MODULE,
    TS_TOK_FUNCTION,
    TS_TOK_IF,
    TS_TOK_ELSE,
    TS_TOK_FOR,
    TS_TOK_LET,
    TS_TOK_EACH,
    TS_TOK_INCLUDE,
    TS_TOK_USE,
    TS_TOK_ECHO,
    TS_TOK_ASSERT,

    /* Operators */
    TS_TOK_PLUS,
    TS_TOK_MINUS,
    TS_TOK_STAR,
    TS_TOK_SLASH,
    TS_TOK_PERCENT,
    TS_TOK_CARET,
    TS_TOK_EQ,
    TS_TOK_NEQ,
    TS_TOK_LT,
    TS_TOK_GT,
    TS_TOK_LE,
    TS_TOK_GE,
    TS_TOK_AND,
    TS_TOK_OR,
    TS_TOK_NOT,
    TS_TOK_ASSIGN,
    TS_TOK_QUESTION,
    TS_TOK_COLON,
    TS_TOK_HASH,

    /* Delimiters */
    TS_TOK_LPAREN,
    TS_TOK_RPAREN,
    TS_TOK_LBRACKET,
    TS_TOK_RBRACKET,
    TS_TOK_LBRACE,
    TS_TOK_RBRACE,
    TS_TOK_SEMICOLON,
    TS_TOK_COMMA,
    TS_TOK_DOT,

    TS_TOK_EOF,
    TS_TOK_ERROR,
} ts_tok_type;

/* ================================================================
 * TOKEN
 * ================================================================ */
typedef struct {
    ts_tok_type type;
    const char *start;
    int         len;
    int         line;
    double      num_val;
} ts_token;

/* ================================================================
 * LEXER STATE
 * ================================================================ */
typedef struct {
    const char *src;
    const char *cur;
    int         line;
    ts_token    peeked;
    int         has_peek;
} ts_lexer;

/* ================================================================
 * IMPLEMENTATION
 * ================================================================ */

static inline void ts_lexer_init(ts_lexer *lex, const char *source) {
    lex->src = source;
    lex->cur = source;
    lex->line = 1;
    lex->has_peek = 0;
}

static inline void ts_lex_skip_ws(ts_lexer *lex) {
    for (;;) {
        /* Whitespace */
        while (*lex->cur == ' ' || *lex->cur == '\t' || *lex->cur == '\r' ||
               *lex->cur == '\n') {
            if (*lex->cur == '\n') lex->line++;
            lex->cur++;
        }
        /* Line comment */
        if (lex->cur[0] == '/' && lex->cur[1] == '/') {
            lex->cur += 2;
            while (*lex->cur && *lex->cur != '\n') lex->cur++;
            continue;
        }
        /* Block comment */
        if (lex->cur[0] == '/' && lex->cur[1] == '*') {
            lex->cur += 2;
            while (*lex->cur && !(lex->cur[0] == '*' && lex->cur[1] == '/')) {
                if (*lex->cur == '\n') lex->line++;
                lex->cur++;
            }
            if (*lex->cur) lex->cur += 2; /* skip closing */
            continue;
        }
        break;
    }
}

static inline ts_token ts_lex_make(ts_tok_type type, const char *start,
                                    int len, int line) {
    ts_token t;
    t.type = type;
    t.start = start;
    t.len = len;
    t.line = line;
    t.num_val = 0;
    return t;
}

static inline int ts_lex_kw_match(const char *start, int len,
                                   const char *kw) {
    return (int)strlen(kw) == len && memcmp(start, kw, (size_t)len) == 0;
}

static inline ts_token ts_lexer_scan(ts_lexer *lex) {
    ts_lex_skip_ws(lex);

    if (*lex->cur == '\0')
        return ts_lex_make(TS_TOK_EOF, lex->cur, 0, lex->line);

    const char *start = lex->cur;
    int line = lex->line;
    char c = *lex->cur++;

    /* Numbers */
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)*lex->cur))) {
        lex->cur = start;
        char *end;
        double val = strtod(lex->cur, &end);
        int len = (int)(end - lex->cur);
        lex->cur = end;
        ts_token t = ts_lex_make(TS_TOK_NUMBER, start, len, line);
        t.num_val = val;
        return t;
    }

    /* Strings */
    if (c == '"') {
        while (*lex->cur && *lex->cur != '"') {
            if (*lex->cur == '\\' && lex->cur[1]) lex->cur++;
            if (*lex->cur == '\n') lex->line++;
            lex->cur++;
        }
        if (*lex->cur == '"') lex->cur++;
        return ts_lex_make(TS_TOK_STRING, start + 1,
                           (int)(lex->cur - start - 2), line);
    }

    /* Identifiers and keywords (including $-prefixed) */
    if (isalpha((unsigned char)c) || c == '_' || c == '$') {
        while (isalnum((unsigned char)*lex->cur) || *lex->cur == '_')
            lex->cur++;
        int len = (int)(lex->cur - start);

        /* Check keywords */
        if (ts_lex_kw_match(start, len, "true"))     return ts_lex_make(TS_TOK_TRUE, start, len, line);
        if (ts_lex_kw_match(start, len, "false"))    return ts_lex_make(TS_TOK_FALSE, start, len, line);
        if (ts_lex_kw_match(start, len, "undef"))    return ts_lex_make(TS_TOK_UNDEF, start, len, line);
        if (ts_lex_kw_match(start, len, "module"))   return ts_lex_make(TS_TOK_MODULE, start, len, line);
        if (ts_lex_kw_match(start, len, "function")) return ts_lex_make(TS_TOK_FUNCTION, start, len, line);
        if (ts_lex_kw_match(start, len, "if"))       return ts_lex_make(TS_TOK_IF, start, len, line);
        if (ts_lex_kw_match(start, len, "else"))     return ts_lex_make(TS_TOK_ELSE, start, len, line);
        if (ts_lex_kw_match(start, len, "for"))      return ts_lex_make(TS_TOK_FOR, start, len, line);
        if (ts_lex_kw_match(start, len, "let"))      return ts_lex_make(TS_TOK_LET, start, len, line);
        if (ts_lex_kw_match(start, len, "each"))     return ts_lex_make(TS_TOK_EACH, start, len, line);
        if (ts_lex_kw_match(start, len, "include"))  return ts_lex_make(TS_TOK_INCLUDE, start, len, line);
        if (ts_lex_kw_match(start, len, "use"))      return ts_lex_make(TS_TOK_USE, start, len, line);
        if (ts_lex_kw_match(start, len, "echo"))     return ts_lex_make(TS_TOK_ECHO, start, len, line);
        if (ts_lex_kw_match(start, len, "assert"))   return ts_lex_make(TS_TOK_ASSERT, start, len, line);

        return ts_lex_make(TS_TOK_IDENT, start, len, line);
    }

    /* Two-character operators */
    switch (c) {
    case '=': if (*lex->cur == '=') { lex->cur++; return ts_lex_make(TS_TOK_EQ, start, 2, line); }
              return ts_lex_make(TS_TOK_ASSIGN, start, 1, line);
    case '!': if (*lex->cur == '=') { lex->cur++; return ts_lex_make(TS_TOK_NEQ, start, 2, line); }
              return ts_lex_make(TS_TOK_NOT, start, 1, line);
    case '<': if (*lex->cur == '=') { lex->cur++; return ts_lex_make(TS_TOK_LE, start, 2, line); }
              /* include <path> — handled by parser, < is just LT here */
              return ts_lex_make(TS_TOK_LT, start, 1, line);
    case '>': if (*lex->cur == '=') { lex->cur++; return ts_lex_make(TS_TOK_GE, start, 2, line); }
              return ts_lex_make(TS_TOK_GT, start, 1, line);
    case '&': if (*lex->cur == '&') { lex->cur++; return ts_lex_make(TS_TOK_AND, start, 2, line); }
              break;
    case '|': if (*lex->cur == '|') { lex->cur++; return ts_lex_make(TS_TOK_OR, start, 2, line); }
              break;
    }

    /* Single-character tokens */
    switch (c) {
    case '+': return ts_lex_make(TS_TOK_PLUS, start, 1, line);
    case '-': return ts_lex_make(TS_TOK_MINUS, start, 1, line);
    case '*': return ts_lex_make(TS_TOK_STAR, start, 1, line);
    case '/': return ts_lex_make(TS_TOK_SLASH, start, 1, line);
    case '%': return ts_lex_make(TS_TOK_PERCENT, start, 1, line);
    case '^': return ts_lex_make(TS_TOK_CARET, start, 1, line);
    case '?': return ts_lex_make(TS_TOK_QUESTION, start, 1, line);
    case ':': return ts_lex_make(TS_TOK_COLON, start, 1, line);
    case '#': return ts_lex_make(TS_TOK_HASH, start, 1, line);
    case '(': return ts_lex_make(TS_TOK_LPAREN, start, 1, line);
    case ')': return ts_lex_make(TS_TOK_RPAREN, start, 1, line);
    case '[': return ts_lex_make(TS_TOK_LBRACKET, start, 1, line);
    case ']': return ts_lex_make(TS_TOK_RBRACKET, start, 1, line);
    case '{': return ts_lex_make(TS_TOK_LBRACE, start, 1, line);
    case '}': return ts_lex_make(TS_TOK_RBRACE, start, 1, line);
    case ';': return ts_lex_make(TS_TOK_SEMICOLON, start, 1, line);
    case ',': return ts_lex_make(TS_TOK_COMMA, start, 1, line);
    case '.': return ts_lex_make(TS_TOK_DOT, start, 1, line);
    }

    return ts_lex_make(TS_TOK_ERROR, start, 1, line);
}

static inline ts_token ts_lexer_next(ts_lexer *lex) {
    if (lex->has_peek) {
        lex->has_peek = 0;
        return lex->peeked;
    }
    return ts_lexer_scan(lex);
}

static inline ts_token ts_lexer_peek(ts_lexer *lex) {
    if (!lex->has_peek) {
        lex->peeked = ts_lexer_scan(lex);
        lex->has_peek = 1;
    }
    return lex->peeked;
}

/* Extract a heap-allocated copy of token text */
static inline char *ts_tok_strdup(ts_token t) {
    char *s = (char *)malloc((size_t)t.len + 1);
    if (s) { memcpy(s, t.start, (size_t)t.len); s[t.len] = '\0'; }
    return s;
}

#endif /* TS_LEXER_H */
