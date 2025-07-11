/*
Copyright 2025 0880880

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SHASH_H
#define SHASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Type definitions

typedef struct {
    char *working_directory;
    void *environment_variables;
    void *global;
    const bool is_interactive;
    bool terminate_signal;
    void *commands;
    void *path;
    void *pathext;
    void *process_table;
} Shell;

typedef struct {
    int posix[2];
    void *wread, *wwrite;
} ShashPipe;

typedef int (*Command)(Shell *shell, char **argv, int argc,
                       ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                       ShashPipe *stderr_pipe);

typedef struct {
    unsigned long pid;
    unsigned long exit_code;
    bool internal;
    bool background;
    void *thread_id;
#ifdef _WIN32
    void *windows_handle;
#else
    // pid_t posix_handle;
#endif
} Process;

typedef struct {
    char **str;
    char *buffer;
    size_t len;
    size_t nb;
} _utf8_str;

typedef struct {
    char *name;
    char *secondary_name;
    char *help;
    void *default_value;
    bool implicit_value;
    bool required;
} Argument;

typedef struct {
    char *program_name;
    Argument **arguments;
    size_t num_args;
    unsigned int min_args;
    unsigned int max_args;
    unsigned int min_positional_args;
    unsigned int max_positional_args;
} Program;

typedef struct {
    Argument *arg;
    char *used_name;
    bool user_set;
    void *value;
} ParsedArgument;

typedef struct {
    ParsedArgument **arguments;
    char **positional_arguments;
    size_t num_args;
    size_t num_pos_args;
} ParseResult;

enum ParseError {
    INVALID_ARGUMENT,
    INVALID_NUM_ARGUMENTS,
    INVALID_NUM_POSITIONAL_ARGUMENTS,
    MISSING_REQUIRED_ARGUMENT
};

// API declarations

Shell *make_shell(bool is_interactive);
Shell *make_shell_no_interactive();

Shell *make_sub_shell(Shell *origin);

void shell_free(Shell *shell);

Process *shell_get_process(Shell *shell, int pid);
int shell_kill_proecss(Shell *shell, int pid);
bool shell_process_is_alive(Shell *shell, int pid);

void shell_terminate_main(Shell *shell);

void shell_run(Shell *shell, char *script, size_t len, ShashPipe *stdin_pipe,
               ShashPipe *stdout_pipe, ShashPipe *stderr_pipe);

void shell_register_command(Shell *shell, char *name, Command command);
void shell_unregister_command(Shell *shell, char *name);
Program program_create(char *program_name);

Argument *program_add_argument(Program *program, char *name,
                               char *secondary_name);

ParseResult *program_parse(Program *program, char **argv, int argc,
                           enum ParseError *error, char **error_msg);

Argument *program_get_argument(Program *program, char *name);

ParsedArgument *parse_result_get_argument(ParseResult *parse, char *name);

void shash_pipe_open(ShashPipe *pipe);
void shash_pipe_create_pipe(char *path, bool create, bool truncate,
                            ShashPipe *pipe);
size_t shash_pipe_write(ShashPipe *pipe, void *buffer, size_t size);
size_t shash_pipe_read(ShashPipe *pipe, void *buffer, size_t size,
                       size_t *bytes_read);
void shash_pipe_close_all(ShashPipe *pipe);
void shash_pipe_close_write(ShashPipe *pipe);
void shash_pipe_close_read(ShashPipe *pipe);
void shash_pipe_duplicate_all(ShashPipe *pipe, ShashPipe *out);
void shash_pipe_duplicate_write(ShashPipe *pipe, ShashPipe *out);
void shash_pipe_duplicate_read(ShashPipe *pipe, ShashPipe *out);
void shash_pipe_puts(ShashPipe *pipe, const char *str);
void shash_pipe_putc(ShashPipe *pipe, char character);
void shash_pipe_printf(ShashPipe *pipe, const char *restrict format, ...);

void make_user_pipes(ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                     ShashPipe *stderr_pipe);

#if !defined(SHASH_INTERNALS)
#define SHASH_PRIVATE static
#else
#define SHASH_PRIVATE /* nothing */
#endif

#ifdef __cplusplus
}
#endif

// Implementation
#ifdef SHASH_IMPLEMENTATION

#ifdef _WIN32
#define UNICODE
#define _UNICODE
#endif

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winnt.h>

#include <fileapi.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Pathcch.lib")
#define PATH_MAX MAX_PATH
#define ENV_PATH_SEPARATOR ';'
#else
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define ENV_PATH_SEPARATOR ':'
#endif

static inline void *xmalloc(size_t bytes) {
    void *buffer = malloc(bytes);
    if (!buffer) {
        fprintf(stderr, "shash: xmalloc: out of memory.\n");
        exit(1);
    }
    return buffer;
}

static char *dupstr(const char *str) {
#ifdef _WIN32
    return _strdup(str);
#else
    return strdup(str);
#endif
}

char *get_cwd() {

    char *cwd = (char *)xmalloc(PATH_MAX);

    if (GetCurrentDirectoryA(PATH_MAX, cwd)) {
        return cwd;
    } else {
        char *home;
#ifdef _WIN32
        size_t len;
        _dupenv_s(&home, &len, "USERPROFILE");
#else
        home = getenv("USERPROFILE");
#endif
        if (home) {
            return home;
        } else {
            return "/";
        }
    }
    return cwd;
}

//
// -=================================================================-
// -============================ Unicode ============================-
// -=================================================================-
//

SHASH_PRIVATE int utf8_len(unsigned char c) {
    if ((c & 0x80) == 0)
        return 1; // 0xxxxxxx
    else if ((c & 0xE0) == 0xC0)
        return 2; // 110xxxxx
    else if ((c & 0xF0) == 0xE0)
        return 3; // 1110xxxx
    else if ((c & 0xF8) == 0xF0)
        return 4; // 11110xxx
    return 1;     // invalid fallback
}

static inline const char *utf8_next(const char *s) { return s + utf8_len(*s); }

SHASH_PRIVATE const char *utf8_char_at(const char *s, size_t index) {
    size_t i = 0;
    while (*s) {
        if (i == index)
            return s;
        s = utf8_next(s);
        i++;
    }
    return NULL; // index out of range
}

static inline const char *utf8_str_char_at(_utf8_str *u, size_t index) {
    return u->str[index];
}

SHASH_PRIVATE int utf8_eq_ascii(const char *utf8_char,
                                unsigned char ascii_char) {
    if (!utf8_char) // sanity
        return 0;
    if (ascii_char >= 0x80) // not ASCII
        return 0;
    if ((*utf8_char & 0x80) != 0) // not ASCII
        return 0;
    return (unsigned char)utf8_char[0] == ascii_char;
}

SHASH_PRIVATE int utf8_is_ascii(const char *utf8_char) {
    if (!utf8_char)
        return 0;
    if ((*utf8_char & 0x80) != 0)
        return 0;
    return (unsigned char)utf8_char[0];
}

SHASH_PRIVATE int utf8_eq_utf8(const char *u0, const char *u1) {
    if (!u0 || !u1) // sanity
        return 0;
    int len = utf8_len(*u0);
    if (len != utf8_len(*u1))
        return 0;
    for (int i = 0; i < len; i++) {
        if (u0[i] != u1[i])
            return 0;
    }
    return 1;
}

SHASH_PRIVATE size_t utf8_strlen(const char *s) {
    size_t len = 0;
    while (*s) {
        s = utf8_next(s);
        len++;
    }
    return len;
}

SHASH_PRIVATE _utf8_str utf8_str(const char *s) {
    if (!s) {
        return (_utf8_str){.str = NULL, .buffer = NULL, .len = 0};
    }
    size_t len = utf8_strlen(s);
    char **str = (char **)xmalloc(sizeof(char *) * len);
    size_t total = 0;
    for (size_t i = 0; i < len; i++) {
        size_t l = utf8_len(s[total]);
        total += l;
    }

    char *buffer = (char *)xmalloc(sizeof(char) * (total + len));
    char *write_ptr = buffer;

    const char *t = s;
    for (size_t i = 0; i < len; i++) {
        int char_len = utf8_len(*t);
        const char *next = t + char_len;

        str[i] = write_ptr;
        memcpy(write_ptr, t, char_len);
        write_ptr[char_len] = '\0';
        write_ptr += char_len + 1;

        t = next;
    }
    _utf8_str u;
    u.str = str;
    u.buffer = buffer;
    u.len = len;
    u.nb = total;
    return u;
}

