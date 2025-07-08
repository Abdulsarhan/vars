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
VARSAPI int vars_hot_load(vars_file* file);
VARSAPI char* vars_get_string(char* key, vars_file file, char* buffer);
VARSAPI float vars_get_float(char* key, vars_file file);
VARSAPI int vars_get_int(char* key, vars_file file);
VARSAPI vars_vec2 vars_get_vec2(char* key, vars_file file);
VARSAPI vars_vec3 vars_get_vec3(char* key, vars_file file);
VARSAPI vars_vec4 vars_get_vec4(char* key, vars_file file);
VARSAPI int vars_get_bool(char* key, vars_file file);

// New set functions
VARSAPI int vars_set_string(char* key, const char* value, vars_file* file);
VARSAPI int vars_set_float(char* key, float value, vars_file* file);
VARSAPI int vars_set_int(char* key, int value, vars_file* file);
VARSAPI int vars_set_bool(char* key, int value, vars_file* file);
VARSAPI int vars_set_vec2(char* key, vars_vec2 value, vars_file* file);
VARSAPI int vars_set_vec3(char* key, vars_vec3 value, vars_file* file);
VARSAPI int vars_set_vec4(char* key, vars_vec4 value, vars_file* file);

// Save function
VARSAPI int vars_save(vars_file* file);

VARSAPI int vars_free(vars_file file);

#ifdef __cplusplus
}
#endif

#endif // vars_h

#ifdef VARS_IMPLEMENTATION
// disables deprecation of strdup on MSVC.
#pragma warning(disable : 4996)

#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <sys/stat.h>
#include <limits.h>

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

// ---------------------------------------------
// ENTRIES 
// ---------------------------------------------

typedef struct {
    char* section;
    char* key;
    char* value;
} vars_entry;

typedef struct {
    vars_entry* entries;
    size_t count;
    size_t capacity;
} vars_entry_list;

static void vars_entry_list_init(vars_entry_list* list) {
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void vars_entry_list_add(vars_entry_list* list, const char* section, const char* key, const char* value) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->entries = (vars_entry*)realloc(list->entries, list->capacity * sizeof(vars_entry));
    }
    
    vars_entry* entry = &list->entries[list->count++];
    entry->section = section ? strdup(section) : NULL;
    entry->key = strdup(key);
    entry->value = strdup(value);
}

static void vars_entry_list_free(vars_entry_list* list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->entries[i].section);
        free(list->entries[i].key);
        free(list->entries[i].value);
    }
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int vars_entry_compare(const void* a, const void* b) {
    const vars_entry* ea = (const vars_entry*)a;
    const vars_entry* eb = (const vars_entry*)b;
    
    // Sort by section first (NULL sections come first)
    if (!ea->section && !eb->section) return strcmp(ea->key, eb->key);
    if (!ea->section) return -1;
    if (!eb->section) return 1;
    
    int section_cmp = strcmp(ea->section, eb->section);
    if (section_cmp != 0) return section_cmp;
    
    return strcmp(ea->key, eb->key);
}

// ---------------------------------------------
// HELPER FUNCTIONS 
// ---------------------------------------------

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

static int vars__update_or_add_key(const char* key, const char* value, vars_file* file) {
    if (!file || !key || !value) return 0;
    
    size_t new_capacity = file->parsed_len + strlen(key) + strlen(value) + 100; // Extra space
    char* new_buf = (char*)malloc(new_capacity);
    if (!new_buf) return 0;
    
    char* src = file->parsed_buf;
    char* dst = new_buf;
    size_t key_len = strlen(key);
    int key_found = 0;
    
    while (*src) {
        if (*src == ':' && *(src+1) == '/') {
            while (*src && !IS_END_OF_LINE(*src)) {
                *dst++ = *src++;
            }
            if (*src) *dst++ = *src++;
            continue;
        }
        
        if (strncmp(src, key, key_len) == 0 && IS_WHITE_SPACE(src[key_len])) {
            key_found = 1;
            
            strcpy(dst, key);
            dst += key_len;
            
            *dst++ = ' ';
            strcpy(dst, value);
            dst += strlen(value);
            
            while (*src && !IS_END_OF_LINE(*src)) src++;
            
            if (*src) *dst++ = *src++;
        } else {
            while (*src && !IS_END_OF_LINE(*src)) {
                *dst++ = *src++;
            }
            if (*src) *dst++ = *src++;
        }
    }
    
    if (!key_found) {
        strcpy(dst, key);
        dst += key_len;
        *dst++ = ' ';
        strcpy(dst, value);
        dst += strlen(value);
        *dst++ = '\n';
    }
    
    *dst = '\0';
    
    free(file->parsed_buf);
    file->parsed_buf = new_buf;
    file->parsed_len = dst - new_buf;
    
    return 1;
}

// ---------------------------------------------
// SET FUNCTIONS
// ---------------------------------------------

VARSAPI int vars_set_string(char* key, const char* value, vars_file* file) {
    if (!key || !value || !file) return 0;
    
    // Format as quoted string
    size_t len = strlen(value);
    char* quoted_value = (char*)malloc(len + 3); // +2 for quotes, +1 for null terminator
    if (!quoted_value) return 0;
    
    quoted_value[0] = '"';
    strcpy(quoted_value + 1, value);
    quoted_value[len + 1] = '"';
    quoted_value[len + 2] = '\0';
    
    int result = vars__update_or_add_key(key, quoted_value, file);
    free(quoted_value);
    return result;
}

