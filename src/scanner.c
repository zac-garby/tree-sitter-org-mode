#include "tree_sitter/parser.h"
#include "tree_sitter/alloc.h"
#include "tree_sitter/array.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>

#define NAME_MAX_LEN 64

#define DEBUG

#ifdef DEBUG
    #define PRINT(fmt, ...) (printf(fmt, ##__VA_ARGS__))
    #define LOG(fmt, ...) do { \
        char msg[128]; \
        sprintf(msg, fmt, ##__VA_ARGS__); \
        lexer->log(lexer, msg); \
    } while (0)
#else
    #define PRINT(fmt, ...)
    #define LOG(fmt, ...)
#endif

#define ARRAYS \
    ARRAY(section_level, unsigned char) \
    ARRAY(list_indents, unsigned char) \
    ARRAY(drawer_stack, DrawerType) \
    ARRAY_STR(block_name_stack)

#define TOKEN_TYPES \
    TOK(BLOCK_BEGIN_MARKER) \
    TOK(BLOCK_END_MARKER) \
    TOK(BLOCK_BEGIN_NAME) \
    TOK(BLOCK_END_NAME) \
    TOK(DRAWER_NAME) \
    TOK(DRAWER_END) \
    TOK(STARS) \
    TOK(END_SECTION) \
    TOK(BULLET) \
    TOK(LIST_START) \
    TOK(LIST_END) \
    TOK(ERROR_SENTINEL)

enum TokenType {
#define TOK(id) id,
TOKEN_TYPES
#undef TOK
};

typedef enum {
    NORMAL_DRAWER = 'N',
    PROPERTY_DRAWER = 'P',
    NO_DRAWER = 'X',
} DrawerType;

typedef enum {
    HYPHEN = '-',
    STAR = '*',
    PLUS = '+',
    COUNTER_DOT = '.',
    COUNTER_PAREN = ')',
} Bullet;

#define ARRAY_STR(name) ARRAY(name, const char *)

typedef struct {
#define ARRAY(name, type) Array(type) name;
ARRAYS
#undef ARRAY
} Scanner;

static bool scan_literal(TSLexer *lexer, const char *string) {
    for (char c = *string; c != '\0'; c = *(++string)) {
        if (lexer->eof(lexer) || lexer->lookahead != c) {
            return false;
        }
        lexer->advance(lexer, false);
    }
    return true;
}

static const char *scan_while(TSLexer *lexer, bool (*pred)(char)) {
    if (!pred(lexer->lookahead)) return NULL;

    char *name = ts_malloc(sizeof(char) * NAME_MAX_LEN);
    unsigned n;

    for (n = 0; pred(lexer->lookahead); n++) {
        name[n] = lexer->lookahead;
        lexer->advance(lexer, false);
    }

    name[n] = '\0';
    return name;
}