SHASH_PRIVATE bool utf8_char_eq(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    int len = utf8_len(*a);
    if (len != utf8_len(*b)) {
        return 0;
    }
    for (int i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

SHASH_PRIVATE bool utf8_eq(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    int len = strlen(a);
    if (len != strlen(b)) {
        return 0;
    }
    for (int i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

SHASH_PRIVATE bool utf8_str_eq(_utf8_str *a, _utf8_str *b) { // TODO Implement
    if (!a || !b) {
        return 0;
    }
    return 0;
}

SHASH_PRIVATE bool utf8_contains_ascii_char(_utf8_str *u, char ch) {
    if (!u || !ch || u->len == 0) {
        return false;
    }
    for (int i = 0; i < u->len; i++) {
        if (utf8_eq_ascii(u->str[i], ch)) {
            return true;
        }
    }
    return false;
}

SHASH_PRIVATE void utf8_free(_utf8_str *u) {
    free(u->str);
    free(u->buffer);
}

SHASH_PRIVATE bool utf8_endswith(const char *str, const char *end) {
    if (!str || !end) {
        return false;
    }
    _utf8_str u = utf8_str(str);
    _utf8_str end_u = utf8_str(end);
    if (end_u.nb > u.nb) {
        utf8_free(&u);
        utf8_free(&end_u);
        return false;
    }
    int diff = u.len - end_u.len;
    for (int i = u.len - 1; i <= 0; i--) {
        if (!utf8_char_eq(u.str[i], end_u.str[i - diff])) {
            utf8_free(&u);
            utf8_free(&end_u);
            return false;
        }
    }
    utf8_free(&u);
    utf8_free(&end_u);
    return true;
}

SHASH_PRIVATE uint32_t utf8_codepoint(const char *u) {
    const uint32_t REPLACEMENT_CHARACTER = 0xFFFD;

    if (u == NULL || (unsigned char)u[0] < 0x80) {
        return (u == NULL) ? REPLACEMENT_CHARACTER : (unsigned char)u[0];
    }

    const unsigned char *s = (const unsigned char *)u;
    uint32_t codepoint = 0;
    int len = 0;

    if ((s[0] & 0xE0) == 0xC0) { // 110xxxxx: 2-byte sequence
        len = 2;
        codepoint = s[0] & 0x1F;
    } else if ((s[0] & 0xF0) == 0xE0) { // 1110xxxx: 3-byte sequence
        len = 3;
        codepoint = s[0] & 0x0F;
    } else if ((s[0] & 0xF8) == 0xF0) { // 11110xxx: 4-byte sequence
        len = 4;
        codepoint = s[0] & 0x07;
    } else {
        return REPLACEMENT_CHARACTER;
    }

    for (int i = 1; i < len; ++i) {
        if ((s[i] & 0xC0) != 0x80) {
            return REPLACEMENT_CHARACTER;
        }
        codepoint = (codepoint << 6) | (s[i] & 0x3F);
    }

    static const uint32_t min_vals[] = {0, 0, 0x80, 0x800, 0x10000};

    if (codepoint < min_vals[len] ||                    // Overlong encoding
        codepoint > 0x10FFFF ||                         // Outside Unicode range
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) { // Surrogate halves
        return REPLACEMENT_CHARACTER;
    }

    return codepoint;
}

SHASH_PRIVATE char *utf8_lower_dup(char *str) {
    if (!str) {
        return NULL;
    }
    _utf8_str u = utf8_str(str);
    if (u.len == 0) {
        return NULL;
    }
    char *buf = (char *)xmalloc(u.nb);
    int idx = 0;
    for (int i = 0; i < u.len; i++) {
        char *ch = u.str[i];
        if (utf8_is_ascii(ch)) {
            char ascii = *ch;
            if (ascii >= 'A' && ascii <= 'Z') {
                ascii -= 'a' - 'A';
            }
            buf[idx] = ascii;
            idx++;
        } else {
            for (int j = 0; j < utf8_len(*ch); j++) {
                buf[idx] = ch[j];
                idx++;
            }
        }
    }
    utf8_free(&u);
    return buf;
}

SHASH_PRIVATE int codepoint_to_utf8(uint32_t cp, char *buffer) {
    if (!buffer) {
        return 0;
    }
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }

    // 1-byte sequence (ASCII) for U+0000 to U+007F
    if (cp <= 0x7F) {
        buffer[0] = (char)cp;
        return 1;
    }
    // 2-byte sequence for U+0080 to U+07FF
    else if (cp <= 0x7FF) {
        // Byte 1: 110yyyyy -> 0xC0 | (top 5 bits of cp)
        buffer[0] = 0xC0 | (char)(cp >> 6);
        // Byte 2: 10xxxxxx -> 0x80 | (bottom 6 bits of cp)
        buffer[1] = 0x80 | (char)(cp & 0x3F);
        return 2;
    }
    // 3-byte sequence for U+0800 to U+FFFF
    else if (cp <= 0xFFFF) {
        // Byte 1: 1110zzzz -> 0xE0 | (top 4 bits)
        buffer[0] = 0xE0 | (char)(cp >> 12);
        // Byte 2: 10yyyyyy -> 0x80 | (middle 6 bits)
        buffer[1] = 0x80 | (char)((cp >> 6) & 0x3F);
        // Byte 3: 10xxxxxx -> 0x80 | (bottom 6 bits)
        buffer[2] = 0x80 | (char)(cp & 0x3F);
        return 3;
    }
    // 4-byte sequence for U+10000 to U+10FFFF
    else { // cp <= 0x10FFFF
        // Byte 1: 11110uuu -> 0xF0 | (top 3 bits)
        buffer[0] = 0xF0 | (char)(cp >> 18);
        // Byte 2: 10zzzzzz -> 0x80 | (next 6 bits)
        buffer[1] = 0x80 | (char)((cp >> 12) & 0x3F);
        // Byte 3: 10yyyyyy -> 0x80 | (next 6 bits)
        buffer[2] = 0x80 | (char)((cp >> 6) & 0x3F);
        // Byte 4: 10xxxxxx -> 0x80 | (bottom 6 bits)
        buffer[3] = 0x80 | (char)(cp & 0x3F);
        return 4;
    }
}

static inline bool utf8_is_whitespace(char *ch) {
    return utf8_eq_ascii(ch, ' ') || utf8_eq_ascii(ch, '\n');
}

//
// -=================================================================-
// -========================= StringBuilder =========================-
// -=================================================================-
//

typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} StringBuilder;

SHASH_PRIVATE void sb_init(StringBuilder *sb, size_t initial_capacity) {
    sb->data = (char *)xmalloc(initial_capacity * sizeof(char));
    sb->len = 0;
    sb->capacity = initial_capacity;
    sb->data[0] = '\0';
}

void sb_append_utf8(StringBuilder *sb, const char *utf8) {
    size_t char_len = utf8_len((unsigned char)*utf8);
    if (sb->len + char_len + 1 > sb->capacity) {
        while (sb->len + char_len + 1 > sb->capacity) {
            sb->capacity += 1;
            sb->capacity *= 2;
        }
        sb->data = (char *)realloc(sb->data, sb->capacity * sizeof(char));
    }
    memcpy(sb->data + sb->len, utf8, char_len);
    sb->data[sb->len + char_len] = '\0';
    sb->len += char_len;
}

SHASH_PRIVATE void sb_append(StringBuilder *sb, char *str) {
    if (!str)
        return;
    size_t str_len = strlen(str);
    if (sb->len + str_len + 1 > sb->capacity) {
        while (sb->len + str_len + 1 > sb->capacity) {
            sb->capacity += 1;
            sb->capacity *= 2;
        }
        sb->data = (char *)realloc(sb->data, sb->capacity * sizeof(char));
    }
    memcpy(sb->data + sb->len, str, str_len + 1);
    sb->len += str_len;
}

SHASH_PRIVATE void sb_append_utf8_str(StringBuilder *sb, _utf8_str *u, int size,
                                      int offset) {
    size_t total = 0;
    for (int i = offset; i < offset + size; i++) {
        total += utf8_len(*u->str[i]);
    }
    size_t str_len = total;
    if (sb->len + str_len + 1 > sb->capacity) {
        while (sb->len + str_len + 1 > sb->capacity) {
            sb->capacity += 1;
            sb->capacity *= 2;
        }
        sb->data = (char *)realloc(sb->data, sb->capacity * sizeof(char));
    }
    for (int i = offset; i < offset + size; i++) {
        sb_append_utf8(sb, u->str[i]);
    }
}

SHASH_PRIVATE void sb_append_c(StringBuilder *sb, char c) {
    if (sb->len + 2 > sb->capacity) { // +1 for char, +1 for null terminator
        sb->capacity += 1;
        sb->capacity *= 2;
        sb->data = (char *)realloc(sb->data, sb->capacity * sizeof(char));
    }
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

SHASH_PRIVATE void sb_ensure_capacity(StringBuilder *sb, int capacity) {
    if (capacity > sb->capacity) {
        sb->capacity = capacity;
        sb->data = (char *)realloc(sb->data, capacity * sizeof(char));
    }
}

SHASH_PRIVATE StringBuilder sb_copy(StringBuilder *sb, int size, int offset) {
    StringBuilder copy;
    sb_init(&copy, size + 1);
    memcpy(copy.data, sb->data + offset, size);
    copy.data[size] = '\0';
    copy.len = size;
    return copy;
}

static inline StringBuilder sb_copy_all(StringBuilder *sb) {
    return sb_copy(sb, sb->len, 0);
}

static inline void sb_clear(StringBuilder *sb) { sb->len = 0; }

SHASH_PRIVATE void sb_free(StringBuilder *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->capacity = 0;
}

SHASH_PRIVATE char *utf8_strip_dup(char *str) {

    _utf8_str u = utf8_str(str);

    StringBuilder sb;
    sb_init(&sb, u.len);

    bool start = false;

    for (int i = 0; i < u.len; i++) {
        char *ch = u.str[i];
        if (start) {
            sb_append_utf8(&sb, ch);
        } else if (!utf8_eq_ascii(ch, ' ')) {
            start = true;
            sb_append_utf8(&sb, ch);
        }
    }

    utf8_free(&u);

    str = dupstr(sb.data);
    sb_free(&sb);

    u = utf8_str(str);
    free(str);

    start = false;

    for (int i = u.len - 1; i >= 0; i--) {
        char *ch = u.str[i];
        if (start) {
            sb_append_utf8(&sb, ch);
        } else if (!utf8_eq_ascii(ch, ' ')) {
            start = true;
            sb_append_utf8(&sb, ch);
        }
    }

    utf8_free(&u);

    str = dupstr(sb.data);
    sb_free(&sb);

    u = utf8_str(str);
    free(str);

    for (int i = u.len - 1; i >= 0; i--) {
        char *ch = u.str[i];
        sb_append_utf8(&sb, ch);
    }

    utf8_free(&u);

    char *out = dupstr(sb.data);
    sb_free(&sb);
    return out;
}

//
// -=================================================================-
// -============================= Array =============================-
// -=================================================================-
//

typedef struct {
    void **data;
    size_t capacity;
    size_t size;
} Array;

SHASH_PRIVATE void arr_init(Array *arr, int initial_capacity) {
    arr->data = (void **)xmalloc(initial_capacity * sizeof(void *));
    arr->capacity = initial_capacity;
    arr->size = 0;
}

SHASH_PRIVATE void arr_copy(Array *origin, Array *arr) {
    arr->data = (void **)xmalloc(origin->size * sizeof(void *));
    arr->capacity = origin->size;
    arr->size = origin->size;
}

SHASH_PRIVATE void arr_add(Array *arr, void *item) {
    if (arr->size + 1 > arr->capacity) {
        if (arr->capacity == 0) {
            arr->capacity = 1;
        }
        arr->capacity *= 2;
        void **new_data =
            (void **)realloc(arr->data, arr->capacity * sizeof(void *));
        if (new_data == NULL) {
            fprintf(stderr, "Allocation failed.");
            return;
        }
        arr->data = new_data;
    }
    arr->data[arr->size] = item;
    arr->size++;
}

SHASH_PRIVATE void *arr_pop(Array *arr) {
    if (arr->size == 0) {
        return NULL;
    }
    arr->size--;
    return arr->data[arr->size];
}

static inline void *arr_get(Array *arr, int index) { return arr->data[index]; }

static inline void arr_clear(Array *arr) { arr->size = 0; }

SHASH_PRIVATE void arr_free(Array *arr) {
    arr->size = 0;
    arr->capacity = 0;
    free(arr->data);
    arr->data = NULL;
}

typedef struct {
    int *data;
    size_t capacity;
    size_t size;
} ShashIntArray;

SHASH_PRIVATE void int_arr_add(ShashIntArray *arr, int el) {
    if (arr->size + 1 > arr->capacity) {
        if (arr->capacity == 0)
            arr->capacity = 1;
        arr->capacity *= 2;
        arr->data = realloc(arr->data, arr->capacity);
    }
    arr->data[arr->size] = el;
    arr->size++;
}

static inline int int_arr_pop(ShashIntArray *arr) {
    return arr->data[--arr->size];
}

SHASH_PRIVATE void int_arr_free(ShashIntArray *arr) {
    if (arr->data) {
        free(arr->data);
        arr->data = NULL;
    }
}

//
// -=================================================================-
// -======================== Argument Parser ========================-
// -=================================================================-
//

Program program_create(char *program_name) {
    Program program;
    program.program_name = program_name;
    program.num_args = 0;
    program.min_args = -1;
    program.max_args = -1;
    program.min_positional_args = -1;
    program.max_positional_args = -1;
    return program;
}

Argument *program_add_argument(Program *program, char *name,
                               char *secondary_name) {
    Argument *arg = (Argument *)xmalloc(sizeof(Argument)); // DEFAULTS
    arg->name = name;
    arg->secondary_name = secondary_name;
    arg->help = 0;
    arg->implicit_value = 0;
    arg->default_value = 0;
    arg->required = 0;
    if (program->num_args == 0) {
        program->arguments = (Argument **)xmalloc(1 * sizeof(Argument *));
        program->arguments[0] = arg;
    } else {
        program->arguments = (Argument **)realloc(
            program->arguments, (program->num_args + 1) * sizeof(Argument *));
        program->arguments[program->num_args] = arg;
    }
    program->num_args++;
    return arg;
}

Argument *program_get_argument(Program *program, char *name) {
    for (size_t i = 0; i < program->num_args; i++) {
        if (utf8_eq(program->arguments[i]->name, name) ||
            utf8_eq(program->arguments[i]->secondary_name, name)) {
            return program->arguments[i];
        }
    }
    return 0;
}

ParseResult *program_parse(Program *program, char **argv, int argc,
                           enum ParseError *error, char **error_msg) {
    ParseResult *result = (ParseResult *)xmalloc(sizeof(ParseResult));
    bool force_arg = false;
    int num_args = 0;
    int num_options = 0;
    for (int i = 0; i < argc; i++) {
        if (utf8_eq(argv[i], "--")) {
            force_arg = true;
        } else if (force_arg) {
            num_args++;
        } else if ((utf8_strlen(argv[i]) >= 2 && argv[i][0] == '-' &&
                    argv[i][1] != '-') ||
                   (utf8_strlen(argv[i]) >= 3 && argv[i][0] == '-' &&
                    argv[i][1] == '-' && argv[i][2] != '-')) {
            Argument *arg = program_get_argument(program, argv[i]);
            if (arg == 0) {
                *error = INVALID_ARGUMENT;

                int len = snprintf(NULL, 0, "Invalid argument %s.", argv[i]);
                *error_msg = (char *)xmalloc(len + 1);
                snprintf(*error_msg, len + 1, "Invalid argument %s.", argv[i]);
                return 0;
            }
            if (!arg->implicit_value) {
                if (argc > i + 1 &&
                    !(utf8_strlen(argv[i]) >= 2 && argv[i][0] == '-')) {
                    i++;
                }
            }
            num_options++;
        } else {
            num_args++;
        }
    }
    if ((program->min_positional_args >= 0 &&
         num_args < program->min_positional_args) ||
        (program->max_positional_args >= 0 &&
         num_args > program->max_positional_args)) {
        *error = INVALID_NUM_POSITIONAL_ARGUMENTS;
        int len = snprintf(
            NULL, 0,
            "Invalid number of positional arguments %d expected %d<=x<=%d.",
            num_args, program->min_positional_args,
            program->max_positional_args);
        *error_msg = (char *)xmalloc(len + 1);
        snprintf(
            *error_msg, len + 1,
            "Invalid number of positional arguments %d expected %d<=x<=%d.",
            num_args, program->min_positional_args,
            program->max_positional_args);
        return 0;
    }
    if ((program->min_args >= 0 && num_options < program->min_args) ||
        (program->max_args >= 0 && num_options > program->max_args)) {
        *error = INVALID_NUM_ARGUMENTS;
        int len = snprintf(NULL, 0,
                           "Invalid number of options %d expected %d<=x<=%d.",
                           num_options, program->min_args, program->max_args);
        *error_msg = (char *)xmalloc(len + 1);
        snprintf(*error_msg, len + 1,
                 "Invalid number of options %d expected %d<=x<=%d.",
                 num_options, program->min_args, program->max_args);
        return 0;
    }
    result->positional_arguments = (char **)xmalloc(num_args * sizeof(char *));
    result->arguments =
        (ParsedArgument **)xmalloc(num_options * sizeof(ParsedArgument *));
    force_arg = false;
    int arg_idx = 0;
    int opt_idx = 0;
    for (int i = 0; i < argc; i++) {
        if (utf8_eq(argv[i], "--")) {
            force_arg = true;
        } else if (force_arg) {
            result->positional_arguments[arg_idx] = argv[i];
            arg_idx++;
        } else if ((utf8_strlen(argv[i]) >= 2 && argv[i][0] == '-' &&
                    argv[i][1] != '-') ||
                   (utf8_strlen(argv[i]) >= 3 && argv[i][0] == '-' &&
                    argv[i][1] == '-' &&
                    argv[i][2] != '-')) { // TODO Replace char compares with
                                          // utf8_eq_ascii
            Argument *arg = program_get_argument(program, argv[i]);
            ParsedArgument *parsed_arg =
                (ParsedArgument *)xmalloc(sizeof(ParsedArgument));
            parsed_arg->arg = arg;
            parsed_arg->used_name = argv[i];
            if (arg->implicit_value) {
                parsed_arg->value = parsed_arg;
            } else {
                parsed_arg->value = arg->default_value;
                if (argc > i + 1 &&
                    !(utf8_strlen(argv[i]) >= 2 && argv[i][0] == '-')) {
                    parsed_arg->value = argv[i + 1];
                    parsed_arg->user_set = true;
                    i++;
                }
            }
            result->arguments[arg_idx] = parsed_arg;
            opt_idx++;
        } else {
            result->positional_arguments[arg_idx] = argv[i];
            arg_idx++;
        }
    }
    result->num_pos_args = num_args;
    result->num_args = num_options;

    for (int i = 0; i < program->num_args; i++) {
        Argument *arg = program->arguments[i];
        if (arg->required) {
            bool found = false;
            for (int j = 0; j < result->num_args; j++) {
                if (result->arguments[j]->arg == arg) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                *error = MISSING_REQUIRED_ARGUMENT;
                int len = snprintf(NULL, 0, "Missing required argument \"%s\".",
                                   arg->name);
                *error_msg = (char *)xmalloc(len + 1);
                snprintf(*error_msg, len + 1,
                         "Missing required argument \"%s\".", arg->name);
                return 0;
            }
        }
    }
    return result;
}

void parse_result_free(ParseResult *result) {
    free(result->positional_arguments);
    for (int i = 0; i < result->num_args; i++) {
        if (result->arguments[i]) {
            free(result->arguments[i]);
        }
    }
    free(result->arguments);
    free(result);
}

ParsedArgument *parse_result_get_argument(ParseResult *result, char *name) {
    for (size_t i = 0; i < result->num_args; i++) {
        if (utf8_eq(result->arguments[i]->arg->name, name) ||
            utf8_eq(result->arguments[i]->arg->secondary_name, name)) {
            return result->arguments[i];
        }
    }
    return 0;
}

SHASH_PRIVATE char *normalize_path_dup(char *str) {
    if (!str) {
        return NULL;
    }
    StringBuilder sb;
    sb_init(&sb, utf8_strlen(str) / 2);
    _utf8_str u = utf8_str(str);
    bool slash = false;
    StringBuilder sub_sb;
    sb_init(&sub_sb, 1);
    for (int i = 0; i < u.len; i++) {
        char *ch = u.str[i];
        if (utf8_eq_ascii(ch, '/') || utf8_eq_ascii(ch, '\\')) {
            if (!slash) {
                bool all_dots = true;
                _utf8_str sub_u = utf8_str(sub_sb.data);
                for (int i = 0; i < sub_u.len; i++) {
                    if (!utf8_eq_ascii(sub_u.str[i], '.')) {
                        all_dots = false;
                        break;
                    }
                }
                utf8_free(&sub_u);
                if (!all_dots || strcmp(sub_sb.data, "..") == 0) {
                    sb_append(&sb, sub_sb.data);
                } else {
                    sb_append_c(&sb, '.');
                }
                sb_free(&sub_sb);
                sb_init(&sub_sb, 1);
                sb_append_c(&sb, '/');
            }
            slash = true;
        } else {
            slash = false;
            sb_append_utf8(&sub_sb, ch);
        }
    }
    if (sub_sb.len > 0) {
        sb_append(&sb, sub_sb.data);
    }
    sb_free(&sub_sb);
    if (sb.len > 0 && (sb.data[sb.len - 1] == '\\' ||
                       sb.data[sb.len - 1] == '/')) { // FIXME Use UTF8 instead
        sb.data[sb.len - 1] = '\0';
    }
    return sb.data;
}

SHASH_PRIVATE char *path_remove_file_dup(char *path) {
    char *norm = normalize_path_dup(path);
    _utf8_str u = utf8_str(norm);

    for (int i = u.len - 1; i >= 0; i--) {
        if (utf8_eq_ascii(u.str[i], '/')) {
            char *p = norm;
            for (int j = 0; j < i; j++) {
                p += utf8_len(*p);
            }
            *p = 0;
            utf8_free(&u);
            return norm;
        }
    }

    utf8_free(&u);
    return norm;
}

SHASH_PRIVATE bool is_absolute(char *path) {
    if (!path) {
        return false;
    }
    char *norm = normalize_path_dup(path);
    _utf8_str u = utf8_str(norm);
    if (u.len == 0) {
        utf8_free(&u);
        return false;
    }
    if (utf8_eq_ascii(u.str[0], '/')) {
        utf8_free(&u);
        free(norm);
        return true;
    } else {
        if (u.len > 2 && utf8_is_ascii(u.str[0]) &&
            ((u.buffer[0] >= 'A' && u.buffer[0] <= 'Z') ||
             (u.buffer[0] >= 'a' && u.buffer[0] <= 'z')) &&
            utf8_eq_ascii(u.str[1], ':') && utf8_eq_ascii(u.str[2], '/')) {
            utf8_free(&u);
            free(norm);
            return true;
        }
        utf8_free(&u);
        free(norm);
        return false;
    }
}

static inline bool is_relative(char *path) { return !is_absolute(path); }

#ifdef _WIN32

SHASH_PRIVATE char *wide_to_utf8_dup(const wchar_t *wstr) {
    if (wstr == NULL) {
        return NULL;
    }

    size_t utf8_len = WideCharToMultiByte(CP_UTF8, // convert to UTF-8
                                          0,       // no special flags
                                          wstr,    // source UTF-16 string
                                          -1,   // -1: input is NUL-terminated
                                          NULL, // no output buffer yet
                                          0,    // request size only
                                          NULL, NULL // no default char
    );
    if (utf8_len == 0) {
        // conversion failed
        fprintf(stderr, "WideCharToMultiByte failed (err=%lu)\n",
                GetLastError());
        return NULL;
    }

    char *utf8 = (char *)xmalloc(utf8_len);

    int result =
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8, utf8_len, NULL, NULL);
    if (result == 0) {
        fprintf(stderr, "WideCharToMultiByte(2) failed (err=%lu)\n",
                GetLastError());
        free(utf8);
        return NULL;
    }

    return utf8;
}

SHASH_PRIVATE wchar_t *utf8_to_wide_dup(const char *utf8) {
    size_t len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *wide = (wchar_t *)xmalloc(len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len);
    return wide;
}

static inline void utf8_to_wide_fill(const char *utf8, wchar_t buffer[],
                                     size_t size) {
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buffer, size);
}
#endif

SHASH_PRIVATE char *join_path_dup(char *a, char *b) {
    if (!a || !b) {
        return NULL;
    }
    char *bnorm = normalize_path_dup(b);
    if (is_absolute(bnorm)) {
        return bnorm;
    }
    char *anorm = normalize_path_dup(a);
    int size = 0;
    StringBuilder sb;
    sb_init(&sb, strlen(b));
    StringBuilder sub_sb;
    sb_init(&sub_sb, 1);
    _utf8_str b_uni = utf8_str(b);
    for (int i = 0; i < b_uni.len; i++) {
        char *ch = b_uni.str[i];
        if (utf8_eq_ascii(ch, '/')) {
            if (strcmp(sub_sb.data, ".") != 0 &&
                strcmp(sub_sb.data, "..") != 0) {
                sb_append(&sb, sub_sb.data);
                sb_append_c(&sb, '/');
            } else if (strcmp(sub_sb.data, "..") == 0) {
                size--;
            }
            sb_clear(&sub_sb);
        } else {
            sb_append_utf8(&sub_sb, ch);
        }
    }
    if (sub_sb.len > 0) {
        sb_append(&sb, sub_sb.data);
    }
    free(bnorm);
    bnorm = dupstr(sb.data);
    sb_clear(&sb);
    utf8_free(&b_uni);
    sb_free(&sub_sb);
    _utf8_str a_uni = utf8_str(anorm);

    int end = strlen(anorm);
    for (int i = a_uni.len - 1; i >= 0; i--) {
        char *ch = a_uni.str[i];
        if (utf8_eq_ascii(ch, '/')) {
            size++;
            int idx = a_uni.len;
            int bytes = strlen(anorm);
            while (idx != i) {
                idx--;
                bytes -= utf8_len(*a_uni.str[idx]);
            }
            end = bytes;
        }
        if (size >= 0) {
            break;
        } else if (i == 0) {
            utf8_free(&a_uni);
            return bnorm;
        }
    }
    char *old_a = anorm;
    anorm = xmalloc(end + 1);
    memcpy(anorm, old_a, end);
    anorm[end] = 0;
    utf8_free(&a_uni);
    free(old_a);
    sb_free(&sb);
    int alen = strlen(anorm);
    int blen = strlen(bnorm);
    char *res = (char *)xmalloc(alen + blen + 2);
    memcpy(res, anorm, alen);
    res[alen] = '/';
    memcpy(res + alen + 1, bnorm, blen);
    res[alen + blen + 1] = 0;
    free(anorm);
    free(bnorm);
    return res;
}
SHASH_PRIVATE wchar_t *join_path_wide_dup(const wchar_t *a, const wchar_t *b) {
    char *anorm = wide_to_utf8_dup(a);
    char *bnorm = wide_to_utf8_dup(b);
    char *res = join_path_dup(anorm, bnorm);
    free(anorm);
    free(bnorm);
    wchar_t *wide = utf8_to_wide_dup(res);
    free(res);
    return wide;
}

SHASH_PRIVATE int count_leading_zeros(long num) {
    if (num == 0)
        return sizeof(long) * 8;
    int count = 0;
    while ((num & (1L << (sizeof(long) * 8 - 1))) == 0) {
        count++;
        num <<= 1;
    }
    return count;
}

SHASH_PRIVATE int min_i(int a, int b) { return a < b ? a : b; }

SHASH_PRIVATE int max_i(int a, int b) { return a > b ? a : b; }

// https://github.com/tommyettinger/jdkgdxds

#define LOAD_FACTOR 0.7
#define DEFAULT_CAPACITY (int)(LOAD_FACTOR * 64)

typedef struct {
    char **key_table;
    void **val_table;
    int capacity;
    int mask;
    int shift;
    float threshold;
    size_t size;
    size_t table_size;
} StringMap;

SHASH_PRIVATE int table_size(int capacity) {
    return 1 << -count_leading_zeros(
               max_i(2, (int)ceil(capacity / (double)LOAD_FACTOR)) - 1);
}

SHASH_PRIVATE StringMap *create_string_map(int capacity) {
    StringMap *map = (StringMap *)xmalloc(sizeof(StringMap));
    int size = table_size(capacity);
    map->threshold = (int)(size * LOAD_FACTOR);
    map->mask = size - 1;
    map->shift = count_leading_zeros(map->mask) + 32;
    map->key_table = (char **)xmalloc(size * sizeof(char *));
    map->val_table = (void **)xmalloc(size * sizeof(void *));
    map->size = 0;
    map->table_size = size;
    for (int i = 0; i < size; i++) {
        map->key_table[i] = 0;
    }

    return map;
}

SHASH_PRIVATE StringMap *string_map_copy(StringMap *origin) {

    StringMap *map = (StringMap *)xmalloc(sizeof(StringMap));
    int size = origin->table_size;
    map->threshold = origin->threshold;
    map->mask = origin->mask;
    map->shift = origin->shift;
    map->key_table = (char **)xmalloc(size * sizeof(char *));
    map->val_table = (void **)xmalloc(size * sizeof(void *));
    map->size = 0;
    map->table_size = size;
    for (int i = 0; i < size; i++) {
        map->key_table[i] = 0;
    }

    memcpy(map->key_table, origin->key_table, size * sizeof(char *));
    memcpy(map->val_table, origin->val_table, size * sizeof(void *));

    return map;
}

SHASH_PRIVATE void string_map_free(StringMap *map) {
    free(map->key_table);
    free(map->val_table);
    free(map);
}

SHASH_PRIVATE uint32_t fnv1a_hash(char *str) {
    const uint32_t FNV_prime = 0x01000193;        // 16777619
    const uint32_t FNV_offset_basis = 0x811C9DC5; // 2166136261

    uint32_t hash = FNV_offset_basis;
    size_t len = strlen(str);

    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= FNV_prime;
    }

    return hash;
}

SHASH_PRIVATE int string_map_place(StringMap *map, char *str) {
    return fnv1a_hash(str) & map->mask;
}

SHASH_PRIVATE int string_map_locate_key(StringMap *map, char *key) {
    for (int i = string_map_place(map, key);; i = i + 1 & map->mask) {
        char *other = map->key_table[i];
        if (utf8_eq(key, other))
            return i;
        if (!other)
            return ~i;
    }
}

