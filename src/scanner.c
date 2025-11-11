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
    ARRAY(markup_stack, char) \
    ARRAY_STR(block_name_stack)

#define TOKEN_TYPES \
    TOK(BLOCK_BEGIN_MARKER) \
    TOK(BLOCK_END_MARKER) \
    TOK(BLOCK_BEGIN_NAME) \
    TOK(BLOCK_END_NAME) \
    TOK(DRAWER_NAME) \
    TOK(DRAWER_END) \
    TOK(PROPERTY_NAME) \
    TOK(STARS) \
    TOK(END_SECTION) \
    TOK(BULLET) \
    TOK(LIST_START) \
    TOK(LIST_END) \
    TOK(BOLD_START) \
    TOK(BOLD_END) \
    TOK(ITALIC_START) \
    TOK(ITALIC_END) \
    TOK(UNDERLINE_START) \
    TOK(UNDERLINE_END) \
    TOK(VERBATIM_START) \
    TOK(VERBATIM_END) \
    TOK(CODE_INLINE_START) \
    TOK(CODE_INLINE_END) \
    TOK(STRIKETHROUGH_START) \
    TOK(STRIKETHROUGH_END) \
    TOK(WORD) \
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
    NO_BULLET = 'X',
} Bullet;

static const enum TokenType markup_begins[] = {
    BOLD_START,
    ITALIC_START,
    UNDERLINE_START,
    VERBATIM_START,
    CODE_INLINE_START,
    STRIKETHROUGH_START,
};

static const enum TokenType markup_ends[] = {
    BOLD_END,
    ITALIC_END,
    UNDERLINE_END,
    VERBATIM_END,
    CODE_INLINE_END,
    STRIKETHROUGH_END,
};

static const char markup_chars[] = {
    [BOLD_START] = '*',
    [BOLD_END] = '*',
    [ITALIC_START] = '/',
    [ITALIC_END] = '/',
    [UNDERLINE_START] = '_',
    [UNDERLINE_END] = '_',
    [VERBATIM_START] = '=',
    [VERBATIM_END] = '=',
    [CODE_INLINE_START] = '~',
    [CODE_INLINE_END] = '~',
    [STRIKETHROUGH_START] = '+',
    [STRIKETHROUGH_END] = '+',
};

#define ARRAY_STR(name) ARRAY(name, const char *)

typedef struct {
#define ARRAY(name, type) Array(type) name;
ARRAYS
#undef ARRAY
} Scanner;

static unsigned scan_literal(TSLexer *lexer, const char *string) {
    unsigned len = 0;

    for (char c = *string; c != '\0'; c = *(++string)) {
        if (lexer->eof(lexer) || lexer->lookahead != c) {
            return len;
        }
        lexer->advance(lexer, false);
        len++;
    }
    return len;
}

static const char *scan_while(TSLexer *lexer, bool (*pred)(char)) {
    if (lexer->eof(lexer) || !pred(lexer->lookahead)) return NULL;

    char *name = ts_malloc(sizeof(char) * NAME_MAX_LEN);
    unsigned n;

    for (n = 0; pred(lexer->lookahead) && !lexer->eof(lexer); n++) {
        name[n] = lexer->lookahead;
        lexer->advance(lexer, false);
    }

    name[n] = '\0';
    return name;
}

static unsigned skip_while(TSLexer *lexer, bool (*pred)(char), bool ws) {
    if (!pred(lexer->lookahead)) return 0;

    unsigned n;
    for (n = 0; pred(lexer->lookahead); n++) {
        lexer->advance(lexer, ws);
    }

    return n;
}

static inline bool is_name_char(char c) {
    return iswalnum(c) || c == '_' || c == '-';
}

static inline bool is_property_name_char(char c) {
    return is_name_char(c) || c == '+';
}

static inline bool not_whitespace(char c) {
    return !iswspace(c);
}

static inline bool is_whitespace(char c) {
    return iswspace(c);
}

static bool is_word_char(Scanner *s, char c) {
    if (c == '\0') return false;

    // words can't contain any of the markup symbols (e.g. *, /) we're
    // currently inside. otherwise, they would consume the end token.
    for (int i = 0; i < s->markup_stack.size; i++) {
        if (c == s->markup_stack.contents[i]) return false;
    }

    if (is_whitespace(c)) return false;

    return true;
}