VARSAPI int vars_set_float(char* key, float value, vars_file* file) {
    if (!key || !file) return 0;
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.6f", value);
    return vars__update_or_add_key(key, buffer, file);
}

VARSAPI int vars_set_int(char* key, int value, vars_file* file) {
    if (!key || !file) return 0;
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return vars__update_or_add_key(key, buffer, file);
}

VARSAPI int vars_set_bool(char* key, int value, vars_file* file) {
    if (!key || !file) return 0;
    
    return vars__update_or_add_key(key, value ? "true" : "false", file);
}

VARSAPI int vars_set_vec2(char* key, vars_vec2 value, vars_file* file) {
    if (!key || !file) return 0;
    
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "(%.6f %.6f)", value.x, value.y);
    return vars__update_or_add_key(key, buffer, file);
}

VARSAPI int vars_set_vec3(char* key, vars_vec3 value, vars_file* file) {
    if (!key || !file) return 0;
    
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "(%.6f %.6f %.6f)", value.x, value.y, value.z);
    return vars__update_or_add_key(key, buffer, file);
}

VARSAPI int vars_set_vec4(char* key, vars_vec4 value, vars_file* file) {
    if (!key || !file) return 0;
    
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "(%.6f %.6f %.6f %.6f)", value.x, value.y, value.z, value.w);
    return vars__update_or_add_key(key, buffer, file);
}

// ---------------------------------------------
// SAVING
// ---------------------------------------------

VARSAPI int vars_save(vars_file* file) {
    if (!file || !file->file_path) return 0;
    
    vars_entry_list list;
    vars_entry_list_init(&list);
    
    char* p = file->parsed_buf;
    
    while (*p) {
        // Skip subfolder declarations
        if (*p == ':' && *(p+1) == '/') {
            while (*p && !IS_END_OF_LINE(*p)) p++;
            if (*p) p++;
            continue;
        }
        
        // Parse key-value pair
        char* key_start = p;
        while (*p && !IS_WHITE_SPACE(*p) && !IS_END_OF_LINE(*p)) p++;
        
        if (key_start == p) {
            // Empty line, skip
            if (*p) p++;
            continue;
        }
        
        size_t key_len = p - key_start;
        char* key = (char*)malloc(key_len + 1);
        strncpy(key, key_start, key_len);
        key[key_len] = '\0';
        
        // Skip whitespace
        while (*p && IS_WHITE_SPACE(*p)) p++;
        
        // Get value
        char* value_start = p;
        while (*p && !IS_END_OF_LINE(*p)) p++;
        
        size_t value_len = p - value_start;
        char* value = (char*)malloc(value_len + 1);
        strncpy(value, value_start, value_len);
        value[value_len] = '\0';
        
        // Split key into section and key parts
        char* slash = strchr(key, '/');
        if (slash) {
            *slash = '\0';
            vars_entry_list_add(&list, key, slash + 1, value);
        } else {
            vars_entry_list_add(&list, NULL, key, value);
        }
        
        free(key);
        free(value);
        
        // Skip newline
        if (*p) p++;
    }
    
    qsort(list.entries, list.count, sizeof(vars_entry), vars_entry_compare);
    
    // Write to file
    FILE* fp = fopen(file->file_path, "w");
    if (!fp) {
        vars_entry_list_free(&list);
        return 0;
    }
    
    const char* current_section = NULL;
    
    for (size_t i = 0; i < list.count; i++) {
        vars_entry* entry = &list.entries[i];
        
        // Check if we need to write a section header
        if (entry->section) {
            if (!current_section || strcmp(current_section, entry->section) != 0) {
                if (i > 0) fprintf(fp, "\n"); // new line before new section.
                fprintf(fp, ":/%s\n", entry->section);
                current_section = entry->section;
            }
        } else {
            if (current_section) {
                if (i > 0) fprintf(fp, "\n"); // new line before global section
                current_section = NULL;
            }
        }
        
        fprintf(fp, "%s %s\n", entry->key, entry->value);
    }
    
    fclose(fp);
    vars_entry_list_free(&list);
    
    file->last_modified = vars__get_file_mod_time(file->file_path);
    
    return 1;
}

// ---------------------------------------------
// GET FUNCTIONS
// ---------------------------------------------

VARSAPI int vars_hot_load(vars_file* file) {
    if (!file || !file->file_path) {
        return 0;
    }

    long current_mod_time = vars__get_file_mod_time(file->file_path);
    if (current_mod_time == -1) {
        return 0;
    }

    if (current_mod_time <= file->last_modified) {
        return 0;
    }

    vars_file new_file = vars__load_and_parse_file(file->file_path);
    
    size_t path_len = strlen(file->file_path);
    new_file.file_path = (char*)malloc(path_len + 1);
    if (!new_file.file_path) {
        fprintf(stderr, "ERROR: vars_hot_load: Memory allocation failed.\n");
        vars_free(new_file);
        return 0;
    }
    strcpy(new_file.file_path, file->file_path);
    new_file.last_modified = current_mod_time;

    if (file->filebuf) free(file->filebuf);
    if (file->parsed_buf) free(file->parsed_buf);

    *file = new_file;
    
    return 1; // 1 for success.
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
    if (!val) return 0.0f; 

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