SHASH_PRIVATE void string_map_put_resize(StringMap *map, char *key,
                                         void *value) {
    for (int i = string_map_place(map, key);; i = i + 1 & map->mask) {
        if (!map->key_table[i]) {
            map->key_table[i] = key;
            map->val_table[i] = value;
        }
    }
}

SHASH_PRIVATE void string_map_resize(StringMap *map, int new_size) {

    int old_cap = map->size;

    map->threshold = (int)(new_size * LOAD_FACTOR);
    map->mask = new_size - 1;
    map->shift = count_leading_zeros(map->mask) + 32;

    char **old_keys = map->key_table;
    void **old_vals = map->val_table;

    map->key_table = (char **)xmalloc(new_size * sizeof(char *));
    map->val_table = (void **)xmalloc(new_size * sizeof(void *));
    for (int i = 0; i < new_size; i++) {
        map->key_table[i] = 0;
    }

    if (map->size > 0) {
        for (int i = 0; i < old_cap; i++) {
            char *key = map->key_table[i];
            if (!key)
                string_map_put_resize(map, key, old_vals[i]);
        }
    }

    free(old_keys);
    free(old_vals);
}

SHASH_PRIVATE void *string_map_put(StringMap *map, char *key, void *value) {
    int i = string_map_locate_key(map, key);
    if (i >= 0) {
        void *old = map->val_table[i];
        map->val_table[i] = value;
        return old;
    }
    i = ~i;
    map->key_table[i] = key;
    map->val_table[i] = value;
    if (++map->size > map->threshold) {
        string_map_resize(map, map->table_size << 1);
    }
    return NULL;
}

SHASH_PRIVATE void *string_map_get(StringMap *map, char *key) {
    for (int i = string_map_place(map, key);; i = i + 1 & map->mask) {
        char *other = map->key_table[i];
        if (utf8_eq(key, other))
            return map->val_table[i];
        if (!other)
            return NULL;
    }
}

SHASH_PRIVATE bool string_map_contains(StringMap *map, char *key) {
    for (int i = string_map_place(map, key);; i = i + 1 & map->mask) {
        char *other = map->key_table[i];
        if (utf8_eq(key, other))
            return true;
        if (!other)
            return false;
    }
}

static const unsigned int GOOD_MULTIPLIERS[] = {
    0xEC6794E3, 0x9B89CD59, 0xDCA1C8D7, 0xC5F768E7, 0x92317571, 0x937CD501,
    0xE993C987, 0xD5567571, 0x85C8ADB5, 0xE6AC8B4F, 0xC21736F9, 0xFD890F79,
    0xC514D823, 0xF151575F, 0x8BDCE3EF, 0xA7F27B2F, 0x8C1EAA4F, 0xCCE4C43F,
    0x82E28415, 0xC6A39455, 0xE6245E51, 0xC33AFB2F, 0xBFA927CB, 0xAC11C8A3,
    0xC00E6AF1, 0xF98DDA5B, 0x8FA1F025, 0xF0CFFC71, 0xA49DC54B, 0xB3A7C3C3,
    0xC2F9C7BD, 0xE0CC8899, 0xB3B0A51D, 0xF01DF9A5, 0x9E7300F5, 0xA7E675F3,
    0xFE3FB283, 0xE0FF1497, 0xB2CE9603, 0xD9EF3FCD, 0xB7F3D71B, 0xE438BEA9,
    0xF16A6DCD, 0xA4613217, 0xAAE7C54B, 0xB56A208B, 0xBCA43B89, 0xEBE28BC7,
    0x8567101B, 0x9F45E6F1, 0xF95D5505, 0xCDCBFDE3, 0xECFA5363, 0xB449917B,
    0xE1D8CCA3, 0xE208D04F, 0xC0A33019, 0xCD722469, 0xC3D56C8B, 0xEC34F7A1,
    0xBF6C9497, 0xAE7244F5, 0xF3DEC4BF, 0xB79FD4FF, 0xFF54D7E7, 0xEE99B50F,
    0xB965E897, 0xE37B5231, 0xF9FBF639, 0x965B209B, 0xFB164EC9, 0xE33667EF,
    0x808498B7, 0xFFD4C7BD, 0xCF6740FB, 0x98EFE1B5, 0xA5CCFBCF, 0xB268E277,
    0xFF48E3EF, 0xAB8ECED1, 0x83B4DFC5, 0x8E4EA9ED, 0xDB6B35F7, 0x9DCAF41B,
    0xC2EEC5D5, 0xBBEB6F4F, 0x9645256D, 0x9BE82D2B, 0xC7C8533D, 0x8FA69905,
    0xE4F8CD59, 0xAFDBF6C1, 0xC17E533D, 0x80922B93, 0xF40F52B7, 0xB824EF5F,
    0x8EFBF295, 0xFA482B93, 0x8492ADC3, 0xBCBA2E15, 0xD6F7F7AB, 0x80A58489,
    0xB867400B, 0xD5D33021, 0xF43AAD83, 0xA495DEA3, 0xBF03ABE5, 0xFCE635CB,
    0xB4422C7D, 0xCB39CDBD, 0xB370E565, 0x8701C045, 0xCD01F09F, 0xEA326ABB,
    0x8DB96BBD, 0x97D3EA41, 0xE4B3EBB9, 0xA9D2159B, 0xF3EDEDDD, 0xE018F82D,
    0xE609C2E5, 0xFE852FB3, 0xC2107ECF, 0xD42AE479, 0x9E128095, 0xF38831AF,
    0xAF439D51, 0x87FF76C1, 0xD48BF7C7, 0x8099F855, 0xCBC5273F, 0x978BDC1D,
    0xEDEAD625, 0xB784AF47, 0x8C4C960F, 0xDC0CFB0D, 0xFCB95601, 0xA25AF703,
    0xE0F6C62B, 0xB9229497, 0x80B41AB9, 0xCEA1EDDB, 0xB5A7B713, 0xC7AA1AE7,
    0xED81746B, 0xAB08B11D, 0x8F63D7F3, 0x8FABDD75, 0x8F405B2D, 0xDD51AB15,
    0xD7131D63, 0xFCC13CA9, 0xE7D8B7D7, 0x83A0FC83, 0xD689CA89, 0xECE5E5D5,
    0xC1CB8BEB, 0xF1D565C9, 0xC1ADCC9D, 0xB2985335, 0xF28FB701, 0xE8F1FBCF,
    0xC8E7F03D, 0xF3AEC9B5, 0xCF030129, 0xB84DA7F3, 0x94A32A2B, 0xEB1A2609,
    0xA1E0543F, 0xFF96253F, 0x87604B41, 0xA1FE1C2F, 0xD5FBE8B5, 0xF0707F65,
    0xE81D17CD, 0x9D3430ED, 0xF44D7C2D, 0xFAAD5D75, 0x9ED5A1E3, 0xBA56FE47,
    0xFAB9E45B, 0x94DFAC1B, 0xC0A7D057, 0xC4E985F9, 0xA8612159, 0xE35C86ED,
    0xFA0C22AB, 0xE607294F, 0xCC11653B, 0xFAD1EED3, 0xA49D3AFF, 0xB7D7B6B1,
    0xC0A8E4EF, 0xAF255C59, 0xF101965F, 0xBA7DB033, 0x9AC1679B, 0xC1831603,
    0xE6F4AAB7, 0x8DC1D40F, 0xC076504F, 0xCC8965C7, 0xA562FE85, 0xEE138EB9,
    0xF0EE34C7, 0xEE739611, 0xFE191A7D, 0xCBBEE81D, 0xD0DE6BBD, 0xF6D38D31,
    0x9F620FFD, 0x82E63243, 0xF4A1FFF3, 0xAD1185C7, 0x83B2BF37, 0xADE3D033,
    0xD568B12D, 0x927B92DF, 0xF664655D, 0x9CB98587, 0x825DFCA3, 0xD5AC8F79,
    0xC79860D5, 0xD063D19B, 0xDE0C7DF7, 0x9C2759F5, 0xA856B25F, 0xB1BEB50F,
    0xCED724B3, 0xD68F0657, 0xC862D5DD, 0xACABAFB3, 0x9E01B893, 0x98FB4B61,
    0x87D6B58B, 0xCE42682D, 0xC8FF67C7, 0x8F531893, 0xCFB5FEA7, 0x83D18A7B,
    0xB07D3A2D, 0xB423727D, 0xAA333A2D, 0x978F92ED, 0x830CD2EB, 0xA5EBC713,
    0xD3C8C535, 0xDAE520E5, 0xCC356C4D, 0x8AE04AAD, 0xAC226E1D, 0x966E5FAB,
    0xD241CD23, 0x84487F11, 0xA5DE22F3, 0xE2061CD3, 0xBE5B9F0B, 0xC88E2807,
    0x8AEDE62B, 0xAF2D17D7, 0xC1188E6D, 0xD08851B5, 0xA979EC49, 0xA26FD4F7,
    0xFC92BDFB, 0xFB73BC8B, 0xB26FABE5, 0xC2F8BBC7, 0xC5AA2797, 0xB5511B61,
    0x8BCDD4B7, 0xC4431803, 0x853BC693, 0xFF544D9B, 0x8D32BB1B, 0xCF2CB46F,
    0xBCBECE53, 0xCF26F6F7, 0xE3C1B8CF, 0x92648319, 0xC29E60A3, 0x880E8E9B,
    0xD7336DA5, 0xDF391243, 0xA2945DB3, 0xB943F971, 0x8009F5F3, 0x844BAB95,
    0x8C515033, 0x9296AFF5, 0xA9288C65, 0xA0B1A807, 0xB725C529, 0xA36E8EC7,
    0xA59B6A83, 0xB1173B23, 0x91F6787F, 0x8F009B75, 0x93A815E7, 0xC465778F,
    0x9425DC8D, 0xCA564247, 0xDF3909A1, 0xC788231F, 0x854066D1, 0xAAA0CBBF,
    0xF4E86F33, 0xD1D3B9E7, 0xE81D90D5, 0xFB9F0613, 0xCD3E83D3, 0xC3A1CBF5,
    0xD4D8A629, 0xB1A74627, 0x9DF00FC5, 0xC7559721, 0xAA19AAB7, 0xF0A31CA7,
    0xBD355B25, 0xA3A55BE1, 0xF0855D59, 0xE935C8D7, 0xBF8F0567, 0x93912007,
    0xCE810209, 0x8E0CE38D, 0xA898701D, 0xE6B6789F, 0x901BFFFB, 0xCA0A9FDB,
    0xFC8F186F, 0xA8A4FFA5, 0xEA393429, 0xEF3AF87B, 0xB80947FD, 0xC2D190D3,
    0xA8DBCCBF, 0xF7A1B909, 0xA4CB2F61, 0xC5AD4B79, 0xF288FED7, 0xE45710E5,
    0x8A2E6A69, 0xE69BEE77, 0xF0CE6ED1, 0x95B94A41, 0xD2403F83, 0xD3A1919F,
    0x81668DEB, 0xEBA70489, 0xD0CDB4FF, 0xD81FD1D3, 0xB302C987, 0xABB672CD,
    0x9E55CBF3, 0xCF5B331D, 0xA8FA9803, 0x8BD6AD6F, 0xD0ED94DB, 0xC3BCF1AD,
    0xC30969E3, 0x847FE7BB, 0xCB0959AB, 0x8CF7D80F, 0xFAEFB6C3, 0xD9EFDB5D,
    0xA02B8A53, 0xCED41215, 0xDA322367, 0x8C2A253F, 0x88663643, 0xB8580E4B,
    0x83657DE7, 0xE4E04137, 0xF9E0C7DF, 0xA68C7791, 0xC17DC8F1, 0xC9247ACF,
    0xDC8DEE37, 0xA88A6439, 0x95E6BCF9, 0xDE0184B1, 0xE881C805, 0xCEA405C7,
    0x9A3AB6F9, 0xB520960B, 0xD83C14C5, 0xFF570117, 0x855FCDA5, 0xEB65580F,
    0xB1A10705, 0xFE1B43AD, 0xDA0B0115, 0xE2C5A9D9, 0xC9FB76BD, 0xDA6C84C9,
    0xB76FD153, 0xF3A9039B, 0xA49668EF, 0xE09F978B, 0xF701704F, 0xE39ECEF7,
    0xEDD14F51, 0xB1E0228B, 0xE78D0427, 0x9438B3D9, 0xB5CE57BB, 0xABFBDEB9,
    0x9CEC22C9, 0x9BE73B1B, 0x897954FF, 0xC6E3D5B7, 0xE0D3DC53, 0xE79030AB,
    0xA4E2AF8D, 0x86C337AD, 0xADCBDFA1, 0x8E2ED2B3, 0xB3A06767, 0xC179176F,
    0xE0503E4F, 0xD30A3B83, 0xDD8A7ED7, 0xB68E5DC9, 0xA00EC5B7, 0xB57AD749,
    0xA0148BD1, 0xEB8D7DB5, 0x8CE7A2FB, 0xA8D481A7, 0xF54EBE4F, 0xDFBF3899,
    0x8F7507B9, 0x8DAF8FB1, 0x83C514D9, 0xC5414787, 0xD5FA5B15, 0xD90AC247,
    0x864C3A75, 0x828E004F, 0xC18F9447, 0xF458E5D9, 0x950540CD, 0xF13D03B7,
    0xABDF22BF, 0xEF226C59, 0xACA4EEE7, 0x9D9532F7, 0xDB65787F, 0x86F7E439,
    0xA3AA8F0D, 0xEFBF06E5, 0xCC448CC9, 0x95C66415, 0xACBE0555, 0xCC211003,
    0xE72A6339, 0x8699F4CD, 0xCAC0C9DD, 0xCA5B050D, 0xCA3D45BF, 0xE73DB38D,
    0xAFA63E3F, 0xC94E3F59, 0xDAF09BD5, 0xCFCBE891, 0xFDA3293B, 0xDB41FBB9,
    0xE86CE16F, 0xA94D8587, 0xA6D2E6D1, 0xCA179FC9, 0xDC9D87BB, 0xC14D4C3D,
    0xF58C4C35, 0xB30ECEC3, 0xC4B12B3F, 0xE72A5A97, 0xF8CCB713, 0xF2406667,
    0xE0FC83A3, 0xCC8EAA37, 0x87668A63, 0xFC988415, 0xCC0B2619, 0xBE0EF94B,
    0xC75DEE2F, 0xFBDF3ED3, 0xF176FD55, 0xA8FFC28D, 0x83887061, 0xCD0A47AD,
    0xB672ADC5, 0xFAFF47A5, 0xA85E7F21, 0xCAA1ED55, 0xE9E5B3B7, 0x888A3D55,
    0xCCFED55F, 0xA3F9555B, 0x93A04925, 0xC905253F, 0xAF7525FB, 0xC427E9A9,
    0xA69E3A45, 0x84C86EE7, 0xAB058D3B, 0xB87E35EB, 0xA03786ED, 0x9AC482DB,
    0xB6815DDB, 0xC2E0B9F1,
};

typedef struct {
    int *key_table;
    void **val_table;
    int capacity;
    int mask;
    int shift;
    int hash_multiplier;
    float threshold;
    size_t size;
    size_t table_size;
} IntMap;

SHASH_PRIVATE IntMap *create_int_map(int capacity) {
    IntMap *map = (IntMap *)xmalloc(sizeof(IntMap));
    int size = table_size(capacity);
    map->threshold = (int)(size * LOAD_FACTOR);
    map->mask = size - 1;
    map->shift = count_leading_zeros(map->mask) + 32;
    map->hash_multiplier = GOOD_MULTIPLIERS[64 - map->shift];
    map->key_table = (int *)xmalloc(size * sizeof(int));
    map->val_table = (void **)xmalloc(size * sizeof(void *));
    map->size = 0;
    map->table_size = size;
    for (int i = 0; i < size; i++) {
        map->key_table[i] = 1 << 31;
    }

    return map;
}

SHASH_PRIVATE IntMap *int_map_copy(IntMap *origin) {
    IntMap *map = (IntMap *)xmalloc(sizeof(IntMap));
    int size = origin->table_size;
    map->threshold = origin->threshold;
    map->mask = origin->mask;
    map->shift = origin->shift;
    map->hash_multiplier = origin->hash_multiplier;
    map->key_table = (int *)xmalloc(size * sizeof(int));
    map->val_table = (void **)xmalloc(size * sizeof(void *));
    map->size = origin->size;
    map->table_size = size;
    for (int i = 0; i < size; i++) {
        map->key_table[i] = 1 << 31;
    }

    memcpy(map->key_table, origin->key_table, size * sizeof(int));
    memcpy(map->val_table, origin->val_table, size * sizeof(void *));

    return map;
}

SHASH_PRIVATE void int_map_free(IntMap *map) {
    free(map->key_table);
    free(map->val_table);
    free(map);
}

static inline int int_map_place(IntMap *map, int i) {
    return ((unsigned int)(i * map->hash_multiplier)) >> map->shift;
}

SHASH_PRIVATE int int_map_locate_key(IntMap *map, int key) {
    for (int i = int_map_place(map, key);; i = i + 1 & map->mask) {
        int other = map->key_table[i];
        int *addr = &key;
        if (key == other)
            return i;
        if (other == 1 << 31)
            return ~i;
    }
}

SHASH_PRIVATE void int_map_put_resize(IntMap *map, int key, void *value) {
    for (int i = int_map_place(map, key);; i = i + 1 & map->mask) {
        if (!map->key_table[i]) {
            map->key_table[i] = key;
            map->val_table[i] = value;
        }
    }
}

SHASH_PRIVATE void int_map_resize(IntMap *map, int new_size) {

    int old_cap = map->size;

    map->threshold = (int)(new_size * LOAD_FACTOR);
    map->mask = new_size - 1;
    map->shift = count_leading_zeros(map->mask) + 32;

    int *old_keys = map->key_table;
    void **old_vals = map->val_table;

    map->key_table = (int *)xmalloc(new_size * sizeof(int));
    map->val_table = (void **)xmalloc(new_size * sizeof(void *));
    for (int i = 0; i < new_size; i++) {
        map->key_table[i] = 1 << 31;
    }

    if (map->size > 0) {
        for (int i = 0; i < old_cap; i++) {
            int key = map->key_table[i];
            if (!key)
                int_map_put_resize(map, key, old_vals[i]);
        }
    }

    free(old_keys);
    free(old_vals);
}

SHASH_PRIVATE void *int_map_put(IntMap *map, int key, void *value) {
    int i = int_map_locate_key(map, key);
    if (i >= 0) {
        void *old = map->val_table[i];
        map->val_table[i] = value;
        return old;
    }
    i = ~i;
    map->key_table[i] = key;
    map->val_table[i] = value;
    if (++map->size > map->threshold) {
        int_map_resize(map, map->table_size << 1);
    }
    return NULL;
}

SHASH_PRIVATE void *int_map_get(IntMap *map, int key) {
    for (int i = int_map_place(map, key);; i = i + 1 & map->mask) {
        int other = map->key_table[i];
        if (key == other)
            return map->val_table[i];
        if (other == 1 << 31)
            return NULL;
    }
}

SHASH_PRIVATE bool int_map_contains(IntMap *map, int key) {
    for (int i = int_map_place(map, key);; i = i + 1 & map->mask) {
        int other = map->key_table[i];
        if (key == other)
            return true;
        if (other == 1 << 31)
            return false;
    }
}

//
// -=================================================================-
// -============================= Utils =============================-
// -=================================================================-
//

