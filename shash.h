#ifndef SHASH_H
#define SHASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdlib.h>

// Type definitions

typedef struct {
    char *working_directory;
    char **environment_variables;
    char **global_scope;
    const bool is_interactive;
} Shell;

typedef struct {
    unsigned long pid;
    char *stdout_c;
    char *stderr_c;
    unsigned long exit_code;
} Process;

typedef struct {
    char **str;
    char *buffer;
    size_t len;
} utf8;

typedef char *(*Command)(char **argv, int argc, char *user_stdin);

// API declarations

Shell *make_shell(bool is_interactive);
Shell *make_shell_no_interactive();

void shell_poll(Shell *shell);

Process *shell_get_process(Shell *shell, int pid);
int shell_kill_proecss(Shell *shell, int pid);
bool shell_process_is_alive(Shell *shell, int pid);

void shell_dispose(Shell *shell);

#ifdef __cplusplus
}
#endif

// Implementation
#ifdef SHASH_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
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

char *get_cwd() {

    char *cwd = (char *)malloc(PATH_MAX);

    if (GetCurrentDirectoryA(PATH_MAX, cwd)) {
        return cwd;
    } else {
        char *home = getenv("USERPROFILE");
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

int utf8_len(unsigned char c) {
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

inline const char *utf8_next(const char *s) { return s + utf8_len(*s); }

const char *utf8_char_at(const char *s, size_t index) {
    size_t i = 0;
    while (*s) {
        if (i == index)
            return s;
        s = utf8_next(s);
        i++;
    }
    return NULL; // index out of range
}

inline const char *utf8_str_char_at(utf8 *u, size_t index) {
    return u->str[index];
}

int utf8_eq_ascii(const char *utf8_char, unsigned char ascii_char) {
    if (!utf8_char) // sanity
        return 0;
    if (ascii_char >= 0x80) // not ASCII
        return 0;
    if ((*utf8_char & 0x80) != 0) // not ASCII
        return 0;
    return (unsigned char)utf8_char[0] == ascii_char;
}

int utf8_eq_utf8(const char *u0, const char *u1) {
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

size_t utf8_strlen(const char *s) {
    size_t len = 0;
    while (*s) {
        s = utf8_next(s);
        len++;
    }
    return len;
}

utf8 utf8_str(const char *s) {
    if (!s) {
        return (utf8){.str = NULL, .buffer = NULL, .len = 0};
    }
    size_t len = utf8_strlen(s);
    char **str = (char **)malloc(sizeof(char *) * len);
    if (!str) {
        return (utf8){.str = NULL, .buffer = NULL, .len = 0};
    }
    size_t total = 0;
    for (size_t i = 0; i < len; i++) {
        size_t l = utf8_len(s[total]);
        total += l;
    }

    char *buffer = (char *)malloc(sizeof(char) * (total + len));
    if (!buffer) {
        free(str);
        return (utf8){.str = NULL, .buffer = NULL, .len = 0};
    }
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
    utf8 u;
    u.str = str;
    u.buffer = buffer;
    u.len = len;
    return u;
}

void utf8_free(utf8 *u) {
    free(u->str);
    free(u->buffer);
}

//
// -=================================================================-
// -============================ Unicode ============================-
// -=================================================================-
//

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

void sb_init(StringBuilder *sb, size_t initial_capacity) {
    sb->data = (char *)malloc(initial_capacity);
    sb->len = 0;
    sb->capacity = initial_capacity;
    sb->data[0] = '\0';
}

void sb_append(StringBuilder *sb, const char *str) {
    size_t str_len = strlen(str);
    if (sb->len + str_len + 1 > sb->capacity) {
        while (sb->len + str_len + 1 > sb->capacity) {
            sb->capacity *= 2;
        }
        sb->data = (char *)realloc(sb->data, sb->capacity);
    }
    memcpy(sb->data + sb->len, str, str_len + 1);
    sb->len += str_len;
}

void sb_append_utf8(StringBuilder *sb, const char *utf8) {
    size_t char_len = utf8_len((unsigned char)*utf8);
    if (sb->len + char_len + 1 > sb->capacity) {
        while (sb->len + char_len + 1 > sb->capacity) {
            sb->capacity *= 2;
        }
        sb->data = (char *)realloc(sb->data, sb->capacity);
    }
    memcpy(sb->data + sb->len, utf8, char_len);
    sb->data[sb->len + char_len] = '\0';
    sb->len += char_len;
}

void sb_append_c(StringBuilder *sb, char c) {
    if (sb->len + 2 > sb->capacity) { // +1 for char, +1 for null terminator
        sb->capacity *= 2;
        sb->data = (char *)realloc(sb->data, sb->capacity);
    }
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void sb_free(StringBuilder *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->capacity = 0;
}

//
// -=================================================================-
// -========================= StringBuilder =========================-
// -=================================================================-
//

//
// -=================================================================-
// -======================= Built-in Commands =======================-
// -=================================================================-
//

char *cmd_error(char **argv, int argc, char *user_stdin) {}

//
// -=================================================================-
// -======================= Built-in Commands =======================-
// -=================================================================-
//

int min_i(int a, int b) { return a < b ? a : b; }

int max_i(int a, int b) { return a > b ? a : b; }

enum QuoteKind {
    QUOTE_NONE,
    QUOTE_DOUBLE,
    QUOTE_SINGLE,
};

#ifdef _WIN32
LPWSTR utf8_to_wide(char *utf8) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *wide = (wchar_t *)malloc(len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len);
    return wide;
}
#endif

Process *raw_execute(Shell *shell, char **argv, int argc, unsigned int wait,
                     char *stdin_data, size_t stdin_len) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE in_read, in_write;
    HANDLE out_read, out_write;
    HANDLE err_read, err_write;

    // Create pipes
    if (!CreatePipe(&in_read, &in_write, &sa, 0))
        return NULL;
    if (!CreatePipe(&out_read, &out_write, &sa, 0)) {
        CloseHandle(in_read);
        CloseHandle(in_write);
        return NULL;
    }
    if (!CreatePipe(&err_read, &err_write, &sa, 0)) {
        CloseHandle(in_read);
        CloseHandle(in_write);
        CloseHandle(out_read);
        CloseHandle(out_write);
        return NULL;
    }

    // No inheritance on these ends
    SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

    // Prepare STARTUPINFO
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_read;
    si.hStdOutput = out_write;
    si.hStdError = err_write;

    PROCESS_INFORMATION pi = {0};

    StringBuilder sb;
    sb_init(&sb, 256);
    sb_append(&sb, argv[0]);
    sb_append_c(&sb, ' ');
    for (int i = 0; i < argc; i++) {
        sb_append(&sb, argv[i]);
        if (i != argc - 1)
            sb_append_c(&sb, ' ');
    }

    LPWSTR wcmd = utf8_to_wide(sb.data);
    LPWSTR args = utf8_to_wide(sb.data);
    LPWSTR wdir = utf8_to_wide(shell->working_directory);

    sb_free(&sb);

    BOOL ok = CreateProcessW(NULL, wcmd, // application name / command line
                             NULL, NULL, // process & thread security
                             TRUE,       // inherit handles
                             CREATE_NO_WINDOW, // flags
                             NULL,             // env
                             wdir,             // working dir
                             &si, &pi);
    free(wcmd);
    free(args);
    free(wdir);

    // Close child-side handles in parent
    CloseHandle(in_read);
    CloseHandle(out_write);
    CloseHandle(err_write);

    if (!ok) {
        CloseHandle(in_write);
        CloseHandle(out_read);
        CloseHandle(err_read);
        return NULL;
    }

    // Write to child stdin
    if (stdin_data) {
        DWORD written;
        WriteFile(in_write, stdin_data, (DWORD)stdin_len, &written, NULL);
    }
    CloseHandle(in_write);

    // Wait for the process (or timeout)
    unsigned long w = WaitForSingleObject(pi.hProcess, wait);

    DWORD child_pid = GetProcessId(pi.hProcess);
    if (w == WAIT_TIMEOUT) {
        Process *proc = (Process *)malloc(sizeof(Process));
        proc->pid = child_pid;
        return proc;
    } else if (w == WAIT_OBJECT_0) {
        ;
    } else {
        return NULL;
    }

    char *stdout_buf = NULL;
    size_t stdout_len = 0;
    char *stderr_buf = NULL;
    size_t stderr_len = 0;
    DWORD bytes_read;
    char chunk[4096];

    while (ReadFile(out_read, chunk, sizeof(chunk), &bytes_read, NULL) &&
           bytes_read > 0) {
        char *tmp = (char *)realloc(stdout_buf, stdout_len + bytes_read + 1);
        if (!tmp)
            break; // allocation failed
        stdout_buf = tmp;
        memcpy(stdout_buf + stdout_len, chunk, bytes_read);
        stdout_len += bytes_read;
        stdout_buf[stdout_len] = '\0';
    }

    // -- read all from stderr:
    while (ReadFile(err_read, chunk, sizeof(chunk), &bytes_read, NULL) &&
           bytes_read > 0) {
        char *tmp = (char *)realloc(stderr_buf, stderr_len + bytes_read + 1);
        if (!tmp)
            break;
        stderr_buf = tmp;
        memcpy(stderr_buf + stderr_len, chunk, bytes_read);
        stderr_len += bytes_read;
        stderr_buf[stderr_len] = '\0';
    }

    CloseHandle(out_read);
    CloseHandle(err_read);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    Process *proc = (Process *)malloc(sizeof(Process));
    proc->pid = child_pid;
    proc->stdout_c = stdout_buf;
    proc->stderr_c = stderr_buf;
    proc->exit_code = exit_code;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return proc;
}

char **split_commands(char *text, int *len) {
    if (len == NULL) {
        return NULL;
    }
    StringBuilder sb;
    int med_size = max_i(utf8_strlen(text) / 16, 1);
    sb_init(&sb, med_size);

    char **commands = NULL;
    int args_len = 0;

    int escape_idx = -1;
    enum QuoteKind quote = QUOTE_NONE;
    int scope;

    utf8 u_str = utf8_str(text);

    for (int i = 0; i < u_str.len; i++) {
        const char *ch = utf8_str_char_at(&u_str, i);
        int remaining = u_str.len - i - 1;
        if (!ch)
            break;
        if (utf8_eq_ascii(ch, '\n') && quote == QUOTE_NONE) {
            if (sb.len > 0) {
                if (args_len == 0) {
                    commands = (char **)malloc(sizeof(char *));
                    commands[args_len] = (char *)malloc(sb.len + 1);
                    memcpy(commands[args_len], sb.data, sb.len + 1);
                } else {
                    commands = (char **)realloc(commands, sizeof(char *) *
                                                              (args_len + 1));
                    commands[args_len] = (char *)malloc(sb.len + 1);
                    memcpy(commands[args_len], sb.data, sb.len + 1);
                }
                args_len++;
                sb_free(&sb);
                sb_init(&sb, med_size);
            }
        } else if ((utf8_eq_ascii(ch, '|') || utf8_eq_ascii(ch, '>') ||
                    utf8_eq_ascii(ch, '&') || utf8_eq_ascii(ch, '{') ||
                    utf8_eq_ascii(ch, '}')) &&
                   quote == QUOTE_NONE) {
            if (sb.len > 0) {
                if (args_len == 0) {
                    commands = (char **)malloc(sizeof(char *));
                    commands[args_len] = (char *)malloc(sb.len + 1);
                    memcpy(commands[args_len], sb.data, sb.len + 1);
                } else {
                    commands = (char **)realloc(commands, sizeof(char *) *
                                                              (args_len + 1));
                    commands[args_len] = (char *)malloc(sb.len + 1);
                    memcpy(commands[args_len], sb.data, sb.len + 1);
                }
                args_len++;
                sb_free(&sb);
                sb_init(&sb, med_size);
            }
            if (remaining > 0 &&
                ((utf8_eq_ascii(ch, '&') &&
                  utf8_eq_ascii(utf8_str_char_at(&u_str, i + 1), '&')) ||
                 (utf8_eq_ascii(ch, '|') &&
                  utf8_eq_ascii(utf8_str_char_at(&u_str, i + 1), '|')))) {
                sb_append_utf8(&sb, ch);
                i++;
            }
            sb_append_utf8(&sb, ch);
            if (sb.len > 0) {
                if (args_len == 0) {
                    commands = (char **)malloc(sizeof(char *));
                    commands[args_len] = (char *)malloc(sb.len + 1);
                    memcpy(commands[args_len], sb.data, sb.len + 1);
                } else {
                    commands = (char **)realloc(commands, sizeof(char *) *
                                                              (args_len + 1));
                    commands[args_len] = (char *)malloc(sb.len + 1);
                    memcpy(commands[args_len], sb.data, sb.len + 1);
                }
                args_len++;
                sb_free(&sb);
                sb_init(&sb, med_size);
            }
        } else if (utf8_eq_ascii(ch, '\\') && escape_idx != i) {
            sb_append_utf8(&sb, ch);
            escape_idx = i + 1;
        } else if (utf8_eq_ascii(ch, '\"') && escape_idx != i) {
            sb_append_utf8(&sb, ch);
            if (quote == QUOTE_NONE) {
                quote = QUOTE_DOUBLE;
            } else if (quote == QUOTE_DOUBLE) {
                quote = QUOTE_NONE;
            }
        } else if (utf8_eq_ascii(ch, '\'') && escape_idx != i) {
            sb_append_utf8(&sb, ch);
            if (quote == QUOTE_NONE) {
                quote = QUOTE_SINGLE;
            } else if (quote == QUOTE_SINGLE) {
                quote = QUOTE_NONE;
            }
        } else {
            sb_append_utf8(&sb, ch);
        }
    }

    if (quote != QUOTE_NONE) {
        *len = 2;
        sb_free(&sb);
        free(commands);
        commands = (char **)malloc(1 * sizeof(char *));
        commands[0] = "error \"SYNTAX ERROR: Quote was not closed.\"";
        return commands;
    }
    if (sb.len > 0) {
        if (args_len == 0) {
            commands = (char **)malloc(sizeof(char *));
            commands[args_len] = (char *)malloc(sb.len + 1);
            memcpy(commands[args_len], sb.data, sb.len + 1);
        } else {
            commands =
                (char **)realloc(commands, sizeof(char *) * (args_len + 1));
            commands[args_len] = (char *)malloc(sb.len + 1);
            memcpy(commands[args_len], sb.data, sb.len + 1);
        }
        args_len++;
    }
    sb_free(&sb);
    *len = args_len;
    return commands;
}

char **split_command(char *command, int *len) {
    if (len == NULL) {
        return NULL;
    }
    StringBuilder sb;
    int med_size = max_i(utf8_strlen(command) / 16, 1);
    sb_init(&sb, med_size);

    char **args = NULL;
    int args_len = 0;

    int escape_idx = -1;
    enum QuoteKind quote = QUOTE_NONE;
    int scope;

    utf8 u_str = utf8_str(command);

    for (int i = 0; i < u_str.len; i++) {
        const char *ch = utf8_str_char_at(&u_str, i);
        int remaining = u_str.len - i - 1;
        if (!ch)
            break;
        if (utf8_eq_ascii(ch, ' ') && quote == QUOTE_NONE) {
            if (sb.len > 0) {
                if (args_len == 0) {
                    args = (char **)malloc(sizeof(char *));
                    args[args_len] = (char *)malloc(sb.len + 1);
                    memcpy(args[args_len], sb.data, sb.len + 1);
                } else {
                    args =
                        (char **)realloc(args, sizeof(char *) * (args_len + 1));
                    args[args_len] = (char *)malloc(sb.len + 1);
                    memcpy(args[args_len], sb.data, sb.len + 1);
                }
                args_len++;
                sb_free(&sb);
                sb_init(&sb, med_size);
            }
        } else if (utf8_eq_ascii(ch, '\\') && escape_idx != i) {
            escape_idx = i + 1;
        } else if (utf8_eq_ascii(ch, '\"') && escape_idx != i) {
            if (quote == QUOTE_NONE) {
                quote = QUOTE_DOUBLE;
            } else if (quote == QUOTE_DOUBLE) {
                quote = QUOTE_NONE;
            }
        } else if (utf8_eq_ascii(ch, '\'') && escape_idx != i) {
            if (quote == QUOTE_NONE) {
                quote = QUOTE_SINGLE;
            } else if (quote == QUOTE_SINGLE) {
                quote = QUOTE_NONE;
            }
        } else {
            sb_append_utf8(&sb, ch);
        }
    }

    if (quote != QUOTE_NONE) {
        *len = 2;
        sb_free(&sb);
        free(args);
        args = (char **)malloc(2 * sizeof(char *));
        args[0] = "error";
        args[1] = "SYNTAX ERROR: Quote was not closed.";
        return args;
    }
    if (sb.len > 0) {
        if (args_len == 0) {
            args = (char **)malloc(sizeof(char *));
            args[args_len] = (char *)malloc(sb.len + 1);
            memcpy(args[args_len], sb.data, sb.len + 1);
        } else {
            args = (char **)realloc(args, sizeof(char *) * (args_len + 1));
            args[args_len] = (char *)malloc(sb.len + 1);
            memcpy(args[args_len], sb.data, sb.len + 1);
        }
        args_len++;
    }
    sb_free(&sb);
    *len = args_len;
    return args;
}

char **separate_paths(char *var) {
    int size = 0;
    for (int i = 0; var[i] != 0;) {
        int len = utf8_len((unsigned char)var[i]);
        if (var[i] == ENV_PATH_SEPARATOR) {
            size++;
        }
        i += len;
    }
    if (size == 0) {
        char **paths = (char **)malloc(1);
        paths[0] = var;
        return paths;
    }
    char **paths = (char **)malloc(sizeof(char *) * size);
    int med_size = utf8_strlen(var) / size;
    StringBuilder sb;
    sb_init(&sb, med_size);
    int p = 0;
    for (int i = 0; i < utf8_strlen(var); i++) {
        const char *ch = utf8_char_at(var, i);
        if (!ch)
            break;
        int len = utf8_len((unsigned char)*ch);
        if (utf8_eq_ascii(ch, ENV_PATH_SEPARATOR)) {
            paths[p] = (char *)malloc(sb.len);
            memcpy(paths[p], sb.data, sb.len);
            sb_free(&sb);
            sb_init(&sb, med_size);
            p++;
        } else {
            sb_append_utf8(&sb, ch);
        }
    }
    sb_free(&sb);
    return paths;
}

Shell *make_shell(bool is_interactive) {
    Shell *shell = (Shell *)malloc(sizeof(Shell));
    if (!shell)
        return NULL;

    shell->working_directory = get_cwd();
    // shell->path = separate_paths(getenv("PATH"));
    shell->global_scope = NULL;

    // Use cast to bypass const init
    *(bool *)&shell->is_interactive = is_interactive;

    printf("[ \n");

    int len = 0;
    char **commands = split_commands(
        "echo 'Hello     world !\\'' > gooz.txt && piss || echo", &len);
    for (int i = 0; i < len; i++) {
        int len2 = 0;
        char **parts = split_command(commands[i], &len2);
        printf("    [ ");
        for (int j = 0; j < len2; j++) {
            if (j == len2 - 1) {
                printf("\"%s\" ", parts[j]);
            } else {
                printf("\"%s\", ", parts[j]);
            }
        }
        printf("]");
        if (i == len - 1) {
            printf("\n");
        } else {
            printf(",\n");
        }
    }
    printf(" ]\n");
    free(commands);

    return shell;
}

Shell *make_shell_no_interactive() { return make_shell(false); }

#endif // SHASH_IMPLEMENTATION

#endif // SHASH_H
