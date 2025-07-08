#ifndef VARS_H
#define VARS_H

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char* filebuf;
    char* parsed_buf;
    size_t parsed_len;
    char* file_path;
    long last_modified;
} vars_file;

typedef struct { float x, y; } vars_vec2;
typedef struct { float x, y, z; } vars_vec3;
typedef struct { float x, y, z, w; } vars_vec4;

#ifdef VARS_DO_NOT_PREFIX_TYPES
typedef vars_vec2 vec2;
typedef vars_vec3 vec3;
typedef vars_vec4 vec4;
#endif

#ifdef VARS_STATIC
#define VARSAPI static
#else
#define VARSAPI extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

VARSAPI vars_file vars_load(const char* file_path);
VARSAPI int vars_hot_reload(vars_file* file);
VARSAPI char* vars_get_string(char* key, vars_file file, char* buffer);
VARSAPI float vars_get_float(char* key, vars_file file);
VARSAPI int vars_get_int(char* key, vars_file file);
VARSAPI vars_vec2 vars_get_vec2(char* key, vars_file file);
VARSAPI vars_vec3 vars_get_vec3(char* key, vars_file file);
VARSAPI vars_vec4 vars_get_vec4(char* key, vars_file file);
VARSAPI int vars_get_bool(char* key, vars_file file);
VARSAPI int vars_free(vars_file file);

#ifdef __cplusplus
}
#endif

#endif // vars_h

#ifdef VARS_IMPLEMENTATION

#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <sys/stat.h>

#define IS_CAPITAL_LETTER(ch)  (((ch) >= 'A') && ((ch) <= 'Z'))
#define IS_LOWER_CASE_LETTER(ch)  (((ch) >= 'a') && ((ch) <= 'z'))
#define IS_LETTER(ch)  (IS_CAPITAL_LETTER(ch) || IS_LOWER_CASE_LETTER(ch))
#define IS_END_OF_LINE(ch)  (((ch) == '\r') || ((ch) == '\n'))
#define IS_WHITE_SPACE(ch)  (((ch) == ' ') || ((ch) == '\t') || ((ch) == '\v') || ((ch) == '\f'))
#define IS_NUMBER(ch)  (((ch) >= '0') && ((ch) <= '9'))
#define IS_UNDERSCORE(ch)  ((ch) == '_')
#define IS_HYPHEN(ch)  ((ch) == '-')
#define IS_DOT(ch) ((ch) == '.')
#define IS_DOUBLEQUOTES(ch) ((ch) == '"')
#define IS_PAREN(ch) (((ch) == '(')  || ((ch) == ')'))
#define IS_COLON(ch) ((ch) == ':')
#define IS_SLASH(ch) ((ch) == '/')
#define ARE_CHARS_EQUAL(ch1, ch2) ((ch1) == (ch2))

static long vars__get_file_mod_time(const char* file_path) {
    struct stat st;
    if (stat(file_path, &st) == 0) {
        return (long)st.st_mtime;
    }
    return -1;
}