SHASH_PRIVATE Array *separate_paths_dup(char *var) {
    int size = 0;
    for (int i = 0; var[i] != 0;) {
        int len = utf8_len((unsigned char)var[i]);
        if (var[i] == ENV_PATH_SEPARATOR) {
            size++;
        }
        i += len;
    }
    if (size == 0) {
        Array *paths = (Array *)xmalloc(sizeof(Array));
        arr_init(paths, 1);
        arr_add(paths, var);
        return paths;
    }
    Array *paths = (Array *)xmalloc(sizeof(Array));
    arr_init(paths, size);
    int med_size = utf8_strlen(var) / size;
    StringBuilder sb;
    sb_init(&sb, med_size);
    for (int i = 0; i < utf8_strlen(var); i++) {
        const char *ch = utf8_char_at(var, i);
        if (!ch)
            break;
        int len = utf8_len((unsigned char)*ch);
        if (utf8_eq_ascii(ch, ENV_PATH_SEPARATOR)) {
            arr_add(paths, dupstr(sb.data));
            sb_clear(&sb);
        } else {
            sb_append_utf8(&sb, ch);
        }
    }
    sb_free(&sb);
    return paths;
}

SHASH_PRIVATE bool utoi(const char *u, int *out) {
    int len = utf8_strlen(u);
    if (len == 0) {
        return false;
    }
    int mul = 1;
    int start = 0;
    const char *first = utf8_char_at(u, 0);
    if (utf8_eq_ascii(first, '-') || utf8_eq_ascii(first, '+')) {
        start = 1;
        if (utf8_eq_ascii(first, '-')) {
            mul = -1;
        }
        if (len == 1) {
            return false;
        }
    }
    int num = 0;
    for (int i = start; i < len; i++) {
        const char *uch = utf8_char_at(u, i);
        if (utf8_is_ascii(uch)) {
            char ch = *uch;
            if (ch >= '0' && ch <= '9') {
                if (num > (INT_MAX - (ch - '0')) / 10) {
                    return false; // Overflow
                }
                num = num * 10 + (ch - '0');
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    *out = num;
    return true;
}

SHASH_PRIVATE void get_all_env(StringMap *map) {
    LPWCH envBlock = GetEnvironmentStringsW();
    if (!envBlock) {
        fprintf(stderr, "GetEnvironmentStringsW failed: %lu\n", GetLastError());
        return;
    }

    LPWCH current = envBlock;
    while (*current) {
        size_t len = wcslen(current);
        if (len == 0)
            break;

        wchar_t *eq = wcschr(current, L'=');
        if (eq) {
            *eq = L'\0';
            const wchar_t *wname = current;
            const wchar_t *wvalue = eq + 1;

            char *name = wide_to_utf8_dup(wname);
            char *value = wide_to_utf8_dup(wvalue);

            if (name && value) {
                string_map_put(map, name, value);
            }

            free(name);
            free(value);

            *eq = L'=';
        }

        current += (len + 1);
    }

    FreeEnvironmentStringsW(envBlock);
}

SHASH_PRIVATE char *remove_ext_dup(char *path) {
    _utf8_str u = utf8_str(path);
    int len = strlen(path);
    for (int i = u.len - 1; i >= 0; i--) {
        char *ch = u.str[i];
        len -= utf8_len(*ch);
        if (utf8_eq_ascii(ch, '.')) {
            break;
        }
    }
    utf8_free(&u);
    char *out = (char *)xmalloc(len + 1);
    memcpy(out, path + strlen(path) - len, len);
    out[len] = 0;
    return out;
}

SHASH_PRIVATE char *path_filename_dup(char *path) {
    char *norm = normalize_path_dup(path);
    _utf8_str u = utf8_str(norm);
    int len = strlen(norm);
    for (int i = u.len - 1; i >= 0; i--) {
        char *ch = u.str[i];
        len -= utf8_len(*ch);
        if (utf8_eq_ascii(ch, '/')) {
            break;
        }
    }
    utf8_free(&u);
    char *out = (char *)xmalloc(len + 1);
    memcpy(out, path + strlen(norm) - len, len);
    out[len] = 0;
    free(norm);
    return out;
}

SHASH_PRIVATE Array *get_files_dup(char *pattern) {
    wchar_t search_path[MAX_PATH];
    utf8_to_wide_fill(pattern, search_path, MAX_PATH);
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    hFind = FindFirstFileW(search_path, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    Array *files = (Array *)xmalloc(sizeof(Array));
    arr_init(files, 1);

    do {
        if (wcscmp(ffd.cFileName, L".") == 0 ||
            wcscmp(ffd.cFileName, L"..") == 0)
            continue;

        arr_add(files, wide_to_utf8_dup(ffd.cFileName));
    } while (FindNextFileW(hFind, &ffd) != 0);

    FindClose(hFind);

    return files;
}

SHASH_PRIVATE uint32_t get_attributes(char *path) {
    wchar_t *wide = utf8_to_wide_dup(path);
    DWORD attr = GetFileAttributesW(wide);
    free(wide);
    return attr;
}

static inline bool file_exists(uint32_t attrib) {
    return attrib != INVALID_FILE_ATTRIBUTES;
}

static inline bool is_dir(uint32_t attrib) {
    return (attrib & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

SHASH_PRIVATE void *open_file(char *path, bool truncate, bool create,
                              int *error) {
    wchar_t wide[MAX_PATH];
    utf8_to_wide_fill(path, wide, MAX_PATH);
    DWORD create_f = OPEN_EXISTING;
    DWORD truncate_f = FILE_APPEND_DATA;
    if (create) {
        create_f = CREATE_ALWAYS;
    }
    if (truncate) {
        truncate_f = GENERIC_WRITE;
    }
    void *handle = CreateFileW(wide, create_f, 0, NULL, truncate_f,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return NULL;
    }
    return handle;
}

static inline void close_file(void *file) { CloseHandle(file); }

SHASH_PRIVATE bool read_file(void *file, void **out_buf,
                             unsigned long long *out_size) {
    if (!file) {
        return false;
    }
    LARGE_INTEGER size;
    GetFileSizeEx(file, &size);
    if (size.QuadPart == INVALID_FILE_SIZE || size.QuadPart == 0) {
        return false;
    }

    LPVOID buf = HeapAlloc(GetProcessHeap(), 0, size.QuadPart);
    if (!buf) {
        return false;
    }

    DWORD read = 0;
    BOOL ok = ReadFile(file, buf, size.QuadPart, &read, NULL) &&
              (read == size.QuadPart);

    if (!ok) {
        HeapFree(GetProcessHeap(), 0, buf);
        return false;
    }

    *out_buf = buf;
    *out_size = size.QuadPart;
    return true;
}

SHASH_PRIVATE void free_read_buffer(void *buffer) {
    HeapFree(GetProcessHeap(), 0, buffer);
}

SHASH_PRIVATE bool write_file(void *file, const void *buf, uint32_t size) {
    if (!file) {
        return false;
    }
    DWORD written = 0;
    return WriteFile(file, buf, size, &written, NULL) && (written == size);
}

BOOL is_directory_empty(char *path) {
    char search_path[MAX_PATH];
    int path_len = strlen(path);
    if (path_len + 3 > MAX_PATH) {
        fprintf(stderr, "Path is too long");
        return FALSE;
    }
    memcpy(search_path, path, path_len);
    search_path[path_len] = '/';
    search_path[path_len + 1] = '*';
    search_path[path_len + 2] = 0;
    wchar_t *wide_base = utf8_to_wide_dup(path);
    wchar_t *wide = utf8_to_wide_dup(search_path);
    DWORD attrs = GetFileAttributesW(wide_base);
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        HeapFree(GetProcessHeap(), 0, wide);
        return FALSE; // Not a directory or doesn't exist
    }
    HeapFree(GetProcessHeap(), 0, wide_base);

    WIN32_FIND_DATAW find_data;
    HANDLE h_find = FindFirstFileW(wide, &find_data);

    if (h_find == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, wide);
        return TRUE;
    }

    do {
        if (wcscmp(find_data.cFileName, L".") != 0 &&
            wcscmp(find_data.cFileName, L"..") != 0) {
            HeapFree(GetProcessHeap(), 0, wide);
            FindClose(h_find);
            return FALSE;
        }
    } while (FindNextFileW(h_find, &find_data) != 0);

    FindClose(h_find);

    DWORD dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES) {
        HeapFree(GetProcessHeap(), 0, wide);
        return FALSE;
    }

    HeapFree(GetProcessHeap(), 0, wide);
    return TRUE;
}

SHASH_PRIVATE char *path_parent_dup(char *path) {
    char *norm = normalize_path_dup(path);
    int len = strlen(norm);
    _utf8_str norm_u = utf8_str(norm);
    for (int i = norm_u.len - 1; i >= 0; i--) {
        char *ch = norm_u.str[i];
        len -= utf8_len(*ch);
        if (utf8_eq_ascii(ch, '/')) {
            break;
        }
    }
    if (len == 0) {
        free(norm);
        return NULL;
    }
    char *res = xmalloc((len + 1) * sizeof(char));
    memcpy(res, norm, len);
    res[len] = 0;
    free(norm);
    return res;
}

/**
 * @brief Opens a @see ShashPipe.
 * @param[out] pipe The pipe to populate.
 */
void shash_pipe_open(ShashPipe *pipe) {
#ifdef _WIN32
    if (!CreatePipe(&pipe->wread, &pipe->wwrite, NULL, 0)) {
        fputs("shash: failed to open pipe.", stderr);
        return;
    }
#else
    if (pipe(pipe->posix) == -1) {
        fputs("shash: failed to open pipe.", stderr);
        return NULL;
    }
#endif
}

/**
 * @brief Creates a pipe from a file.
 * @param[in] path The path to the file.
 * @param[in] create Whether a new file should be created or not (error if file
   doesn't exist).
 * @param[in] truncate If false writing will append to the file.
 * @param[out] pipe The pipe to populate.
*/
SHASH_PRIVATE void shash_pipe_create_from_file(char *path, bool create,
                                               bool truncate, ShashPipe *pipe) {
#ifdef _WIN32
    wchar_t *wide = utf8_to_wide_dup(path);
    DWORD create_f = OPEN_EXISTING;
    DWORD truncate_f = FILE_APPEND_DATA;
    if (create) {
        create_f = CREATE_ALWAYS;
    }
    if (truncate) {
        truncate_f = GENERIC_WRITE;
    }
    void *handle = CreateFileW(wide, create_f, 0, NULL, truncate_f,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        free(wide);
        //*error = GetLastError();
        return;
    }
    free(wide);
    pipe->wwrite = handle;
    pipe->wread = handle;
#else
    int flags = O_RDWR;
    if (create) {
        flags |= O_CREAT;
    }
    if (truncate) {
        flags |= O_TRUNC;
    }
    int fd = open(path, flags, S_IRUSR | S_IWUSR);

    pipe->posix[0] = fd;
    pipe->posix[1] = fd;
#endif
}

/**
 * @brief Closes both ends of a pipe.
 * @param[in] pipe The pipe to close both ends of.
 */
void shash_pipe_close_all(ShashPipe *pipe) {
    if (!pipe) {
        return;
    }
#ifdef _WIN32
    if (!pipe->wwrite || !pipe->wread) {
        return;
    }
    CloseHandle(pipe->wwrite);
    CloseHandle(pipe->wread);
    pipe->wwrite = NULL;
    pipe->wread = NULL;
#else
    close(pipe->posix[1]);
    close(pipe->posix[0]);
#endif
}

/**
 * @brief Closes the write end of a pipe.
 * @param[in] pipe The pipe to close the write end of.
 */
void shash_pipe_close_write(ShashPipe *pipe) {
    if (!pipe) {
        return;
    }
#ifdef _WIN32
    if (!pipe->wwrite) {
        return;
    }
    CloseHandle(pipe->wwrite);
    pipe->wwrite = NULL;
#else
    close(pipe->posix[1]);
#endif
}

/**
 * @brief Closes the read end of a pipe.
 * @param[in] pipe The pipe to close the read end of.
 */
void shash_pipe_close_read(ShashPipe *pipe) {
    if (!pipe) {
        return;
    }
#ifdef _WIN32
    if (!pipe->wread) {
        return;
    }
    CloseHandle(pipe->wread);
    pipe->wread = NULL;
#else
    close(pipe->posix[0]);
#endif
}

/**
 * @brief Writes to a pipe from the input buffer.
 * @param[in] pipe The pipe to write into.
 * @param[in] buffer The buffer to write from.
 * @param[in] size The size of the buffer.
 * @return The number of bytes written
 */
size_t shash_pipe_write(ShashPipe *pipe, void *buffer, size_t size) {

    if (!pipe) {
        return -1;
    }
#ifdef _WIN32
    unsigned long s;
    WriteFile(pipe->wwrite, buffer, size, &s, NULL);
    return s;
#else
    return write(pipe->posix[1], buffer, size);
#endif
}

/**
 * @brief Reads from a pipe to the input buffer.
 * @param[in] pipe The pipe to read from.
 * @param[in] buffer The buffer to write the results in.
 * @param[in] size The number of bytes to read.
 * @param[out] bytes_read The actual number of bytes read.
 * @return bytes_read is returned for convinience.
 */
size_t shash_pipe_read(ShashPipe *pipe, void *buffer, size_t size,
                       size_t *bytes_read) {
    if (!pipe) {
        return -1;
    }
#ifdef _WIN32
    ReadFile(pipe->wread, buffer, size, (LPDWORD)bytes_read, NULL);
    return *bytes_read;
#else
    *bytes_read = read(pipe->posix[0], buffer, size);
    return *bytes_read;
#endif
}

/**
 * @brief Duplicates both ends of a pipe.
 * @param[in] pipe The pipe to duplicate from.
 * @param[out] out The output of the duplicated handles.
 */
void shash_pipe_duplicate_all(ShashPipe *pipe, ShashPipe *out) {
    if (!pipe || !out) {
        return;
    }
#ifdef _WIN32
    DuplicateHandle(GetCurrentProcess(), pipe->wwrite, GetCurrentProcess(),
                    &out->wwrite, 0, FALSE, DUPLICATE_SAME_ACCESS);
    DuplicateHandle(GetCurrentProcess(), pipe->wread, GetCurrentProcess(),
                    &out->wread, 0, FALSE, DUPLICATE_SAME_ACCESS);
#else
    perror("Not implemented.");
#endif
}

/**
 * @brief Duplicates the write end of a pipe.
 * @param[in] pipe The pipe to duplicate from.
 * @param[out] out The output of the duplicated handles.
 */
void shash_pipe_duplicate_write(ShashPipe *pipe, ShashPipe *out) {
    if (!pipe || !out) {
        return;
    }
#ifdef _WIN32
    DuplicateHandle(GetCurrentProcess(), pipe->wwrite, GetCurrentProcess(),
                    &out->wwrite, 0, FALSE, DUPLICATE_SAME_ACCESS);
#else
    perror("Not implemented.");
#endif
}

/**
 * @brief Duplicates the read end of a pipe.
 * @param[in] pipe The pipe to duplicate from.
 * @param[out] out The output of the duplicated handles.
 */
void shash_pipe_duplicate_read(ShashPipe *pipe, ShashPipe *out) {
    if (!pipe || !out) {
        return;
    }
#ifdef _WIN32
    out->wread = NULL;
    DuplicateHandle(GetCurrentProcess(), pipe->wread, GetCurrentProcess(),
                    &out->wread, 0, FALSE, DUPLICATE_SAME_ACCESS);
#else
    perror("Not implemented.");
#endif
}

/**
 * @brief Writes a NUL-terminated string to a pipe.
 * @param[in] pipe The pipe to write to.
 * @param[in] str The string to write.
 */
void shash_pipe_puts(ShashPipe *pipe, const char *str) {
    if (!pipe) {
        return;
    }
    size_t len = strlen(str);
    size_t off = 0;
    const size_t chunk = 4096;
#ifdef _WIN32
    while (off < len) {
        DWORD to_write = (DWORD)((len - off) > chunk ? chunk : (len - off));
        DWORD w = 0;
        BOOL ok = WriteFile(pipe->wwrite, str + off, to_write, &w, NULL);
        if (!ok || w == 0) { // error or end-of-stream
            break;
        }
        off += w;
    }
#else
    while (off < len) {
        ssize_t w = write(pipe->posix[1], str + off,
                          (len - off) > chunk ? chunk : (len - off));
        if (w <= 0) { // error or end-of-stream
            break;
        }
        off += w;
    }
#endif
}

/**
 * @brief Writes a character to a pipe.
 * @param[in] pipe The pipe to write to.
 * @param[in] character The character to write.
 */
void shash_pipe_putc(ShashPipe *pipe, char character) {
    if (!pipe) {
        return;
    }
#ifdef _WIN32
    WriteFile(pipe->wwrite, &character, 1, NULL, NULL);
#else
    write(pipe->posix[1], &character, 1);
#endif
}

/**
 * @brief Writes to a pipe with formatting.
 * @param[in] pipe The pipe to write to.
 * @param[in] format The format string (printf-style)
 * @param[in] ... Additional arguments matching format string.
 */
void shash_pipe_printf(ShashPipe *pipe, const char *restrict format, ...) {
    if (!pipe) {
        return;
    }
    va_list args;
    va_list args_copy;

    va_start(args, format);
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (len < 0) {
        va_end(args);
        fputs("shash: vsnprintf (length calculation) failed.", stderr);
        return;
    }

    char *buf = (char *)xmalloc(len + 1);
    len = vsnprintf(buf, len + 1, format, args);
    if (len < 0) {
        va_end(args);
        free(buf);
        fputs("shash: vsnprintf (string formatting) failed.", stderr);
        return;
    }

    shash_pipe_puts(pipe, buf);
    free(buf);
    va_end(args);
}

enum ShashQuoteKind {
    SHASH_QUOTE_NONE,
    SHASH_QUOTE_DOUBLE,
    SHASH_QUOTE_SINGLE,
};

/**
 * @brief Launches a process from given argv.
 * You have to close child side pipes on your own.
 * @param[in] shell The shell context.
 * @param[in] argv Arguments to launch the executable.
 * @param[in] argc Number of arguments.
 * @param[in] stdin_pipe stdin pipe.
 * @param[in] stdout_pipe stdout pipe.
 * @param[in] stderr_pipe stderr pipe.
 * @param[in] stderr_pipe Whether the @see Process should be flagged as a
background process.
|* @return Returns a pointer to a @see Process or NULL if launching the
executable failed.
*/
SHASH_PRIVATE Process *process_launch(Shell *shell, char **argv, int argc,
                                      ShashPipe *stdin_pipe,
                                      ShashPipe *stdout_pipe,
                                      ShashPipe *stderr_pipe, bool background) {

    HANDLE hStdInput = (HANDLE)stdin_pipe->wread;
    HANDLE hStdOutput = (HANDLE)stdout_pipe->wwrite;
    HANDLE hStdError = (HANDLE)stderr_pipe->wwrite;

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdInput;
    si.hStdOutput = hStdOutput;
    si.hStdError = hStdError;

    StringBuilder sb;
    sb_init(&sb, 256);
    for (int i = 0; i < argc; i++) {
        sb_append(&sb, argv[i]);
        if (i != argc - 1)
            sb_append_c(&sb, ' ');
    }
    LPWSTR wcmd = utf8_to_wide_dup(sb.data);
    LPWSTR wdir = utf8_to_wide_dup(shell->working_directory);
    sb_free(&sb);

    PROCESS_INFORMATION pi = {0};

    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL,
                             TRUE, // bInheritHandles - This is CRITICAL!
                             CREATE_NO_WINDOW, NULL, wdir, &si, &pi);
    free(wcmd);
    free(wdir);

    if (!ok) {
        fputs("shash: failed to launch executable.", stderr);
        return NULL;
    }

    CloseHandle(pi.hThread);

    Process *proc = (Process *)xmalloc(sizeof(Process));
    proc->pid = pi.dwProcessId;
    proc->background = background;
    proc->exit_code = -1;
    proc->windows_handle = pi.hProcess;

    return proc;
}

int process_wait(Process *proc) {
    if (!proc || !proc->windows_handle) {
        return -1;
    }

    WaitForSingleObject(proc->windows_handle, INFINITE);

    DWORD exit_code;
    GetExitCodeProcess(proc->windows_handle, &exit_code);
    proc->exit_code = (int)exit_code;

    CloseHandle(proc->windows_handle);
    proc->windows_handle = NULL;

    return proc->exit_code;
}

SHASH_PRIVATE char **split_script(char *text, int *len) {
    _utf8_str u = utf8_str(text);

    StringBuilder sb;
    sb_init(&sb, 16);

    Array lines;
    arr_init(&lines, 1);

    int escape_idx = -1;

    for (int i = 0; i < u.len; i++) {
        char *ch = u.str[i];
        if (escape_idx != i && sb.len > 0 &&
            (utf8_eq_ascii(ch, '\n') ||
             (escape_idx != i && utf8_eq_ascii(ch, ';')))) {
            arr_add(&lines, utf8_strip_dup(sb.data));
            sb_clear(&sb);
        } else if (escape_idx != i && utf8_eq_ascii(ch, '\\')) {
            escape_idx = i + 1;
        } else {
            sb_append_utf8(&sb, ch);
        }
    }
    if (sb.len > 0) {
        arr_add(&lines, utf8_strip_dup(sb.data));
    }
    utf8_free(&u);
    sb_free(&sb);

    *len = lines.size;
    return (char **)lines.data;
}

SHASH_PRIVATE char **split_line(char *line, int *len) {
    _utf8_str u = utf8_str(line);

    StringBuilder sb;
    sb_init(&sb, 16);

    Array sections;
    arr_init(&sections, 1);

    enum ShashQuoteKind quote = SHASH_QUOTE_NONE;
    int escape_idx = -1;

    for (int i = 0; i < u.len; i++) {
        char *ch = u.str[i];
        int rem = u.len - i - 1;
        if (sb.len > 0 && quote == SHASH_QUOTE_NONE && utf8_eq_ascii(ch, ' ')) {
            arr_add(&sections, utf8_strip_dup(sb.data));
            sb_clear(&sb);
        } else if (escape_idx != i && utf8_eq_ascii(ch, '\\')) {
            escape_idx = i + 1;
        } else if (escape_idx != i && quote != SHASH_QUOTE_DOUBLE &&
                   utf8_eq_ascii(ch, '\'')) {
            if (quote == SHASH_QUOTE_NONE) {
                quote = SHASH_QUOTE_SINGLE;
            } else {
                quote = SHASH_QUOTE_NONE;
            }
        } else if (escape_idx != i && quote != SHASH_QUOTE_SINGLE &&
                   utf8_eq_ascii(ch, '"')) {
            if (quote == SHASH_QUOTE_NONE) {
                quote = SHASH_QUOTE_DOUBLE;
            } else {
                quote = SHASH_QUOTE_NONE;
            }
        } else {
            sb_append_utf8(&sb, ch);
        }
    }
    if (sb.len > 0) {
        arr_add(&sections, utf8_strip_dup(sb.data));
    }
    utf8_free(&u);
    sb_free(&sb);

    *len = sections.size;
    return (char **)sections.data;
}

enum ShashShellOperation {
    SHASH_SHELL_OP_NONE,
    SHASH_SHELL_OP_DAEMON,
    SHASH_SHELL_OP_LOGICAL_AND,
    SHASH_SHELL_OP_LOGICAL_OR,
    SHASH_SHELL_OP_PIPE,
    SHASH_SHELL_OP_TRUNCATE,
    SHASH_SHELL_OP_APPEND,
    SHASH_SHELL_OP_INPUT,
};

SHASH_PRIVATE enum ShashShellOperation get_operation(char *cmd) {
    if (!cmd) {
        return SHASH_SHELL_OP_NONE;
    }
    if (strcmp(cmd, "&") == 0)
        return SHASH_SHELL_OP_DAEMON;
    else if (strcmp(cmd, "&&") == 0)
        return SHASH_SHELL_OP_LOGICAL_AND;
    else if (strcmp(cmd, "|") == 0)
        return SHASH_SHELL_OP_PIPE;
    else if (strcmp(cmd, "||") == 0)
        return SHASH_SHELL_OP_LOGICAL_OR;
    else if (strcmp(cmd, ">") == 0)
        return SHASH_SHELL_OP_TRUNCATE;
    else if (strcmp(cmd, ">>") == 0)
        return SHASH_SHELL_OP_APPEND;
    return SHASH_SHELL_OP_NONE;
}

SHASH_PRIVATE void format_sub(Shell *shell, _utf8_str *u, StringBuilder *sb,
                              int *k) {
    unsigned char ascii = utf8_is_ascii(u->str[*k]);
    if (ascii == '(') {
        if (*k == u->len - 1) {
            sb_append(sb, "ERR");
            return;
        }
        (*k)++;
        StringBuilder csb;
        sb_init(&csb, 1);
        while (ascii != ')' && *k != u->len - 1) {
            ascii = utf8_is_ascii(u->str[*k]);
            if (ascii != '$') {
                sb_append_utf8(&csb, u->str[*k]);
            }
            (*k)++;
            if (ascii == '$') {
                format_sub(shell, u, &csb, k);
            }
        }
        ShashPipe stdin_pipe = {0}, stdout_pipe = {0}, stderr_pipe = {0};
        make_user_pipes(&stdin_pipe, &stdout_pipe, &stderr_pipe);
        shash_pipe_close_write(&stdin_pipe);
        shell_run(shell, csb.data, csb.len, &stdin_pipe, &stdout_pipe,
                  &stderr_pipe);
        shash_pipe_close_write(&stdout_pipe);
        shash_pipe_close_write(&stderr_pipe);
        size_t bytes_read;
        char chunk[4096];
        while ((long)shash_pipe_read(&stdout_pipe, chunk, 4096, &bytes_read) >
               0) {
            for (long i = 0; i < (long)bytes_read; i++) {
                sb_append_c(sb, chunk[i]);
            }
        }
        shash_pipe_close_read(&stdout_pipe);
        shash_pipe_close_read(&stderr_pipe);
        sb_free(&csb);

    } else {
        StringBuilder key_sb;
        sb_init(&key_sb, *k);
        while (!((ascii >= 'a' && ascii <= 'z') ||
                 (ascii >= 'A' && ascii <= 'Z') || ascii == '_' ||
                 (ascii >= '0' && ascii <= '9')) ||
               *k == u->len - 1) {
            ascii = utf8_is_ascii(u->str[*k]);
            sb_append_utf8(&key_sb, u->str[*k]);
            (*k)++;
        }
        if (string_map_contains(shell->environment_variables, key_sb.data)) {
            sb_append(sb, (char *)string_map_get(shell->environment_variables,
                                                 key_sb.data));
        } else if (string_map_contains(shell->global, key_sb.data)) {
            sb_append(sb, (char *)string_map_get(shell->global, key_sb.data));
        } else {
            sb_append(sb, "NONE");
        }
        sb_free(&key_sb);
    }
}

SHASH_PRIVATE char *format_command(Shell *shell, char *script, char **error) {

    _utf8_str u = utf8_str(script);
    StringBuilder sb;
    sb_init(&sb, u.len);
    for (int j = 0; j < u.len; j++) {
        char *ch = u.str[j];
        int rem = u.len - j - 1;
        bool catched = false;
        if (utf8_eq_ascii(ch, '$')) {
            int k = j + 1;
            unsigned char ascii = utf8_is_ascii(u.str[k]);
            format_sub(shell, &u, &sb, &k);
            j = k;
        } else if (!catched) {
            sb_append_utf8(&sb, ch);
        }
    }
    char *s0 = dupstr(sb.data);
    sb_free(&sb);

    return s0;
}

SHASH_PRIVATE char **expand_braces(_utf8_str *u, int *size, char **error) {
#define RETURN_ERROR(msg)                                                      \
    *error = msg;                                                              \
    sb_free(&sb);                                                              \
    sb_free(&cb);                                                              \
    arr_free(&combinations);                                                   \
    arr_free(&mutations_arr);                                                  \
    return NULL;

    int num_combinations = 0;
    Array combinations;
    arr_init(&combinations, 1);
    int escape_idx = -1;
    StringBuilder sb;
    sb_init(&sb, u->len);
    *size = 1;
    for (int j = 0; j < u->len; j++) {
        char *ch = u->str[j];
        int rem = u->len - j - 1;
        bool catched = false;

        int is_interpolate = 0;
        bool mode_set = false;

        Array mutations_arr;
        arr_init(&mutations_arr, 1);

        if (utf8_eq_ascii(ch, '\\') && escape_idx != j) {
            escape_idx = j + 1;
        } else if (utf8_eq_ascii(ch, '{') && escape_idx != j) {
            bool closed = false;
            StringBuilder cb;
            sb_init(&cb, 1);
            for (int k = j + 1; k < u->len; k++) {
                char *cur = u->str[k];
                if (utf8_eq_ascii(cur, '}') && escape_idx != k) {
                    j = k;
                    closed = true;
                    if (is_interpolate == 1) {
                        arr_free(&mutations_arr);
                        RETURN_ERROR("Invalid interpolation \".\" instead "
                                     "of \"..\"");
                    }
                    if (is_interpolate == 2) {
                        int start, end;
                        utoi((char *)mutations_arr.data[0], &start);
                        bool end_is_num = utoi(cb.data, &end);
                        if (!end_is_num) {
                            RETURN_ERROR("Invalid number.");
                        }
                        for (int i = start + 1; i < end; i++) {
                            int size = snprintf(NULL, 0, "%d", i);
                            char *cur_num = (char *)xmalloc(size + 1);

                            if (cur_num == NULL) {
                                RETURN_ERROR("Failed to expand braces.");
                            }

                            snprintf(cur_num, size + 1, "%d", i);

                            arr_add(&mutations_arr, cur_num);
                        }
                    }
                    arr_add(&mutations_arr, dupstr(cb.data));
                    sb_clear(&cb);
                    for (int l = 0; l < mutations_arr.size; l++) {
                        StringBuilder copy = sb_copy_all(&sb);
                        sb_append(&copy, (char *)mutations_arr.data[l]);
                        sb_append_utf8_str(&copy, u, u->len - j - 1, j + 1);
                        _utf8_str uo = utf8_str(copy.data);
                        int sizeo = 0;
                        char **inner = expand_braces(&uo, &sizeo, error);
                        if (!inner) {
                            sb_free(&sb);
                            sb_free(&cb);
                            sb_free(&copy);
                            arr_free(&combinations);
                            return NULL;
                        }
                        *size += sizeo;
                        for (int u = 0; u < sizeo; u++) {
                            arr_add(&combinations, inner[u]);
                        }
                        free(inner);
                        sb_free(&copy);
                    }
                    sb_free(&sb);
                    sb_free(&cb);
                    return (char **)combinations.data;
                } else if (utf8_eq_ascii(cur, '{') && escape_idx != k) {
                    RETURN_ERROR("Invalid \"{\" inside a combination");
                } else if (utf8_eq_ascii(cur, '.') && escape_idx != k) {
                    if (cb.len == 0) {
                        RETURN_ERROR("Invalid usage empty combination.");
                    }
                    if (mode_set) {
                        char *e = "Mixing .. and , is invalid.";
                        if (is_interpolate) {
                            e = "Invalid combination \"...\"";
                        }
                        RETURN_ERROR(e);
                    }
                    is_interpolate++;
                    if (is_interpolate == 2) {
                        int num;
                        bool is_num = utoi(cb.data, &num);
                        if (!is_num) {
                            RETURN_ERROR("Invalid number.");
                        }
                        arr_add(&mutations_arr, dupstr(cb.data));
                        sb_clear(&cb);
                        mode_set = true;
                    }
                } else if (utf8_eq_ascii(cur, ',') && escape_idx != k) {
                    if (cb.len == 0) {
                        RETURN_ERROR("Invalid usage empty combination.");
                    }
                    arr_add(&mutations_arr, dupstr(cb.data));
                    sb_clear(&cb);
                    if (is_interpolate > 0) {
                        RETURN_ERROR("Mixing .. and , is invalid.");
                    }
                    mode_set = true;
                } else {
                    if (is_interpolate == 1) {
                        RETURN_ERROR("Expected \"..\" instead of \".\"");
                    }
                    sb_append_utf8(&cb, cur);
                }
            }
            sb_free(&cb);
            if (closed) {
                break;
            } else {
                RETURN_ERROR("Combination was not closed");
            }
        } else {
            sb_append_utf8(&sb, ch);
        }
    }
    arr_add(&combinations, dupstr(sb.data));
    sb_free(&sb);
#undef RETURN_ERROR
    return (char **)combinations.data;
}

SHASH_PRIVATE char **make_expansions(Shell *shell, char **argv, int argc,
                                     int *size, char **error) {
    *size = 0;
    Array args;
    arr_init(&args, argc);
    for (int i = 0; i < argc; i++) {
        _utf8_str u = utf8_str(argv[i]);
        int len = 0;
        char **expansion = expand_braces(&u, &len, error);
        if (!expansion) {
            return NULL;
        }
        for (int i = 0; i < len; i++) {
            arr_add(&args, expansion[i]);
        }
        free(expansion);
    }

    char **res = xmalloc(*size * sizeof(char *));
    memcpy(res, args.data, args.size * sizeof(char *));

    return res;
}

SHASH_PRIVATE bool is_executable(Shell *shell, char *str) {
    Array *pathext = (Array *)shell->pathext;
    char *str_l = utf8_lower_dup(str);
    for (int i = 0; i < pathext->size; i++) {
        char *ext_l = utf8_lower_dup(arr_get(pathext, i));
        if (utf8_endswith(str_l, ext_l)) {
            free(ext_l);
            return true;
        }
        free(ext_l);
    }
    return false;
}

typedef struct {
    Command command;
    Shell *shell;
    char **argv;
    int argc;
    ShashPipe *stdin_pipe;
    ShashPipe *stdout_pipe;
    ShashPipe *stderr_pipe;
} ShashBackgroundData;

SHASH_PRIVATE DWORD WINAPI run_internal_background(LPVOID param) {
    ShashBackgroundData *data = (ShashBackgroundData *)param;
    data->command(data->shell, data->argv, data->argc, data->stdin_pipe,
                  data->stdout_pipe, data->stderr_pipe);
    shash_pipe_close_read(data->stdin_pipe);
    shash_pipe_close_write(data->stdout_pipe);
    shash_pipe_close_write(data->stderr_pipe);
    free(param);
    return 0;
}

SHASH_PRIVATE void *win_error_to_str(uint32_t err) {
    LPVOID lpMsgBuf;

    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, (LPSTR)&lpMsgBuf, 0, NULL);

    return lpMsgBuf;
}

SHASH_PRIVATE Process *route_command(Shell *shell, char **argv, int argc,
                                     ShashPipe *stdin_pipe,
                                     ShashPipe *stdout_pipe,
                                     ShashPipe *stderr_pipe, bool background) {
    if (!argv || !*argv || argc == 0)
        return NULL;
    char *command = normalize_path_dup(argv[0]);
    _utf8_str command_u = utf8_str(command);
    if (utf8_contains_ascii_char(&command_u, '/')) {
        char *file = command;
        bool is_relative = !is_absolute(command);
        if (is_relative) {
            file = join_path_dup(shell->working_directory, command);
        }
        Process *prc = process_launch(shell, argv, argc, stdin_pipe,
                                      stdout_pipe, stderr_pipe, background);
        if (!background) {
            process_wait(prc);
        }
        if (is_relative) {
            free(file);
        }
    } else {
        StringMap *command_map = ((StringMap *)shell->commands);
        if (string_map_contains(command_map, command)) {
            void *cmd_p = string_map_get(command_map, command);
            char *out = NULL;
            int exit_code = 0;
            void *thread_id = NULL;
            ShashPipe *u_stdin_pipe = xmalloc(sizeof(ShashPipe));
            ShashPipe *u_stdout_pipe = xmalloc(sizeof(ShashPipe));
            ShashPipe *u_stderr_pipe = xmalloc(sizeof(ShashPipe));
            shash_pipe_duplicate_read(stdin_pipe, u_stdin_pipe);
            shash_pipe_duplicate_write(stdout_pipe, u_stdout_pipe);
            shash_pipe_duplicate_write(stderr_pipe, u_stderr_pipe);
            if (background) {
                ShashBackgroundData *data =
                    xmalloc(sizeof(ShashBackgroundData));
                data->command = (Command)cmd_p;
                data->shell = shell;
                data->argv = argv;
                data->argc = argc;
                data->stdin_pipe = u_stdin_pipe;
                data->stdout_pipe = u_stdout_pipe;
                data->stderr_pipe = u_stderr_pipe;
                thread_id = CreateThread(NULL, 0, run_internal_background, data,
                                         0, NULL);
            } else {
                exit_code = ((Command)cmd_p)(shell, argv, argc, u_stdin_pipe,
                                             u_stdout_pipe, u_stderr_pipe);
                shash_pipe_close_read(u_stdin_pipe);
                shash_pipe_close_write(u_stdout_pipe);
                shash_pipe_close_write(u_stderr_pipe);
            }

            Process *prc = xmalloc(sizeof(Process));
            prc->exit_code = exit_code;
            prc->pid = 0;
            prc->thread_id = thread_id;
            prc->internal = true;
            prc->background = background;
            utf8_free(&command_u);
            free(command);
            return prc;
        } else {
            for (int i = 0; i < ((Array *)shell->path)->size; i++) {
                char *p = arr_get(shell->path, i);
                int plen = strlen(p);
                char *pattern = (char *)xmalloc(plen + 3);
                if (!pattern) {
                    continue;
                }
                memcpy(pattern, p, plen);
                pattern[plen] = '/';
                pattern[plen + 1] = '*';
                pattern[plen + 2] = 0;
                Array *files = get_files_dup(pattern);
                for (int j = 0; j < files->size; j++) {
                    char *name = arr_get(files, j);
                    char *no_ext = remove_ext_dup(name);
                    if (strcmp(no_ext, command) == 0) {
                        char *join = join_path_dup(p, name);
                        uint32_t attrib = get_attributes(join);
                        if (file_exists(attrib) && !is_dir(attrib) &&
                            is_executable(shell, join)) {
                            char *old = argv[0];
                            utf8_free(&command_u);
                            free(command);
                            free(join);
                            free(name);
                            arr_free(files);
                            free(pattern);
                            argv[0] = join;
                            Process *prc = process_launch(
                                shell, argv, argc, stdin_pipe, stdout_pipe,
                                stderr_pipe, background);
                            if (!background) {
                                process_wait(prc);
                            }
                            int_map_put(shell->process_table, prc->pid, prc);
                            argv[0] = old;
                            return prc;
                        }
                        free(join);
                    }
                    free(no_ext);
                }
                arr_free(files);
                free(pattern);
            }
        }
    }
    utf8_free(&command_u);
    free(command);
    return NULL;
}

static inline char *shell_path_normalize_dup(Shell *shell, char *path) {
    return join_path_dup(shell->working_directory, path);
}

SHASH_PRIVATE wchar_t *shell_path_normalize_wide_dup(Shell *shell, char *path) {
    char *p = shell_path_normalize_dup(shell, path);
    wchar_t *wide = utf8_to_wide_dup(p);
    free(p);
    return wide;
}

SHASH_PRIVATE Array *group_by_dup(char **tokens, int num_tokens,
                                  char *separator) {
    Array *result = xmalloc(sizeof(Array));
    arr_init(result, 1);
    Array build;
    arr_init(&build, 1);
    for (int i = 0; i < num_tokens; i++) {
        if (strcmp(tokens[i], separator) == 0) {
            Array *group = xmalloc(sizeof(Array));
            arr_init(group, build.size);
            group->size = build.size;
            memcpy(group->data, build.data, build.size * sizeof(void *));
            arr_add(result, group);
            arr_clear(&build);
        } else {
            arr_add(&build, tokens[i]);
        }
    }
    if (build.size != 0) {
        Array *group = xmalloc(sizeof(Array));
        arr_init(group, build.size);
        group->size = build.size;
        memcpy(group->data, build.data, build.size * sizeof(void *));
        arr_add(result, group);
        arr_clear(&build);
    }
    arr_free(&build);
    return result;
}

SHASH_PRIVATE void remove_str_arr_idx(char ***arr, int *len, size_t idx,
                                      size_t size) {
    size_t el = sizeof(char *);
    char **n_arr = xmalloc(el * (*len - size));
    memcpy(n_arr, *arr, idx * el);
    memcpy(n_arr + idx, *arr + idx + size, el * (*len - idx - size));
    free(*arr);
    *arr = n_arr;
    *len -= size;
}

SHASH_PRIVATE int execute_simple_command(Shell *shell, char **tokens,
                                         int num_tokens, ShashPipe *stdin_pipe,
                                         ShashPipe *stdout_pipe,
                                         ShashPipe *stderr_pipe) {

    char **argv = xmalloc(sizeof(char *) * num_tokens);
    memcpy(argv, tokens, sizeof(char *) * num_tokens);
    int argc = num_tokens;

    ShashPipe *final_stdin = stdin_pipe;
    ShashPipe *final_stdout = stdout_pipe;
    // TODO handle 2> stderr redirection

    ShashPipe file_in, file_out;
    bool file_in_opened = false, file_out_opened = false;

    for (int i = argc - 1; i >= 0; i--) {
        enum ShashShellOperation op = get_operation(argv[i]);
        if (op == SHASH_SHELL_OP_INPUT) { // '<'
            // ... (error handling) ...
            shash_pipe_create_from_file(argv[i + 1], false, false, &file_in);
            final_stdin = &file_in;
            file_in_opened = true;
            // Remove '<' and 'filename' from argv
            remove_str_arr_idx(&argv, &argc, i, 2);
        } else if (op == SHASH_SHELL_OP_TRUNCATE) { // '>'
            // ... (error handling) ...
            shash_pipe_create_from_file(argv[i + 1], true, true, &file_out);
            final_stdout = &file_out;
            file_out_opened = true;
            remove_str_arr_idx(&argv, &argc, i, 2);
        } else if (op == SHASH_SHELL_OP_APPEND) { // '>>'
            // ... (error handling) ...
            shash_pipe_create_from_file(argv[i + 1], true, false, &file_out);
            final_stdout = &file_out;
            file_out_opened = true;
            remove_str_arr_idx(&argv, &argc, i, 2);
        }
    }

    Process *prc = route_command(shell, argv, argc, final_stdin, final_stdout,
                                 stderr_pipe, false);
    int exit_code = prc->exit_code;

    if (file_in_opened)
        shash_pipe_close_all(&file_in);
    if (file_out_opened)
        shash_pipe_close_all(&file_out);
    free(argv);

    return exit_code;
}

SHASH_PRIVATE int execute_pipeline(Shell *shell, char **tokens, int num_tokens,
                                   ShashPipe *stdin_pipe,
                                   ShashPipe *stdout_pipe,
                                   ShashPipe *stderr_pipe) {
    Array *commands = group_by_dup(tokens, num_tokens, "|");
    if (commands->size == 1) {
        Array *cmd = (Array *)arr_get(commands, 0);
        int exit_code =
            execute_simple_command(shell, (char **)cmd->data, cmd->size,
                                   stdin_pipe, stdout_pipe, stderr_pipe);
        arr_free(cmd);
        arr_free(commands);
        return exit_code;
    }

    int num_pipes = commands->size - 1;
    Process **procs = xmalloc(sizeof(Process *) * commands->size);
    ShashPipe *pipes = xmalloc(sizeof(ShashPipe) * num_pipes);

    ShashPipe *next_cmd_stdin =
        stdin_pipe; // First command reads from the original stdin

    for (int i = 0; i < commands->size - 1; i++) {
        shash_pipe_open(&pipes[i]);

        Array *cmd = arr_get(commands, i);
        procs[i] =
            route_command(shell, (char **)cmd->data, cmd->size, next_cmd_stdin,
                          &pipes[i], stderr_pipe, true); // true = launch async

        shash_pipe_close_write(&pipes[i]);

        next_cmd_stdin = &pipes[i];
    }

    int last_cmd_idx = commands->size - 1;
    Array *last_cmd = (Array *)arr_get(commands, last_cmd_idx);
    procs[last_cmd_idx] = route_command(
        shell, (char **)last_cmd->data, last_cmd->size, next_cmd_stdin,
        stdout_pipe, stderr_pipe, true); // true = launch async

    int last_exit_code = 0;
    for (int i = 0; i < commands->size; i++) {
        Process *proc = procs[i];
        if (proc->internal) {
            WaitForSingleObject(proc->thread_id, INFINITE);
        } else {
            int exit_code = process_wait(procs[i]);
            if (i == commands->size - 1) {
                last_exit_code = exit_code;
            }
        }
    }

    for (int i = 0; i < num_pipes; i++) {
        shash_pipe_close_read(&pipes[i]);
    }
    free(procs);
    free(pipes);
    arr_free(commands);

    return last_exit_code;
}

SHASH_PRIVATE int process_line(Shell *shell, char **tokens, int num_tokens,
                               ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                               ShashPipe *stderr_pipe) {
    int split_point = -1;
    enum ShashShellOperation logical_op = SHASH_SHELL_OP_NONE;
    for (int i = 0; i < num_tokens; i++) {
        enum ShashShellOperation op = get_operation(tokens[i]);
        if (op == SHASH_SHELL_OP_LOGICAL_AND ||
            op == SHASH_SHELL_OP_LOGICAL_OR) {
            split_point = i;
            logical_op = op;
            break;
        }
    }

    int argc;
    char **argv = make_expansions(shell, tokens, num_tokens, &argc, NULL);
    bool temp_assign = num_tokens > 1;
    Array temp_names;
    Array temp_vals;
    arr_init(&temp_names, argc);
    arr_init(&temp_vals, argc);
    while (argc > 0) {
        char *assignment = *argv;
        _utf8_str u = utf8_str(assignment);
        StringBuilder name_sb;
        StringBuilder val_sb;
        sb_init(&name_sb, u.nb / 2);
        sb_init(&val_sb, u.nb / 2);
        bool state_is_val = false;
        for (int i = 0; i < u.len; i++) {
            char *ch = u.str[i];
            int ascii = utf8_is_ascii(ch);
            if (!state_is_val && ascii == '=') {
                state_is_val = true;
            } else if (state_is_val) {
                sb_append_utf8(&val_sb, ch);
            } else {
                if ((ascii >= 'a' && ascii <= 'z') ||
                    (ascii >= 'A' && ascii <= 'Z') || ascii == '_' ||
                    (i > 0 && ascii >= '0' && ascii <= '9')) {
                    sb_append_utf8(&name_sb, ch);
                } else {
                    break;
                }
            }
        }
        wchar_t *name = utf8_to_wide_dup(name_sb.data);
        if (temp_assign) {
            arr_add(&temp_names, name);
            uint32_t val_size = GetEnvironmentVariableW(name, NULL, 0);
            if (val_size == 0) {
                arr_add(&temp_vals, NULL);
            } else {
                wchar_t *val = xmalloc(val_size * sizeof(wchar_t));
                GetEnvironmentVariableW(name, val, val_size);
                arr_add(&temp_vals, val);
            }
        } else {
            string_map_put(shell->global, dupstr(name_sb.data),
                           dupstr(val_sb.data));
        }
        wchar_t *val = utf8_to_wide_dup(val_sb.data);
        SetEnvironmentVariableW(name, val);
        free(val);
        if (!temp_assign) {
            free(name);
        }
        sb_free(&name_sb);
        sb_free(&val_sb);
        argv++;
        argc--;
    }
    if (split_point == -1) {
        if (temp_assign) {
            for (int i = 0; i < temp_names.size; i++) {
                SetEnvironmentVariableW(arr_get(&temp_names, i),
                                        arr_get(&temp_vals, i));
            }
        }
        arr_free(&temp_names);
        arr_free(&temp_vals);
        return execute_pipeline(shell, argv, argc, stdin_pipe, stdout_pipe,
                                stderr_pipe);
    } else {
        char **lhs_tokens = argv;
        int lhs_num_tokens = split_point;
        char **rhs_tokens = argv + split_point + 1;
        int rhs_num_tokens = argc - split_point - 1;

        int lhs_exit_code =
            execute_pipeline(shell, lhs_tokens, lhs_num_tokens, stdin_pipe,
                             stdout_pipe, stderr_pipe);

        if (logical_op == SHASH_SHELL_OP_LOGICAL_AND && lhs_exit_code == 0) {
            if (temp_assign) {
                for (int i = 0; i < temp_names.size; i++) {
                    SetEnvironmentVariableW(arr_get(&temp_names, i),
                                            arr_get(&temp_vals, i));
                }
            }
            arr_free(&temp_names);
            arr_free(&temp_vals);
            return process_line(shell, rhs_tokens, rhs_num_tokens, stdin_pipe,
                                stdout_pipe,
                                stderr_pipe); // Recurse for chaining
        }
        if (logical_op == SHASH_SHELL_OP_LOGICAL_OR && lhs_exit_code != 0) {
            if (temp_assign) {
                for (int i = 0; i < temp_names.size; i++) {
                    SetEnvironmentVariableW(arr_get(&temp_names, i),
                                            arr_get(&temp_vals, i));
                }
            }
            arr_free(&temp_names);
            arr_free(&temp_vals);
            return process_line(shell, rhs_tokens, rhs_num_tokens, stdin_pipe,
                                stdout_pipe,
                                stderr_pipe); // Recurse for chaining
        }
        if (temp_assign) {
            for (int i = 0; i < temp_names.size; i++) {
                SetEnvironmentVariableW(arr_get(&temp_names, i),
                                        arr_get(&temp_vals, i));
            }
        }
        arr_free(&temp_names);
        arr_free(&temp_vals);
        return lhs_exit_code;
    }
}

enum ShashControlMode {
    SHASH_CONTROL_MODE_IF,
    SHASH_CONTROL_MODE_FOR,
    SHASH_CONTROL_MODE_WHILE
};

void shell_run(Shell *shell, char *script, size_t len, ShashPipe *stdin_pipe,
               ShashPipe *stdout_pipe, ShashPipe *stderr_pipe) {
    script = format_command(shell, script, NULL);
    int num_lines;
    char **lines = split_script(script, &num_lines);
    enum ShashShellOperation op = SHASH_SHELL_OP_NONE;
    char *shell_stdin = NULL;
    int last_exit_code = 0;
    ShashIntArray control_stack = {0};
    ShashIntArray control_data_stack = {0};
    for (int i = 0; i < num_lines; i++) {
        char *line = lines[i];
        int rem = num_lines - i - 1;
        int num_tokens;
        char **tokens = split_line(line, &num_tokens);
        char *token = tokens[0];
        enum ShashControlMode control =
            (enum ShashControlMode)control_stack.size == 0
                ? -100
                : control_stack.data[control_stack.size - 1];
        int control_data =
            control_stack.size == 0
                ? -100
                : control_data_stack.data[control_data_stack.size - 1];
        bool pass = true;
        if (control == SHASH_CONTROL_MODE_IF && !control_data) {
            pass = false;
            bool drop = true;
            int level;
            while (rem >= 0) {
                if (strcmp(lines[i], "if")) {
                    level++;
                }
                if (strcmp(lines[i], "fi")) {
                    if (level == 0) {
                        drop = false;
                        break;
                    } else {
                        level--;
                    }
                }
                i++;
                rem = num_lines - i - 1;
            }
            if (drop) {
                shash_pipe_puts(stderr_pipe, "shash: expected token \"fi\".");
                return;
            }
        }
        if (control == SHASH_CONTROL_MODE_IF && num_tokens == 1 &&
            strcmp(token, "fi") == 0) {
            pass = false;
            int_arr_pop(&control_stack);
            int_arr_pop(&control_data_stack);
        }
        if (!pass)
            break;
        if (strcmp(token, "if") == 0) {
            last_exit_code = process_line(shell, tokens + 1, num_tokens - 1,
                                          stdin_pipe, stdout_pipe, stderr_pipe);
            int_arr_add(&control_stack, SHASH_CONTROL_MODE_IF);
            int_arr_add(&control_data_stack, last_exit_code == 0);
            if (rem > 0) {
                if (strcmp(lines[i + 1], "then")) {
                    i++;
                } else {
                    shash_pipe_puts(stderr_pipe,
                                    "shash: expected token \"then\".");
                    return;
                }
            } else {
                shash_pipe_puts(stderr_pipe, "shash: expected token \"then\".");
                return;
            }
        } else {
            last_exit_code = process_line(shell, tokens, num_tokens, stdin_pipe,
                                          stdout_pipe, stderr_pipe);
        }
        free(tokens);
    }
    int_arr_free(&control_stack);
    int_arr_free(&control_data_stack);
    free(lines);
}

void shell_terminate_main(Shell *shell) { shell->terminate_signal = true; }

SHASH_PRIVATE bool glob_match(const wchar_t *pattern,
                              const wchar_t *name); // Forward declaration

SHASH_PRIVATE bool match_char_class(const wchar_t *pattern, wchar_t name_char,
                                    const wchar_t **end_pattern) {
    if (name_char == L'\0')
        return false;

    bool match = false;
    bool negate = (*pattern == L'^' || *pattern == L'!');
    if (negate)
        pattern++;

    if (*pattern == L']') {
        if (*pattern == name_char)
            match = true;
        pattern++;
    }

    while (*pattern && *pattern != L']') {
        wchar_t c1 = *pattern;
        if (pattern[1] == L'-' && pattern[2] != L'\0' && pattern[2] != L']') {
            wchar_t c2 = pattern[2];
            if (name_char >= c1 && name_char <= c2)
                match = true;
            pattern += 3;
        } else {
            if (c1 == name_char)
                match = true;
            pattern++;
        }
    }

    if (*pattern != L']')
        return false; // Malformed
    *end_pattern = pattern + 1;
    return negate ? !match : match;
}

SHASH_PRIVATE bool glob_match(const wchar_t *pattern, const wchar_t *name) {
    while (*pattern) {
        if (*pattern == L'*') {
            pattern++;
            // The logic for '*' is simpler here as we process path
            // components one by one. It just matches zero or more
            // characters within a single component.
            while (*name) {
                if (glob_match(pattern, name))
                    return true;
                name++;
            }
            return glob_match(pattern, name);
        }
        if (!*name) {
            return false;
        }
        if (*pattern == L'[') {
            const wchar_t *end_class;
            if (match_char_class(pattern + 1, *name, &end_class)) {
                pattern = end_class;
                name++;
                continue;
            }
            return false;
        }
        if (*pattern != L'?' && *pattern != *name) {
            return false;
        }
        pattern++;
        name++;
    }
    return *name == L'\0';
}

SHASH_PRIVATE void glob_resolve_recursive(const wchar_t *base_path,
                                          const wchar_t *pattern,
                                          Array *results) {
    // Find the first path separator in the pattern
    const wchar_t *separator = wcschr(pattern, L'\\');
    if (!separator) {
        separator = wcschr(pattern, L'/');
    }

    wchar_t current_pattern[MAX_PATH];
    const wchar_t *next_pattern;

    if (separator) {
        size_t len = separator - pattern;
        wcsncpy_s(current_pattern, MAX_PATH, pattern, len);
        current_pattern[len] = L'\0';
        next_pattern = separator + 1;
    } else {
        wcscpy_s(current_pattern, MAX_PATH, pattern);
        next_pattern =
            L""; // Empty string, indicates this is the last component
    }

    // --- Handle `**` wildcard ---
    if (wcscmp(current_pattern, L"**") == 0) {
        // First, match `**` as zero directories. This means we try to match
        // the rest of the pattern against the current base path.
        glob_resolve_recursive(base_path, next_pattern, results);

        // Second, match `**` as one or more directories. We enumerate all
        // subdirectories and recursively call ourselves with the *original*
        // `**` pattern.
        wchar_t *search_path = join_path_wide_dup(base_path, L"*");

        WIN32_FIND_DATAW find_data;
        HANDLE hFind = FindFirstFileW(search_path, &find_data);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    wcscmp(find_data.cFileName, L".") != 0 &&
                    wcscmp(find_data.cFileName, L"..") != 0) {
                    wchar_t *new_base_path =
                        join_path_wide_dup(base_path, find_data.cFileName);
                    glob_resolve_recursive(new_base_path, pattern, results);
                }
            } while (FindNextFileW(hFind, &find_data));
            FindClose(hFind);
        }
        return;
    }

    // --- Handle regular patterns (`*`, `?`, `[...]`, literals) ---
    wchar_t *search_path = join_path_wide_dup(base_path, L"*"); // Broad search

    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(search_path, &find_data);

    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (glob_match(current_pattern, find_data.cFileName)) {
            wchar_t *new_path =
                join_path_wide_dup(base_path, find_data.cFileName);

            bool is_dir =
                (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);

            // If this is the last component in the pattern...
            if (wcslen(next_pattern) == 0) {
                char *u = wide_to_utf8_dup(new_path);
                arr_add(results, normalize_path_dup(u));
                free(u);
            }
            // If it's a directory and there's more pattern to match...
            else if (is_dir) {
                if (wcscmp(find_data.cFileName, L".") != 0 &&
                    wcscmp(find_data.cFileName, L"..") != 0) {
                    glob_resolve_recursive(new_path, next_pattern, results);
                }
            }
        }
    } while (FindNextFileW(hFind, &find_data));

    FindClose(hFind);
}