static Bullet scan_bullet(TSLexer *lexer) {
    Bullet kind = NO_BULLET;

    if (lexer->lookahead == '-') {
        kind = HYPHEN;
    } else if (lexer->lookahead == '*' && lexer->get_column(lexer) > 0) {
        kind = STAR;
    } else if (lexer->lookahead == '+') {
        kind = PLUS;
    } else if (iswalnum(lexer->lookahead)) {
        while (iswdigit(lexer->lookahead)) lexer->advance(lexer, false);

        if (lexer->lookahead == '.') {
            kind = COUNTER_DOT;
        } else if (lexer->lookahead == ')') {
            kind = COUNTER_PAREN;
        }
    }

    if (kind == NO_BULLET) return NO_BULLET;

    lexer->advance(lexer, false);

    if (skip_while(lexer, is_whitespace, true) == 0) {
        // we need at least one space following a bullet
        return NO_BULLET;
    }

    return kind;
}

static bool scan_stars(Scanner *s, TSLexer *lexer, const bool *valid_symbols, unsigned char found_already) {
    unsigned char new_level = found_already;
    while (lexer->lookahead == '*') {
        lexer->advance(lexer, false);
        new_level++;
    }

    if (valid_symbols[END_SECTION] && new_level <= *array_back(&s->section_level)) {
        LOG("***< ending section");
        lexer->result_symbol = END_SECTION;
        array_pop(&s->section_level);
    } else {
        LOG("***> emitting STARS");
        lexer->result_symbol = STARS;
        lexer->mark_end(lexer);

        if (!is_whitespace(lexer->lookahead)) {
            // must be followed up by whitespace.
            return false;
        }

        array_push(&s->section_level, new_level);
    }

    return true;
}

static bool scan_markup_end(Scanner *s, TSLexer *lexer, const bool *valid_symbols, char *fail) {
    char in_markup = s->markup_stack.size > 0
        ? s->markup_stack.contents[s->markup_stack.size - 1] : '\0';

    if (in_markup == '\0' || *fail) return false;

    for (int i = 0; i < 6; i++) {
        enum TokenType type = markup_ends[i];
        const char ch = markup_chars[type];
        if (valid_symbols[type] && lexer->lookahead == ch) {
            lexer->advance(lexer, false);
            lexer->result_symbol = type;
            lexer->mark_end(lexer);
            LOG("scanned '%c', markup end", ch);
            array_pop(&s->markup_stack);
            return true;
        }
    }

    return false;
}

static bool scan_markup_start(Scanner *s, TSLexer *lexer, const bool *valid_symbols, char *fail) {
    if (*fail) return false;

    for (int i = 0; i < 6; i++) {
        enum TokenType type = markup_begins[i];
        const char ch = markup_chars[type];
        if (valid_symbols[type] && lexer->lookahead == ch) {
            lexer->advance(lexer, false);

            if (is_whitespace(lexer->lookahead) || lexer->lookahead == ch) {
                // this cannot be a START in this case
                LOG("failed to scan '%c' as markup start", ch);
                *fail = ch;
            } else {
                lexer->result_symbol = type;
                lexer->mark_end(lexer);
                array_push(&s->markup_stack, ch);
                LOG("scanned '%c', markup start", ch);
                return true;
            }
        }
    }

    return false;
}