static vars_file vars__load_and_parse_file(const char* file_path) {
    vars_file file = { 0 };

    FILE* handle = fopen(file_path, "rb");
    if (!handle) {
        fprintf(stderr, "ERROR: vars_load: Failed to open file: %s\n", file_path);
        exit(EXIT_FAILURE);
    }

    fseek(handle, 0, SEEK_END);
    long file_size = ftell(handle);
    rewind(handle);

    if (file_size <= 0) {
        fprintf(stderr, "ERROR: vars_load: Invalid file size.\n");
        fclose(handle);
        exit(EXIT_FAILURE);
    }

    file.filebuf = (char*)malloc(file_size);
    file.parsed_buf = (char*)malloc(file_size * 2); // Extra space for subfolder prefixes
    if (!file.filebuf || !file.parsed_buf) {
        fprintf(stderr, "ERROR: vars_load: Memory allocation failed.\n");
        fclose(handle);
        exit(EXIT_FAILURE);
    }

    size_t read = fread(file.filebuf, 1, file_size, handle);
    fclose(handle);

    if (read != (size_t)file_size) {
        fprintf(stderr, "ERROR: vars_load: File read incomplete (%zu of %ld bytes).\n", read, file_size);
        free(file.filebuf);
        free(file.parsed_buf);
        exit(EXIT_FAILURE);
    }

    char* cursor = file.filebuf;
    char* end_of_file = file.filebuf + file_size;
    char current_subfolder[256] = "";  // Current subfolder prefix

    file.parsed_len = 0; // Reset parsed length

    while (cursor < end_of_file) {
        while (cursor < end_of_file && (IS_WHITE_SPACE(*cursor) || IS_END_OF_LINE(*cursor))) {
            cursor++;
        }

        // Start of a line
        int parsed_any = 0;

        while (cursor < end_of_file && !IS_END_OF_LINE(*cursor)) {
            if (*cursor == '#') {
                while (cursor < end_of_file && !IS_END_OF_LINE(*cursor)) {
                    cursor++;
                }
                break;
            }

            // Check for subfolder declaration :/subfolder
            if (*cursor == ':' && cursor + 1 < end_of_file && *(cursor + 1) == '/') {
                cursor += 2; // Skip ":/"
                
                // Get subfolder name
                size_t ns_len = 0;
                while (cursor < end_of_file && !IS_END_OF_LINE(*cursor) && !IS_WHITE_SPACE(*cursor)) {
                    if (ns_len < sizeof(current_subfolder) - 2) {
                        current_subfolder[ns_len++] = *cursor;
                    }
                    cursor++;
                }
                current_subfolder[ns_len] = '\0';
                
                // Add subfolder declaration to parsed buffer
                if (parsed_any) {
                    file.parsed_buf[file.parsed_len++] = ' ';
                }
                file.parsed_buf[file.parsed_len++] = ':';
                file.parsed_buf[file.parsed_len++] = '/';
                for (size_t i = 0; i < ns_len; i++) {
                    file.parsed_buf[file.parsed_len++] = current_subfolder[i];
                }
                parsed_any = 1;
                continue;
            }

            if (*cursor == '"') {
                if (parsed_any) {
                    file.parsed_buf[file.parsed_len++] = ' ';
                }
                file.parsed_buf[file.parsed_len++] = *cursor++;
                while (cursor < end_of_file && *cursor != '"' && !IS_END_OF_LINE(*cursor)) {
                    file.parsed_buf[file.parsed_len++] = *cursor++;
                }
                if (cursor < end_of_file && *cursor == '"') {
                    file.parsed_buf[file.parsed_len++] = *cursor++;
                } else {
                    fprintf(stderr, "ERROR: Unterminated string\n");
                }
                parsed_any = 1;
                continue;
            }

            if (IS_LETTER(*cursor) || IS_NUMBER(*cursor) || IS_UNDERSCORE(*cursor)
                || IS_HYPHEN(*cursor) || IS_DOT(*cursor) || IS_PAREN(*cursor)) {

                if (parsed_any) {
                    file.parsed_buf[file.parsed_len++] = ' ';
                }

                // Check if this is a key (first token on line after subfolder)
                int is_key = !parsed_any;
                
                // If we have a subfolder and this is a key, prefix it
                if (is_key && current_subfolder[0] != '\0') {
                    size_t ns_len = strlen(current_subfolder);
                    for (size_t i = 0; i < ns_len; i++) {
                        file.parsed_buf[file.parsed_len++] = current_subfolder[i];
                    }
                    file.parsed_buf[file.parsed_len++] = '/';
                }

                while (cursor < end_of_file &&
                       (IS_LETTER(*cursor) || IS_NUMBER(*cursor) || IS_UNDERSCORE(*cursor) ||
                        IS_HYPHEN(*cursor) || IS_DOT(*cursor) || IS_PAREN(*cursor))) {
                    file.parsed_buf[file.parsed_len++] = *cursor++;
                }

                parsed_any = 1;
                continue;
            }

            cursor++;
        }

        if (parsed_any) {
            file.parsed_buf[file.parsed_len++] = '\n';
        }

        while (cursor < end_of_file && IS_END_OF_LINE(*cursor)) {
            cursor++;
        }
    }

    file.parsed_buf[file.parsed_len] = '\0';

    return file;
}

static char* vars__find_key_value(const char* key, vars_file file) {
    char* p = file.parsed_buf;
    size_t key_len = strlen(key);
    
    while (*p) {
        // Skip subfolder declarations
        if (*p == ':' && *(p+1) == '/') {
            while (*p && !IS_END_OF_LINE(*p)) p++;
            if (*p) p++;
            continue;
        }
        
        if (strncmp(p, key, key_len) == 0 && IS_WHITE_SPACE(p[key_len])) {
            p += key_len;
            while (IS_WHITE_SPACE(*p)) p++;
            return p;
        }
        while (*p && !IS_END_OF_LINE(*p)) p++;
        if (*p) p++;
    }
    return NULL;
}