SHASH_PRIVATE Array glob_resolve(const char *pattern) {
    Array results;
    arr_init(&results, 1);

    wchar_t full_pattern[MAX_PATH];
    wchar_t *pat = utf8_to_wide_dup(pattern);
    if (GetFullPathNameW(pat, MAX_PATH, full_pattern, NULL) == 0) {
        fprintf(stderr, "shash: could not get full path for pattern.");
        return results;
    }
    free(pat);

    wchar_t root_path[MAX_PATH];
    wcscpy_s(root_path, MAX_PATH, full_pattern);
    wchar_t *first_wildcard = wcschr(root_path, L'*');
    wchar_t *first_qmark = wcschr(root_path, L'?');
    wchar_t *first_bracket = wcschr(root_path, L'[');

    wchar_t *split_point = NULL;
    if (first_wildcard)
        split_point = first_wildcard;
    if (first_qmark && (!split_point || first_qmark < split_point))
        split_point = first_qmark;
    if (first_bracket && (!split_point || first_bracket < split_point))
        split_point = first_bracket;

    const wchar_t *remaining_pattern;
    if (split_point) {
        // Back up to the last directory separator
        while (split_point > root_path && *split_point != L'\\' &&
               *split_point != L'/') {
            split_point--;
        }
        // If we stopped on a separator, we want the pattern to start after
        // it
        remaining_pattern =
            full_pattern + (split_point - root_path) +
            (*split_point == L'\\' || *split_point == L'/' ? 1 : 0);
        *split_point = L'\0';
    } else {
        // No wildcards, this is a literal path
        remaining_pattern = L"";
        // To handle this, we effectively set the remaining pattern to the
        // filename part
        char *root_path_u = wide_to_utf8_dup(root_path);
        char *r_path_u = path_remove_file_dup(root_path_u);
        free(root_path_u);
        utf8_to_wide_fill(r_path_u, root_path, MAX_PATH);
        free(r_path_u);

        char *full_pattern_u = wide_to_utf8_dup(full_pattern);
        char *fn_u = path_filename_dup(full_pattern_u);
        remaining_pattern = utf8_to_wide_dup(fn_u);
        free(full_pattern_u);
    }

    // If the root path is empty (e.g., relative path like "**/*.txt"), use
    // "."
    if (wcslen(root_path) == 0) {
        wcscpy_s(root_path, MAX_PATH, L".");
    }

    glob_resolve_recursive(root_path, remaining_pattern, &results);
    return results;
}