bool tree_sitter_orgmode_external_scanner_scan(
    void *payload,
    TSLexer *lexer,
    const bool *valid_symbols
) {
    Scanner *s = (Scanner*) payload;

    if (lexer->lookahead == '\n') {
        array_clear(&s->markup_stack);
    }

    if (valid_symbols[ERROR_SENTINEL]) {
        LOG("!!! error");
        return false;
    }

    lexer->mark_end(lexer);

    unsigned col = lexer->get_column(lexer);

    unsigned char indent = s->list_indents.size == 0
        ? 255 : *array_back(&s->list_indents);

    DrawerType in_drawer = s->drawer_stack.size == 0
        ? NO_DRAWER : *array_back(&s->drawer_stack);

    char in_markup = s->markup_stack.size == 0
        ? '\0' : *array_back(&s->markup_stack);

    char fail = '\0'; // '\0' is "no fail yet"

    LOG("********");
    LOG("indent: %d; in_drawer: %c; lookahead: '%c'", indent, in_drawer, lexer->lookahead);

    #define TOK(id) if (valid_symbols[id]) { LOG("EXPECTING TOKEN: " #id); }
    TOKEN_TYPES
    #undef TOK

    if (!fail && valid_symbols[END_SECTION] && lexer->eof(lexer)) {
        lexer->result_symbol = END_SECTION;
        LOG("ending section due to EOF");
        return true;
    }

    bool can_end_list = lexer->eof(lexer) || (indent != 255 && col < indent);
    if (!fail && valid_symbols[LIST_END] && can_end_list) {
        lexer->result_symbol = LIST_END;
        array_pop(&s->list_indents);
        LOG("ending list!");
        return true;
    }

    if (scan_markup_end(s, lexer, valid_symbols, &fail)) {
        return true;
    }

    if (scan_markup_start(s, lexer, valid_symbols, &fail)) {
        return true;
    }

    if (!fail && valid_symbols[STARS] && lexer->get_column(lexer) == 0 && lexer->lookahead == '*') {
        return scan_stars(s, lexer, valid_symbols, 0);
    }

    if (fail == '*' && valid_symbols[STARS] && lexer->get_column(lexer) == 1) {
        return scan_stars(s, lexer, valid_symbols, 1);
    }

    if (!fail && in_drawer == PROPERTY_DRAWER && valid_symbols[PROPERTY_NAME] && lexer->lookahead == ':') {
        LOG("looking for a property name");

        lexer->advance(lexer, false);
        const char *name = scan_while(lexer, is_property_name_char);
        if (name != NULL) {
            if (lexer->lookahead == ':') {
                lexer->advance(lexer, false);

                LOG("got one: %s", name);

                // a name can't be 'end'
                if (strcmp(name, "end") == 0) {
                    if (valid_symbols[DRAWER_END]) {
                        lexer->result_symbol = DRAWER_END;
                        array_pop(&s->drawer_stack);
                        lexer->mark_end(lexer);
                        return true;
                    } else {
                        return false;
                    }
                }

                ts_free((void*) name);
                lexer->mark_end(lexer);
                lexer->result_symbol = PROPERTY_NAME;
                LOG("returning property name");

                return true;
            } else {
                ts_free((void*) name);
                lexer->mark_end(lexer);
                lexer->result_symbol = WORD;

                return true;
            }
        }
    }

    if (!fail && in_drawer != PROPERTY_DRAWER && valid_symbols[DRAWER_NAME] && lexer->lookahead == ':') {
        lexer->advance(lexer, false);
        const char *name = scan_while(lexer, is_name_char);

        if (name != NULL) {
            if (lexer->lookahead == ':') {
                lexer->advance(lexer, false);

                // a name can't be 'end'
                if (strcmp(name, "end") == 0) {
                    if (valid_symbols[DRAWER_END]) {
                        lexer->result_symbol = DRAWER_END;
                        array_pop(&s->drawer_stack);
                        lexer->mark_end(lexer);
                        return true;
                    } else {
                        return false;
                    }
                }

                DrawerType drawer_type = strcmp(name, "properties") == 0
                    ? PROPERTY_DRAWER : NORMAL_DRAWER;
                ts_free((void*) name);
                array_push(&s->drawer_stack, drawer_type);

                lexer->result_symbol = DRAWER_NAME;
                lexer->mark_end(lexer);
                return true;
            } else {
                LOG("defaulting drawer name to a WORD, as no ':' following");
                lexer->result_symbol = WORD;
                lexer->mark_end(lexer);
                ts_free((void*) name);
                return true;
            }
        }
    }

    if (!fail && valid_symbols[DRAWER_END] && in_drawer != NO_DRAWER) {
        unsigned len = scan_literal(lexer, ":end:");
        if (len == 5) {
            lexer->mark_end(lexer);
            lexer->result_symbol = DRAWER_END;
            array_pop(&s->drawer_stack);
            return true;
        } else if (len > 0) {
            lexer->mark_end(lexer);
            lexer->result_symbol = WORD;
            LOG("giving a WORD instead of an DRAWER_END");
            return true;
        }
    }

    if (!fail && valid_symbols[BLOCK_BEGIN_NAME]) {
        lexer->log(lexer, "looking for a BLOCK_BEGIN_NAME");

        const char *name = scan_while(lexer, not_whitespace);
        if (name == NULL) return false;

        LOG("got one: '%s'", name);

        array_push(&s->block_name_stack, name);
        LOG("pushed to array");

        lexer->result_symbol = BLOCK_BEGIN_NAME;
        lexer->mark_end(lexer);

        return true;
    }

    if (!fail && valid_symbols[BLOCK_END_NAME]) {
        lexer->log(lexer, "looking for a BLOCK_END_NAME");

        const char *name = scan_while(lexer, not_whitespace);
        if (name == NULL) return false;

        if (s->block_name_stack.size == 0) {
            LOG("got one, but nothing on the stack...");
            ts_free((void*) name);
            return false;
        }

        const char *top_name = array_pop(&s->block_name_stack);
        LOG("top name: '%s'", top_name);
        int compare = strncmp(name, top_name, NAME_MAX_LEN);
        LOG("comparing '%s' with '%s': %d", name, top_name, compare);

        ts_free((void*) name);

        if (compare != 0) {
            // push it back again; we're just a word
            array_push(&s->block_name_stack, top_name);
            lexer->result_symbol = WORD;
        } else {
            ts_free((void*) top_name);
            lexer->result_symbol = BLOCK_END_NAME;
        }

        lexer->mark_end(lexer);

        return true;
    }

    if (!fail && valid_symbols[BLOCK_END_MARKER]) {
        unsigned len = scan_literal(lexer, "#+end_");
        if (len == 6) {
            LOG("got a BLOCK_END_MARKER");
            lexer->result_symbol = BLOCK_END_MARKER;
            lexer->mark_end(lexer);
            return true;
        } else if (len > 0) {
            LOG("not a BLOCK_END, but defaulting to a WORD");
            lexer->result_symbol = WORD;
            lexer->mark_end(lexer);
            return true;
        }
    }

    if (!fail && valid_symbols[BLOCK_BEGIN_MARKER]) {
        unsigned len = scan_literal(lexer, "#+begin_");
        if (len == 8) {
            LOG("got a BLOCK_BEGIN_MARKER");
            lexer->result_symbol = BLOCK_BEGIN_MARKER;
            lexer->mark_end(lexer);
            return true;
        } else if (len > 0) {
            LOG("not a BLOCK_BEGIN, but defaulting to a WORD");
            lexer->result_symbol = WORD;
            lexer->mark_end(lexer);
            return true;
        }
    }

    if (!fail && valid_symbols[BULLET] || valid_symbols[LIST_START]) {
        lexer->mark_end(lexer);

        Bullet b = scan_bullet(lexer);
        LOG("tried to scan a bullet; got: '%c'", b);

        if (b != NO_BULLET) {
            LOG("got bullet '%c'", b);
            // got a bullet
            if (valid_symbols[LIST_START] && (indent == 255 || col > indent)) {
                lexer->result_symbol = LIST_START;
                array_push(&s->list_indents, col);
                LOG("pushing list start for bullet: %c", b);
                return true;
            }

            if (valid_symbols[BULLET] && col == indent) {
                lexer->mark_end(lexer);
                lexer->result_symbol = BULLET;
                LOG("returning bullet: %c", b);
                return true;
            }
        }
    }

    // can do this even if failed earlier. use the failed character
    if (valid_symbols[WORD] &&
        (is_word_char(s, fail) || is_word_char(s, lexer->lookahead)))
    {
        LOG("attempting a word. already got char?: '%c'", fail);

        while (!lexer->eof(lexer) && is_word_char(s, lexer->lookahead)) {
            lexer->advance(lexer, false);
        }

        lexer->result_symbol = WORD;
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