VARSAPI int vars_hot_reload(vars_file* file) {
    if (!file || !file->file_path) {
        return 0; // No file to reload
    }

    long current_mod_time = vars__get_file_mod_time(file->file_path);
    if (current_mod_time == -1) {
        return 0; // Failed to get modification time
    }

    if (current_mod_time <= file->last_modified) {
        return 0; // File hasn't changed
    }

    // File has changed, reload it
    vars_file new_file = vars__load_and_parse_file(file->file_path);
    
    // Copy file path and update modification time
    size_t path_len = strlen(file->file_path);
    new_file.file_path = (char*)malloc(path_len + 1);
    if (!new_file.file_path) {
        fprintf(stderr, "ERROR: vars_hot_reload: Memory allocation failed.\n");
        vars_free(new_file);
        return 0;
    }
    strcpy(new_file.file_path, file->file_path);
    new_file.last_modified = current_mod_time;

    // Free old buffers
    if (file->filebuf) free(file->filebuf);
    if (file->parsed_buf) free(file->parsed_buf);

    // Replace with new data
    *file = new_file;
    
    return 1; // Successfully reloaded
}

VARSAPI char* vars_get_string(char* key, vars_file file, char* buffer) {
    const char* val = vars__find_key_value(key, file);
    if (!val) return NULL;

    if (*val != '"') return NULL;  // Must start with quote

    val++; // skip opening quote

    size_t i = 0;
    while (val[i] != '"' && val[i] != '\0') {
        buffer[i] = val[i];
        i++;
    }

    if (val[i] != '"') return NULL; // No closing quote

    buffer[i] = '\0';
    return buffer;
}

VARSAPI float vars_get_float(char* key, vars_file file) {
    const char* val = vars__find_key_value(key, file);
    if (!val) return 0.0f; // Return 0.0f on failure

    char* endptr;
    float f = strtof(val, &endptr);
    if (val == endptr) return 0.0f;

    return f;
}

VARSAPI int vars_get_int(char* key, vars_file file) {
    const char* val = vars__find_key_value(key, file);
    if (!val) return INT_MIN;

    char* endptr;
    long i = strtol(val, &endptr, 10);
    if (val == endptr) return INT_MIN;

    return (int)i;
}

static int vars__parse_vec(const char* val, float* out, int count) {
    if (*val != '(') return 0;

    val++; // skip '('
    for (int i = 0; i < count; i++) {
        while (*val && isspace((unsigned char)*val)) val++;

        if (!*val) return 0;

        char* endptr;
        out[i] = strtof(val, &endptr);
        if (val == endptr) return 0;

        val = endptr;
    }
    while (*val && isspace((unsigned char)*val)) val++;

    if (*val != ')') return 0;

    return 1;
}

VARSAPI vars_vec2 vars_get_vec2(char* key, vars_file file) {
    vars_vec2 v = {0,0};
    const char* val = vars__find_key_value(key, file);
    if (!val) return v;
    if (!vars__parse_vec(val, (float*)&v, 2)) return v;
    return v;
}

VARSAPI vars_vec3 vars_get_vec3(char* key, vars_file file) {
    vars_vec3 v = {0,0,0};
    const char* val = vars__find_key_value(key, file);
    if (!val) return v;
    if (!vars__parse_vec(val, (float*)&v, 3)) return v;
    return v;
}

VARSAPI vars_vec4 vars_get_vec4(char* key, vars_file file) {
    vars_vec4 v = {0,0,0,0};
    const char* val = vars__find_key_value(key, file);
    if (!val) return v;
    if (!vars__parse_vec(val, (float*)&v, 4)) return v;
    return v;
}

VARSAPI int vars_get_bool(char* key, vars_file file) {
    const char* val = vars__find_key_value(key, file);
    if (!val) return 0;

    if (strncmp(val, "true", 4) == 0 && !isalnum((unsigned char)val[4])) {
        return 1;
    }
    else if (strncmp(val, "false", 5) == 0 && !isalnum((unsigned char)val[5])) {
        return 0;
    }

    return -1; // return this on failure
}

VARSAPI int vars_free(vars_file file) {
    if (file.filebuf) free(file.filebuf);
    if (file.parsed_buf) free(file.parsed_buf);
    if (file.file_path) free(file.file_path);
    return 0;
}

VARSAPI vars_file vars_load(const char* file_path) {
    vars_file file = vars__load_and_parse_file(file_path);
    
    // Store file path and modification time for hot reload
    size_t path_len = strlen(file_path);
    file.file_path = (char*)malloc(path_len + 1);
    if (!file.file_path) {
        fprintf(stderr, "ERROR: vars_load: Memory allocation failed for file path.\n");
        vars_free(file);
        exit(EXIT_FAILURE);
    }
    strcpy(file.file_path, file_path);
    file.last_modified = vars__get_file_mod_time(file_path);
    
    return file;
}

#endif // VARS_IMPLEMENTATION