bool parse_int(const char *string, int *out) {
    int len = strlen(string);
    int res = 0;
    int scalar = 1;
    bool sign = false;
    if (string[0] == '-') {
        scalar = -1;
        sign = true;
    } else if (string[0] == '+') {
        sign = true;
    }
    for (int i = sign ? 1 : 0; i < len; i++) {
        if (string[i] >= '0' && string[i] <= '9') {
            res = (res * 10) + (string[i] - '0');
        } else {
            return false;
        }
    }
    *out = res * scalar;
    return true;
}

//
// -=================================================================-
// -======================= Built-in Commands =======================-
// -=================================================================-
//

static Program error_program;
static Program echo_program;
static Program ls_program;
static Program mkdir_program;
static Program rmdir_program;
static Program tee_program;
static Program cd_program;
static Program pwd_program;
static Program cp_program;
static Program mv_program;
static Program rm_program; // TODO
static Program sleep_program;
static Program source_program;
static Program shash_program;
static Program cat_program;
static Program whoami_program;
static Program grep_program;   // TODO
static Program touch_program;  // TODO
static Program find_program;   // TODO
static Program wc_program;     // TODO
static Program seq_program;    // TODO
static Program export_program; // TODO
static Program read_program;   // TODO
static Program date_program;   // TODO
static Program printf_program; // TODO