static Bullet scan_bullet(TSLexer *lexer) {
    if (lexer->lookahead == '-') {
        lexer->advance(lexer, false);
        return HYPHEN;
    } else if (lexer->lookahead == '*' && lexer->get_column(lexer) > 0) {
        lexer->advance(lexer, false);
        return STAR;
    } else if (lexer->lookahead == '+') {
        lexer->advance(lexer, false);
        return PLUS;
    } else if (iswalnum(lexer->lookahead)) {
        while (iswdigit(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }

        if (lexer->lookahead == '.') {
            lexer->advance(lexer, false);
            return COUNTER_DOT;
        } else if (lexer->lookahead == ')') {
            lexer->advance(lexer, false);
            return COUNTER_PAREN;
        }
    }

    return 0;
}

static bool is_name_char(char c) {
    return iswalnum(c) || c == '_' || c == '-';
}

static inline bool isnotspace(char c) {
    return !isspace(c);
}

bool tree_sitter_orgmode_external_scanner_scan(
    void *payload,
    TSLexer *lexer,
    const bool *valid_symbols
) {
    Scanner *s = (Scanner*) payload;

    lexer->mark_end(lexer);

    unsigned col = lexer->get_column(lexer);

    unsigned char indent = s->list_indents.size == 0
        ? 255 : *array_back(&s->list_indents);

    DrawerType in_drawer = s->drawer_stack.size == 0
        ? NO_DRAWER : *array_back(&s->drawer_stack);

    if (valid_symbols[ERROR_SENTINEL]) {
        return false;
    }

    #define TOK(id) if (valid_symbols[id]) { LOG("EXPECTING TOKEN: " #id); }
    TOKEN_TYPES
    #undef TOK

    if (valid_symbols[END_SECTION] && lexer->eof(lexer)) {
        lexer->result_symbol = END_SECTION;
        LOG("ending section due to EOF");
        return true;
    }

    bool can_end_list = lexer->eof(lexer) || (indent != 255 && col < indent);
    if (valid_symbols[LIST_END] && can_end_list) {
        lexer->result_symbol = LIST_END;
        array_pop(&s->list_indents);
        return true;
    }

    if (valid_symbols[BULLET] || valid_symbols[LIST_START]) {
        lexer->mark_end(lexer);

        Bullet b = scan_bullet(lexer);

        if (b > 0) {
            LOG("got bullet '%c'", b);
            // got a bullet
            if (valid_symbols[LIST_START] && (indent == 255 || col > indent)) {
                lexer->result_symbol = LIST_START;
                array_push(&s->list_indents, col);
                return true;
            }

            if (valid_symbols[BULLET] && col == indent) {
                lexer->mark_end(lexer);
                lexer->result_symbol = BULLET;
                return true;
            }
        }
    }

    if (valid_symbols[STARS] && lexer->get_column(lexer) == 0 && lexer->lookahead == '*') {
        LOG("*** SCANNING STARS");
        lexer->mark_end(lexer);

        unsigned char new_level = 0;
        while (lexer->lookahead == '*') {
            lexer->advance(lexer, false);
            new_level++;
        }

        LOG("*** new level: %d. could end: %d", new_level, valid_symbols[END_SECTION]);

        if (valid_symbols[END_SECTION] && new_level <= *array_back(&s->section_level)) {
            LOG("***< ending section");
            lexer->result_symbol = END_SECTION;
            array_pop(&s->section_level);
        } else {
            LOG("***> emitting STARS");
            lexer->result_symbol = STARS;
            lexer->mark_end(lexer);
            array_push(&s->section_level, new_level);
        }

        return true;
    }

    if (valid_symbols[DRAWER_END]
        && scan_literal(lexer, ":end:")
        && in_drawer != NO_DRAWER)
    {
        lexer->mark_end(lexer);
        lexer->result_symbol = DRAWER_END;
        array_pop(&s->drawer_stack);

        return true;
    }

    if (valid_symbols[DRAWER_NAME] && lexer->lookahead == ':') {
        lexer->advance(lexer, false);
        const char *name = scan_while(lexer, is_name_char);
        if (name == NULL || lexer->lookahead != ':') return false;

        lexer->advance(lexer, false);
        lexer->mark_end(lexer);

        if (strcmp(name, "end") == 0) {
            if (valid_symbols[DRAWER_END]) {
                lexer->result_symbol = DRAWER_END;
                array_pop(&s->drawer_stack);
                return true;
            } else {
                return false;
            }
        }

        DrawerType drawer_type =
            strcmp(name, "properties") == 0 ? PROPERTY_DRAWER : NORMAL_DRAWER;
        ts_free((void*) name);
        array_push(&s->drawer_stack, drawer_type);

        lexer->result_symbol = DRAWER_NAME;

        return true;
    }

    if (valid_symbols[BLOCK_BEGIN_NAME]) {
        lexer->log(lexer, "looking for a BLOCK_BEGIN_NAME");

        const char *name = scan_while(lexer, isnotspace);
        if (name == NULL) return false;

        LOG("got one: '%s'", name);

        array_push(&s->block_name_stack, name);
        LOG("pushed to array");

        lexer->result_symbol = BLOCK_BEGIN_NAME;
        lexer->mark_end(lexer);
        return true;
    }

    if (valid_symbols[BLOCK_END_NAME]) {
        lexer->log(lexer, "looking for a BLOCK_END_NAME");

        const char *name = scan_while(lexer, isnotspace);
        if (name == NULL) return false;

        if (s->block_name_stack.size == 0) {
            ts_free((void*) name);
            return false;
        }

        const char *top_name = array_pop(&s->block_name_stack);
        int compare = strncmp(name, top_name, NAME_MAX_LEN);
        LOG("comparing '%s' with '%s': %d", name, top_name, compare);

        ts_free((void*) top_name);
        ts_free((void*) name);

        if (compare != 0) return false;

        lexer->result_symbol = BLOCK_END_NAME;
        lexer->mark_end(lexer);

        return true;
    }

    if (valid_symbols[BLOCK_END_MARKER] && scan_literal(lexer, "#+end_")) {
        LOG("got a BLOCK_END_MARKER");
        lexer->result_symbol = BLOCK_END_MARKER;
        lexer->mark_end(lexer);
        return true;
    }

    if (valid_symbols[BLOCK_BEGIN_MARKER] && scan_literal(lexer, "#+begin_")) {
        LOG("got a BLOCK_BEGIN_MARKER");
        lexer->result_symbol = BLOCK_BEGIN_MARKER;
        lexer->mark_end(lexer);
        return true;
    }

    return false;
}

void * tree_sitter_orgmode_external_scanner_create() {
    Scanner *s = (Scanner*) ts_calloc(1, sizeof(Scanner));

    return s;
}

void tree_sitter_orgmode_external_scanner_destroy(void *payload) {
    Scanner *s = (Scanner*) payload;

    #undef ARRAY_STR
    #define ARRAY_STR(name) \
    for (unsigned i = 0; i < s->name.size; i++) { \
        const char* val = s->name.contents[i]; \
        ts_free((void*) val); \
    } \
    array_delete(&s->name);

    #define ARRAY(name, _) array_delete(&s->name);

    ARRAYS
    #undef ARRAY
    #undef ARRAY_STR
    #define ARRAY_STR(name) ARRAY(name, const char *)

    ts_free(s);
}

unsigned tree_sitter_orgmode_external_scanner_serialize(
    void *payload,
    char *buffer
) {
    Scanner *s = (Scanner*) payload;
    unsigned n = 0;

    PRINT("SERIALIZING... size=%d\n", s->block_name_stack.size);

    #define ARRAY(name, type) \
    buffer[n++] = s->name.size; \
    for (unsigned i = 0; i < s->name.size; i++) { \
        buffer[n++] = s->name.contents[i]; \
    }

    #undef ARRAY_STR
    #define ARRAY_STR(name) \
    buffer[n++] = s->name.size; \
    for (unsigned i = 0; i < s->name.size; i++) { \
        const char* val = s->name.contents[i]; \
        unsigned len = strlen(val); \
        strncpy(buffer + n, val, len); \
        buffer[n + len] = '\0'; \
        n += len + 1; \
    }

    ARRAYS
    #undef ARRAY
    #undef ARRAY_STR
    #define ARRAY_STR(name) ARRAY(name, const char *)

    PRINT("SERIALIZED: %d bytes\n  to buffer: '%s'\n", n, buffer);

    return n;
}

void tree_sitter_orgmode_external_scanner_deserialize(
    void *payload,
    const char *buffer,
    unsigned length
) {
    Scanner *s = (Scanner*) payload;
    unsigned n = 0;

    PRINT("DESERIALIZING: %d bytes: '%s'\n", length, buffer);

    #define ARRAY(name, _) array_init(&s->name);
    ARRAYS
    #undef ARRAY

    if (length > 0) {
        unsigned char size;

        #define ARRAY(name, type) \
        size = buffer[n++]; \
        for (unsigned i = 0; i < size; i++) { \
            array_push(&s->name, buffer[n++]); \
        }

        #undef ARRAY_STR
        #define ARRAY_STR(name) \
        size = buffer[n++]; \
        for (unsigned i = 0; i < size; i++) { \
            unsigned len = strlen(buffer + n); \
            char *val = ts_calloc(1, sizeof(char) * (len + 1)); \
            strncpy(val, buffer + n, len); \
            array_push(&s->name, val); \
            n += strlen(buffer + n) + 1; \
        }

        ARRAYS
        #undef ARRAY
        #undef ARRAY_STR
        #define ARRAY_STR(name) ARRAY(name, const char *)
    }

    PRINT("DESERIALIZED finished: n reached %d\n", n);
}