SHASH_PRIVATE int cmd_error(Shell *shell, char **argv, int argc,
                            ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                            ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&error_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "error: %s\n", error_msg);
        return 1;
    }
    char *str = parse->positional_arguments[0];
    parse_result_free(parse);
    shash_pipe_puts(stderr_pipe, str);
    return 0;
}

SHASH_PRIVATE int cmd_echo(Shell *shell, char **argv, int argc,
                           ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                           ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&echo_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "echo: %s\n", error_msg);
        return 1;
    }
    for (int i = 0; i < parse->num_pos_args; i++) {
        shash_pipe_puts(stdout_pipe, parse->positional_arguments[i]);
        if (i != parse->num_pos_args - 1) {
            shash_pipe_puts(stdout_pipe, " ");
        }
    }
    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_ls(Shell *shell, char **argv, int argc,
                         ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                         ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&ls_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "ls: %s\n", error_msg);
        return 1;
    }
    char *path = shell->working_directory;
    if (parse->num_pos_args >= 1) {
        for (int i = 0; i < parse->num_pos_args; i++) {
            path =
                shell_path_normalize_dup(shell, parse->positional_arguments[0]);
            uint32_t attrib = get_attributes(path);
            if (!file_exists(attrib)) {
                Array files = glob_resolve(path);
                for (int i = 0; i < files.size; i++) {
                    shash_pipe_puts(stdout_pipe, arr_get(&files, i));
                    shash_pipe_puts(stdout_pipe, "\n");
                    free(arr_get(&files, i));
                }
                arr_free(&files);
            } else {
                if (is_dir(attrib)) {
                    char pattern[MAX_PATH];
                    snprintf(pattern, MAX_PATH, "%s/*", path);
                    Array *files = get_files_dup(pattern);
                    for (int i = 0; i < files->size; i++) {
                        shash_pipe_puts(stdout_pipe, arr_get(files, i));
                        shash_pipe_puts(stdout_pipe, "\n");
                        free(arr_get(files, i));
                    }
                    arr_free(files);
                } else {
                    shash_pipe_puts(stdout_pipe, path);
                    shash_pipe_puts(stdout_pipe, "\n");
                }
            }
            free(path);
        }
    } else {
        char pattern[MAX_PATH];
        snprintf(pattern, MAX_PATH, "%s/*", path);
        Array *files = get_files_dup(pattern);
        for (int i = 0; i < files->size; i++) {
            shash_pipe_puts(stdout_pipe, arr_get(files, i));
            {
                char f[MAX_PATH];
                snprintf(f, MAX_PATH, "%s/%s", path, (char *)arr_get(files, i));
                if (is_dir(get_attributes(f))) {
                    shash_pipe_puts(stdout_pipe, " (dir)");
                }
            }
            shash_pipe_puts(stdout_pipe, "\n");
            free(arr_get(files, i));
        }
        arr_free(files);
    }
    return 0;
}

SHASH_PRIVATE int cmd_mkdir(Shell *shell, char **argv, int argc,
                            ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                            ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&mkdir_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "mkdir: %s\n", error_msg);
        return 1;
    }
    ParsedArgument *parents = parse_result_get_argument(parse, "--parents");
    ParsedArgument *quiet = parse_result_get_argument(parse, "--quiet");
    for (int i = 0; i < parse->num_pos_args; i++) {
        Array paths;
        arr_init(&paths, 1);
        char *path =
            shell_path_normalize_dup(shell, parse->positional_arguments[i]);
        if (parents) {
            while (!file_exists(get_attributes(path))) {
                char *new_path = dupstr(path);
                free(path);
                path = path_parent_dup(new_path);
                arr_add(&paths, new_path);
            }
        } else {
            arr_add(&paths, path);
        }
        for (int j = paths.size - 1; j >= 0; j--) {
            wchar_t *wide = utf8_to_wide_dup(arr_get(&paths, j));
            int flag = CreateDirectoryW(wide, NULL);
            HeapFree(GetProcessHeap(), 0, wide);
            if (!quiet) {
                if (!flag) {
                    switch (GetLastError()) {
                    case ERROR_ALREADY_EXISTS:
                        shash_pipe_puts(stdout_pipe,
                                        "directory already exists.\n");
                        break;
                    case ERROR_PATH_NOT_FOUND:
                        shash_pipe_puts(stdout_pipe, "path not found.\n");
                        break;
                    }
                } else {
                    shash_pipe_puts(stdout_pipe,
                                    "Successfuly created directory.\n");
                }
            }
        }
        free(path);
        arr_free(&paths);
    }
    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_rmdir(Shell *shell, char **argv, int argc,
                            ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                            ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&rmdir_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "rmdir: %s\n", error_msg);
        return 1;
    }
    StringBuilder sb;
    sb_init(&sb, 1);
    ParsedArgument *quiet = parse_result_get_argument(parse, "--quiet");
    for (int i = 0; i < parse->num_pos_args; i++) {
        char *path =
            shell_path_normalize_dup(shell, parse->positional_arguments[i]);
        wchar_t *wide = utf8_to_wide_dup(path);
        free(path);
        int flag = RemoveDirectoryW(wide);
        HeapFree(GetProcessHeap(), 0, wide);
        if (!quiet) {
            if (!flag) {
                switch (GetLastError()) {
                case ERROR_ALREADY_EXISTS:
                    shash_pipe_puts(stderr_pipe,
                                    "Error: Directory already exists.\n");
                    break;
                case ERROR_PATH_NOT_FOUND:
                    shash_pipe_puts(stderr_pipe, "Error: Path not found.\n");
                    break;
                }
            } else {
                shash_pipe_puts(stdout_pipe,
                                "Successfuly created directory.\n");
            }
        }
    }
    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_tee(Shell *shell, char **argv, int argc,
                          ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                          ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&tee_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "tee: %s\n", error_msg);
        return 1;
    }
    Array files;
    arr_init(&files, parse->num_pos_args);
    for (int i = 0; i < parse->num_pos_args; i++) {

        char *p =
            shell_path_normalize_dup(shell, parse->positional_arguments[i]);
        uint32_t attrib = get_attributes(p);
        if (!is_dir(attrib)) {
            int error = 0;
            void *file = open_file(p, true, true, &error);
            if (!file) {
                char *err_s = (char *)win_error_to_str(error);
                shash_pipe_printf(stderr_pipe, "tee: %s: %s\n", p, err_s);
                LocalFree(err_s);
            } else {
                arr_add(&files, file);
            }
        } else {
            shash_pipe_printf(stderr_pipe, "tee: %s: Is a directory\n", p);
        }
        free(p);
    }
    char chunk[4096];
    size_t bytes_read;
    while (shash_pipe_read(stdin_pipe, chunk, 4096, &bytes_read) > 0) {
        shash_pipe_write(stdout_pipe, chunk, bytes_read);
        for (int i = 0; i < files.size; i++) {
            write_file(arr_get(&files, i), chunk, bytes_read);
            close_file(arr_get(&files, i));
        }
    }
    arr_free(&files);
    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_cd(Shell *shell, char **argv, int argc,
                         ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                         ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&cd_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "cd: %s\n", error_msg);
        return 1;
    }

    if (parse->num_pos_args == 1) {
        shell->working_directory = join_path_dup(
            shell->working_directory, parse->positional_arguments[0]);
    } else {
        shash_pipe_puts(stdout_pipe, shell->working_directory);
    }
    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_pwd(Shell *shell, char **argv, int argc,
                          ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                          ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&pwd_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "pwd: %s\n", error_msg);
        return 1;
    }
    shash_pipe_puts(stdout_pipe, shell->working_directory);
    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_sleep(Shell *shell, char **argv, int argc,
                            ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                            ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&sleep_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stdout_pipe, "sleep: %s\n", error_msg);
        return 1;
    }
    _utf8_str u = utf8_str(parse->positional_arguments[0]);
    bool is_decimal = false;
    uint64_t int_part = 0;
    double decimal_part = 0;
    double divide = 1;
    for (int i = 0; i < u.len; i++) {
        if (utf8_is_ascii(u.str[i])) {
            char c = *u.str[i];
            if ((c < '0' || c > '9') && c != '.') {
                shash_pipe_puts(stderr_pipe, "sleep: invalid number.");
                return 1;
            }
            if (c == '.') {
                if (is_decimal) {
                    shash_pipe_puts(stderr_pipe, "sleep: invalid number.");
                    return 1;
                }
                is_decimal = true;
            } else {
                if (is_decimal) {
                    decimal_part = decimal_part * 10 + (c - '0');
                    divide *= 10;
                } else {
                    int_part = int_part * 10 + (c - '0');
                }
            }
        }
    }
    uint64_t ms = (int_part + decimal_part / divide) * 1000;
    Sleep(ms);
    parse_result_free(parse);
    return 0;
}

void cp_recurse(ShashPipe *stdout_pipe, char *folder, char *dest, bool v) {
    size_t len = strlen(folder);
    char *pattern = malloc(len + 3);
    memcpy(pattern, folder, len);
    pattern[len] = '/';
    pattern[len + 1] = '*';
    pattern[len + 2] = 0;
    Array *files = get_files_dup(pattern);
    free(pattern);
    for (int i = 0; i < files->size; i++) {
        char *name = arr_get(files, i);
        char *path = join_path_dup(folder, name);
        char *r_dest = join_path_dup(dest, name);
        if (is_dir(get_attributes(path))) {
            cp_recurse(stdout_pipe, path, r_dest, v);

            char *name = path_filename_dup(path);
            char *ndest = join_path_dup(r_dest, name);
            free(name);
            wchar_t *wndest = utf8_to_wide_dup(ndest);
            CreateDirectoryW(wndest, NULL);
            HeapFree(GetProcessHeap(), 0, wndest);
            if (v) {
                shash_pipe_printf(stdout_pipe, "'%s' -> '%s'\n", path, r_dest);
            }
            cp_recurse(stdout_pipe, path, ndest, v);
            free(ndest);
        } else {
            wchar_t *wpath = utf8_to_wide_dup(path);
            wchar_t *wfdest = utf8_to_wide_dup(r_dest);
            if (!CopyFileW(wpath, wfdest, false)) {
                shash_pipe_puts(stdout_pipe, "cp: failed to copy file.\n");
            } else if (v) {
                shash_pipe_printf(stdout_pipe, "'%s' -> '%s'\n", path, r_dest);
            }
            HeapFree(GetProcessHeap(), 0, wpath);
            HeapFree(GetProcessHeap(), 0, wfdest);
        }
        free(name);
        free(path);
    }
    arr_free(files);
}

SHASH_PRIVATE int cmd_cp(Shell *shell, char **argv, int argc,
                         ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                         ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&cp_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "cp: %s\n", error_msg);
        return 1;
    }

    int args = parse->num_pos_args;

    char *dest =
        shell_path_normalize_dup(shell, parse->positional_arguments[args - 1]);

    int dest_attrib = get_attributes(dest);
    bool dest_dir = is_dir(dest_attrib);
    if ((!file_exists(dest_attrib) || !dest_dir) && args > 2) {
        shash_pipe_puts(stderr_pipe, "cp: destination does not exist.\n");
        return 1;
    }

    ParsedArgument *r = parse_result_get_argument(parse, "--recursive");
    ParsedArgument *u = parse_result_get_argument(parse, "--update");
    ParsedArgument *v = parse_result_get_argument(parse, "--verbose");

    for (int i = 0; i < args - 1; i++) {
        char *n = normalize_path_dup(parse->positional_arguments[i]);
        Array paths = glob_resolve(n);
        free(n);
        for (int j = 0; j < paths.size; j++) {
            char *display_path = (char *)arr_get(&paths, j);
            char *path = shell_path_normalize_dup(shell, display_path);

            int fattrib = get_attributes(path);
            if (!file_exists(fattrib)) {
                shash_pipe_puts(stderr_pipe, "cp: file does not exist.\n");
                free(path);
                continue;
            }
            char *file_dest = dest;
            if (dest_dir) {
                char *fn = path_filename_dup(path);
                file_dest = join_path_dup(dest, fn);
                free(fn);
            }
            if (file_exists(get_attributes(file_dest))) {
                if (u) {
                    void *file_a = open_file(path, false, false, 0);
                    void *file_b = open_file(file_dest, false, false,
                                             0); // FIXME add error handling
                    FILETIME fa_write;
                    FILETIME fb_write;
                    if (!GetFileTime(file_a, NULL, NULL, &fa_write) ||
                        !GetFileTime(file_b, NULL, NULL, &fb_write)) {
                        close_file(file_a);
                        close_file(file_b);
                        shash_pipe_puts(
                            stderr_pipe,
                            "cp: update: failed to get file write time.\n");
                        free(path);
                        continue;
                    }
                    close_file(file_a);
                    close_file(file_b);
                    if (CompareFileTime(&fa_write, &fb_write) <= 0) {
                        shash_pipe_puts(stderr_pipe,
                                        "cp: destination already exists.\n");
                        free(path);
                        continue;
                    }
                } else {
                    shash_pipe_puts(stderr_pipe,
                                    "cp: destination already exists.\n");
                    free(path);
                    continue;
                }
            }
            if (is_dir(fattrib)) {
                if (!is_dir(dest_attrib)) {
                    shash_pipe_puts(stderr_pipe,
                                    "cp: destination is not a directory.\n");
                    free(path);
                    continue;
                }
                if (!r) {
                    shash_pipe_puts(stderr_pipe, "cp: file is a directory.\n");
                    free(path);
                    continue;
                }
                char *name = path_filename_dup(path);
                char *ndest = join_path_dup(dest, name);
                free(name);
                wchar_t *wndest = utf8_to_wide_dup(ndest);
                CreateDirectoryW(wndest, NULL);
                HeapFree(GetProcessHeap(), 0, wndest);
                if (v) {
                    shash_pipe_printf(stdout_pipe, "'%s' -> '%s'\n",
                                      display_path, file_dest);
                }
                cp_recurse(stdout_pipe, path, ndest, v != NULL);
                free(ndest);
            } else {
                wchar_t *wpath = utf8_to_wide_dup(path);
                wchar_t *wfdest = utf8_to_wide_dup(file_dest);
                if (!CopyFileW(wpath, wfdest, false)) {
                    shash_pipe_puts(stderr_pipe, "cp: failed to copy file.\n");
                } else if (v) {
                    shash_pipe_printf(stdout_pipe, "'%s' -> '%s'\n",
                                      display_path, file_dest);
                }
                HeapFree(GetProcessHeap(), 0, wpath);
                HeapFree(GetProcessHeap(), 0, wfdest);
            }
            if (dest_dir) {
                free(file_dest);
            }
            free(path);
        }
    }

    free(dest);
    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_mv(Shell *shell, char **argv, int argc,
                         ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                         ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&mv_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "mv: %s\n", error_msg);
        return 1;
    }

    int args = parse->num_pos_args;

    char *dest =
        shell_path_normalize_dup(shell, parse->positional_arguments[args - 1]);

    int dest_attrib = get_attributes(dest);
    bool dest_dir = is_dir(dest_attrib);
    if ((!file_exists(dest_attrib) || !dest_dir) && args > 2) {
        shash_pipe_puts(stderr_pipe, "mv: destination does not exist.\n");
        return 1;
    }

    ParsedArgument *u = parse_result_get_argument(parse, "--update");
    ParsedArgument *f = parse_result_get_argument(parse, "--force");
    ParsedArgument *v = parse_result_get_argument(parse, "--verbose");

    for (int i = 0; i < args - 1; i++) {
        char *n = normalize_path_dup(parse->positional_arguments[i]);
        Array paths = glob_resolve(n);
        free(n);
        for (int j = 0; j < paths.size; j++) {
            char *display_path = (char *)arr_get(&paths, j);
            char *path = shell_path_normalize_dup(shell, display_path);

            int fattrib = get_attributes(path);
            if (!file_exists(fattrib)) {
                shash_pipe_puts(stderr_pipe, "mv: file does not exist.\n");
                free(path);
                continue;
            }
            char *file_dest = dest;
            if (dest_dir) {
                char *fn = path_filename_dup(path);
                file_dest = join_path_dup(dest, fn);
                free(fn);
            }
            if (file_exists(get_attributes(file_dest))) {
                if (u) {
                    void *file_a = open_file(path, false, false, 0);
                    void *file_b = open_file(file_dest, false, false,
                                             0); // FIXME add error handling
                    FILETIME fa_write;
                    FILETIME fb_write;
                    if (!GetFileTime(file_a, NULL, NULL, &fa_write) ||
                        !GetFileTime(file_b, NULL, NULL, &fb_write)) {
                        close_file(file_a);
                        close_file(file_b);
                        shash_pipe_puts(
                            stderr_pipe,
                            "mv: update: failed to get file write time.\n");
                        free(path);
                        continue;
                    }
                    close_file(file_a);
                    close_file(file_b);
                    if (CompareFileTime(&fa_write, &fb_write) <= 0) {
                        shash_pipe_puts(stderr_pipe,
                                        "mv: destination already exists.\n");
                        free(path);
                        continue;
                    }
                } else {
                    shash_pipe_puts(stderr_pipe,
                                    "mv: destination already exists.\n");
                    free(path);
                    continue;
                }
            }
            wchar_t *wpath = utf8_to_wide_dup(path);
            wchar_t *wfdest = utf8_to_wide_dup(file_dest);
            if (!MoveFileW(wpath, wfdest)) {
                shash_pipe_puts(stderr_pipe, "mv: failed to move file.\n");
            } else if (v) {
                shash_pipe_printf(stdout_pipe, "'%s' -> '%s'\n", display_path,
                                  file_dest);
            }
            HeapFree(GetProcessHeap(), 0, wpath);
            HeapFree(GetProcessHeap(), 0, wfdest);
            if (dest_dir) {
                free(file_dest);
            }
            free(path);
        }
    }

    free(dest);
    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_source(Shell *shell, char **argv, int argc,
                             ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                             ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&source_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "source: %s\n", error_msg);
        return 1;
    }
    int open_error;
    void *file =
        open_file(parse->positional_arguments[0], false, false, &open_error);
    if (open_error) {
        char *err = win_error_to_str(open_error);
        shash_pipe_printf(stderr_pipe, "source: %s\n", err);
        return 1;
    }
    void *script;
    unsigned long long out_size;
    bool fail = read_file(file, &script, &out_size);
    if (fail) {
        shash_pipe_puts(stderr_pipe, "source: failed to read file.");
    }
    shell_run(shell, (char *)script, out_size, stdin_pipe, stdout_pipe,
              stderr_pipe);
    close_file(file);
    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_sh(Shell *shell, char **argv, int argc,
                         ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                         ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&shash_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "shash: %s\n", error_msg);
        return 1;
    }
    Shell *sub = make_sub_shell(shell);
    int open_error;
    void *file =
        open_file(parse->positional_arguments[0], false, false, &open_error);
    if (open_error) {
        char *err = win_error_to_str(open_error);
        shash_pipe_printf(stderr_pipe, "shash: %s\n", err);
        return 1;
    }
    void *script;
    unsigned long long out_size;
    bool fail = read_file(file, &script, &out_size);
    if (fail) {
        shash_pipe_puts(stderr_pipe, "shash: failed to read file.\n");
    }
    shell_run(sub, (char *)script, out_size, stdin_pipe, stdout_pipe,
              stderr_pipe);
    close_file(file);
    shell_free(sub);
    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_test(Shell *shell, char **argv, int argc,
                           ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                           ShashPipe *stderr_pipe) {
    int size = argc - 1;
    if (strcmp(argv[0], "[") == 0) {
        size -= 1;
        if (strcmp(argv[argc - 1], "]") != 0) {
            shash_pipe_puts(stderr_pipe, "test: invalid syntax\n");
        }
    }
    char **args = argv + 1;
    bool negate = false;
    if (strcmp(argv[1], "!") == 0) {
        negate = true;
        args++;
        size--;
    }
    int EXIT_FALSE = negate;
    int EXIT_TRUE = !negate;
    if (strcmp(args[0], "-f") && size == 2) { // Is file
        uint32_t attrib = get_attributes(args[1]);
        return is_dir(attrib) ? EXIT_FALSE : EXIT_TRUE;
    } else if (strcmp(args[0], "-d") && size == 2) { // Is directory
        uint32_t attrib = get_attributes(args[1]);
        return is_dir(attrib) ? EXIT_TRUE : EXIT_FALSE;
    } else if (strcmp(args[0], "-p") && size == 2) { // Is pipe
        uint32_t attrib = get_attributes(args[1]);
        if (attrib == INVALID_FILE_ATTRIBUTES) {
            return EXIT_FALSE;
        }
        void *file = open_file(args[0], false, false, NULL);
        uint32_t type = GetFileType(file);
        close_file(file);
        return type == FILE_TYPE_PIPE ? EXIT_TRUE : EXIT_FALSE;
    } else if ((strcmp(args[0], "-L") || strcmp(args[0], "-h")) &&
               size == 2) { // Is symbolic link
        uint32_t attrib = get_attributes(args[1]);
        return (attrib & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ? EXIT_TRUE
                                                            : EXIT_FALSE;
    } else if ((strcmp(args[0], "-r") || strcmp(args[0], "-w") ||
                strcmp(args[0], "-x") || strcmp(args[0], "-u") ||
                strcmp(args[0], "-g") || strcmp(args[0], "-k")) &&
               size == 2) { // TODO Is readable/writable/executable/...
        return EXIT_TRUE;
    } else if (strcmp(args[0], "-z") && size <= 2) {
        return size == 1 ? EXIT_TRUE : EXIT_FALSE;
    } else if (strcmp(args[0], "-n") && size <= 2) {
        return size == 1 ? EXIT_FALSE : EXIT_TRUE;
    } else if (size == 3) {
        if (strcmp(args[1], "-eq") == 0 || strcmp(args[1], "-ne") == 0 ||
            strcmp(args[1], "-gt") == 0 || strcmp(args[1], "-ge") == 0 ||
            strcmp(args[1], "-lt") == 0 || strcmp(args[1], "-le") == 0) {
            int a, b;
            if (!parse_int(args[0], &a) || !parse_int(args[2], &b)) {
                return 1;
            }
            if (strcmp(args[1], "-eq") == 0) {
                return a == b ? EXIT_TRUE : EXIT_FALSE;
            } else if (strcmp(args[1], "-ne") == 0) {
                return a != b ? EXIT_TRUE : EXIT_FALSE;
            } else if (strcmp(args[1], "-gt") == 0) {
                return a > b ? EXIT_TRUE : EXIT_FALSE;
            } else if (strcmp(args[1], "-ge") == 0) {
                return a >= b ? EXIT_TRUE : EXIT_FALSE;
            } else if (strcmp(args[1], "-lt") == 0) {
                return a < b ? EXIT_TRUE : EXIT_FALSE;
            } else if (strcmp(args[1], "-le") == 0) {
                return a <= b ? EXIT_TRUE : EXIT_FALSE;
            }
        }
        int comp = strcmp(args[0], args[1]);
        if (strcmp(args[1], "=") == 0 || strcmp(args[1], "==") == 0) {
            return comp == 0 ? EXIT_TRUE : EXIT_FALSE;
        } else if (strcmp(args[1], "!=") == 0) {
            return comp != 0 ? EXIT_TRUE : EXIT_FALSE;
        }
    } else if (size == 1) {
        return EXIT_TRUE;
    }

    return 1;
}

SHASH_PRIVATE int cmd_cat(Shell *shell, char **argv, int argc,
                          ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                          ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&cat_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "cat: %s\n", error_msg);
        return 1;
    }

    bool show_tabs = parse_result_get_argument(parse, "--show-tabs") ||
                     parse_result_get_argument(parse, "-t") ||
                     parse_result_get_argument(parse, "-A");

    bool show_ends = parse_result_get_argument(parse, "--show-ends") ||
                     parse_result_get_argument(parse, "-e") ||
                     parse_result_get_argument(parse, "-A");

    bool non_printing =
        parse_result_get_argument(parse, "--show-nonprinting") ||
        parse_result_get_argument(parse, "-t") ||
        parse_result_get_argument(parse, "-e") ||
        parse_result_get_argument(parse, "-A");

    bool squeeze = parse_result_get_argument(parse, "-s");
    bool number = parse_result_get_argument(parse, "-n");
    bool nonblanks = parse_result_get_argument(parse, "-b");

    int line = 1;

    if (number || nonblanks) {
        shash_pipe_printf(stdout_pipe, "%6d  ", line);
    }

    bool is_blank = true;

    for (int i = 0; i < parse->num_pos_args; i++) {
        char *pattern =
            shell_path_normalize_dup(shell, parse->positional_arguments[i]);
        Array paths = glob_resolve(pattern);
        for (int j = 0; j < paths.size; j++) {
            char *path = arr_get(&paths, j);
            int error;
            void *file = open_file(path, false, false, &error);
            if (error) {
                continue;
            }
            char *buf;
            size_t size;
            read_file(file, (void **)&buf, &size);
            for (int k = 0; k < size; k++) {
                if (show_tabs && buf[k] == '\t') {
                    shash_pipe_puts(stdout_pipe, "^I");
                    is_blank = false;
                } else if (buf[k] == '\n' && (!squeeze || !is_blank)) {
                    if (!nonblanks || !is_blank) {
                        line++;
                    }
                    if (show_ends) {
#ifdef _WIN32
                        shash_pipe_puts(stdout_pipe, "$\r\n");
#else
                        shash_pipe_puts(stdout_pipe, "$\n");
#endif
                    }
                    is_blank = true;
                    if (number || nonblanks) {
                        shash_pipe_printf(stdout_pipe, "%6d  ", line);
                    }
                } else if (non_printing) {
                    unsigned char code = buf[k];
                    if (code >= 0x00 && code <= 0x1F) {
                        shash_pipe_printf(stdout_pipe, "^%c",
                                          (char)(code + '@'));
                    } else if (code == 0x7F) {
                        shash_pipe_puts(stdout_pipe, "^?");
                    } else if (code >= 0x80 && code <= 0x9F) {
                        shash_pipe_printf(stdout_pipe, "M-^%c",
                                          (char)(code - 0x80 + '@'));
                    } else if (code >= 0xA0 && code <= 0xFF) {
                        shash_pipe_printf(stdout_pipe, "M-^%c",
                                          (char)(code - 0x80));
                    }
                    is_blank = false;
                } else if (buf[k] != '\r') {
                    shash_pipe_putc(stdout_pipe, buf[k]);
                    is_blank = false;
                }
            }
            close_file(file);
        }
        arr_free(&paths);
    }

    parse_result_free(parse);
    return 0;
}

SHASH_PRIVATE int cmd_printf(Shell *shell, char **argv, int argc,
                             ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                             ShashPipe *stderr_pipe) {
    enum ParseError error;
    char *error_msg;
    ParseResult *parse =
        program_parse(&cat_program, argv + 1, argc - 1, &error, &error_msg);
    if (!parse) {
        shash_pipe_printf(stderr_pipe, "cat: %s\n", error_msg);
        return 1;
    }
    wchar_t username[1024];
    DWORD size = sizeof(username) / sizeof(wchar_t);
    if (GetUserNameW(username, &size)) {
        shash_pipe_printf(stdout_pipe, "%ls", username);
    } else {
        return 1;
    }
    return 0;
}

SHASH_PRIVATE int cmd_whoami(Shell *shell, char **argv, int argc,
                             ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                             ShashPipe *stderr_pipe) {

    return 0;
}

//
// -=================================================================-
// -======================= Built-in Commands =======================-
// -=================================================================-
//

void make_user_pipes(ShashPipe *stdin_pipe, ShashPipe *stdout_pipe,
                     ShashPipe *stderr_pipe) {
    if (stdin_pipe) {
        shash_pipe_open(stdin_pipe);
    }
    if (stdout_pipe) {
        shash_pipe_open(stdout_pipe);
    }
    if (stderr_pipe) {
        shash_pipe_open(stderr_pipe);
    }
}

Shell *make_shell(bool is_interactive) {
    Shell *shell = (Shell *)xmalloc(sizeof(Shell));

    shell->working_directory = get_cwd();
    shell->environment_variables = create_string_map(DEFAULT_CAPACITY);
    shell->global = create_string_map(DEFAULT_CAPACITY);
    shell->commands = create_string_map(DEFAULT_CAPACITY);
    shell->process_table = create_int_map(DEFAULT_CAPACITY);

    // Use cast to bypass const init
    *(bool *)&shell->is_interactive = is_interactive;

    {
        error_program = program_create("error");
        error_program.min_args = 0;
        error_program.max_args = 0;
        error_program.min_positional_args = 1;
        error_program.max_positional_args = 1;
    }

    {
        echo_program = program_create("echo");
        echo_program.min_args = 0;
        echo_program.max_args = 0;
        echo_program.min_positional_args = 0;
        echo_program.max_positional_args = 0xFFFFFFFF;
    }

    {
        ls_program = program_create("ls");
        ls_program.min_args = 0;
        ls_program.max_args = 0;
        ls_program.min_positional_args = 0;
        ls_program.max_positional_args = 0xFFFFFFFF;
    }

    {
        mkdir_program = program_create("mkdir");
        mkdir_program.min_args = 0;
        mkdir_program.max_args = 0xFFFFFFFF;
        mkdir_program.min_positional_args = 1;
        mkdir_program.max_positional_args = 0xFFFFFFFF;
        Argument *parents =
            program_add_argument(&mkdir_program, "--parents", "-p");
        parents->implicit_value = true;
        parents->required = false;
        parents->default_value = NULL;
        Argument *quiet = program_add_argument(&mkdir_program, "--quiet", "-q");
        quiet->implicit_value = true;
        quiet->required = false;
    }

    {
        rmdir_program = program_create("rmdir");
        rmdir_program.min_args = 0;
        rmdir_program.max_args = 0xFFFFFFFF;
        rmdir_program.min_positional_args = 1;
        rmdir_program.max_positional_args = 0xFFFFFFFF;
        Argument *quiet = program_add_argument(&rmdir_program, "--quiet", "-q");
        quiet->implicit_value = true;
        quiet->required = false;
    }

    {
        cd_program = program_create("cd");
        cd_program.min_args = 0;
        cd_program.max_args = 0;
        cd_program.min_positional_args = 0;
        cd_program.max_positional_args = 1;
    }

    {
        pwd_program = program_create("pwd");
        pwd_program.min_args = 0;
        pwd_program.max_args = 0;
        pwd_program.min_positional_args = 0;
        pwd_program.max_positional_args = 0;
    }

    {
        cp_program = program_create("cp");
        cp_program.min_args = 0;
        cp_program.max_args = 0xFFFFFFFF;
        cp_program.min_positional_args = 2;
        cp_program.max_positional_args = 0xFFFFFFFF;
        Argument *recursive =
            program_add_argument(&cp_program, "--recrusive", "-r");
        recursive->implicit_value = true;
        recursive->required = false;
        Argument *verbose =
            program_add_argument(&cp_program, "--verbose", "-v");
        verbose->implicit_value = true;
        verbose->required = false;
        Argument *update = program_add_argument(&cp_program, "--update", "-u");
        update->implicit_value = true;
        update->required = false;
        Argument *p = program_add_argument(&cp_program, "-p", NULL);
        p->implicit_value = true;
        p->required = false;
    }
    {
        mv_program = program_create("mv");
        mv_program.min_args = 0;
        mv_program.max_args = 0xFFFFFFFF;
        mv_program.min_positional_args = 2;
        mv_program.max_positional_args = 0xFFFFFFFF;
        Argument *verbose =
            program_add_argument(&mv_program, "--verbose", "-v");
        verbose->implicit_value = true;
        verbose->required = false;
        Argument *update = program_add_argument(&mv_program, "--update", "-u");
        update->implicit_value = true;
        update->required = false;
        Argument *force = program_add_argument(&mv_program, "--force", "-f");
        force->implicit_value = true;
        force->required = false;
    }
    {
        shash_program = program_create("shash");
        shash_program.min_args = 0;
        shash_program.max_args = 0;
        shash_program.min_positional_args = 1;
        shash_program.max_positional_args = 1;
    }
    {
        source_program = program_create("source");
        source_program.min_args = 0;
        source_program.max_args = 0;
        source_program.min_positional_args = 1;
        source_program.max_positional_args = 1;
    }
    {
        cat_program = program_create("cat");
        cat_program.min_args = 0;
        cat_program.max_args = 0xFFFFFFFF;
        cat_program.min_positional_args = 0;
        cat_program.max_positional_args = 0xFFFFFFFF;

        program_add_argument(&cat_program, "-u", NULL);

        Argument *non_printing =
            program_add_argument(&cat_program, "-v", "--show-nonprinting");
        non_printing->implicit_value = true;
        non_printing->required = false;

        Argument *tabs =
            program_add_argument(&cat_program, "-T", "--show-tabs");
        tabs->implicit_value = true;
        tabs->required = false;

        Argument *vT = program_add_argument(&cat_program, "-t", NULL);
        vT->implicit_value = true;
        vT->required = false;

        Argument *squeeze =
            program_add_argument(&cat_program, "-s", "--squeeze-blank");
        squeeze->implicit_value = true;
        squeeze->required = false;

        Argument *number = program_add_argument(&cat_program, "-n", "--number");
        number->implicit_value = true;
        number->required = false;

        Argument *ends =
            program_add_argument(&cat_program, "-E", "--show-ends");
        ends->implicit_value = true;
        ends->required = false;

        Argument *vE = program_add_argument(&cat_program, "-e", NULL);
        vE->implicit_value = true;
        vE->required = false;

        Argument *nonblank =
            program_add_argument(&cat_program, "-b", "--number-nonblank");
        nonblank->implicit_value = true;
        nonblank->required = false;

        Argument *vET = program_add_argument(&cat_program, "-A", "--show-all");
        vET->implicit_value = true;
        vET->required = false;
    }
    shell_register_command(shell, "echo", cmd_echo);
    shell_register_command(shell, "error", cmd_error);
    shell_register_command(shell, "ls", cmd_ls);
    shell_register_command(shell, "mkdir", cmd_mkdir);
    shell_register_command(shell, "rmdir", cmd_rmdir);
    shell_register_command(shell, "cd", cmd_cd);
    shell_register_command(shell, "pwd", cmd_pwd);
    shell_register_command(shell, "sleep", cmd_sleep);
    shell_register_command(shell, "cp", cmd_cp);
    shell_register_command(shell, "mv", cmd_mv);
    shell_register_command(shell, "source", cmd_source);
    shell_register_command(shell, ".", cmd_source);
    shell_register_command(shell, "shash", cmd_sh);
    shell_register_command(shell, "sh", cmd_sh);
    shell_register_command(shell, "test", cmd_test);
    shell_register_command(shell, "[", cmd_test);
    shell_register_command(shell, "cat", cmd_cat);

    char *pathext;
    char *path;
#ifdef _WIN32
    size_t alen;
    size_t blen;
    _dupenv_s(&pathext, &alen, "PATHEXT");
    _dupenv_s(&path, &blen, "PATH");
#else
    pathext = getenv("PATHEXT");
    path = getenv("PATH");
#endif

    shell->pathext = separate_paths_dup(pathext);
    arr_add(shell->pathext, ".shs");

    shell->path = separate_paths_dup(path);

    return shell;
}

Shell *make_sub_shell(Shell *origin) {
    Shell *shell = (Shell *)xmalloc(sizeof(Shell));

    shell->working_directory = dupstr(origin->working_directory);
    shell->environment_variables =
        string_map_copy(origin->environment_variables);
    shell->global = create_string_map(DEFAULT_CAPACITY);
    shell->commands = string_map_copy(origin->commands);
    shell->process_table = int_map_copy(origin->process_table);

    shell->pathext = (Array *)xmalloc(sizeof(Array));
    arr_copy(origin->pathext, shell->pathext);

    shell->path = (Array *)xmalloc(sizeof(Array));
    arr_copy(origin->path, shell->path);

    return shell;
}

void shell_free(Shell *shell) {
    string_map_free((StringMap *)shell->environment_variables);
    string_map_free((StringMap *)shell->global);
    string_map_free((StringMap *)shell->commands);
    int_map_free((IntMap *)shell->process_table);
    Array *path = (Array *)shell->path;
    Array *pathext = (Array *)shell->pathext;
    for (int i = 0; i < path->size; i++) {
        free(path->data[i]);
    }
    for (int i = 0; i < pathext->size; i++) {
        free(pathext->data[i]);
    }
    arr_free(path);
    arr_free(pathext);
    free(shell);
}

Shell *make_shell_no_interactive() { return make_shell(false); }

void shell_register_command(Shell *shell, char *name, Command command) {
    StringMap *commands = ((StringMap *)shell->commands);
    string_map_put(commands, name, (void *)command);
}

void shell_unregister_command(Shell *shell, char *name) {
    printf("shell_unregister_command: NOT IMPLEMENTED.\n");
}

#endif // SHASH_IMPLEMENTATION

#endif // SHASH_H
