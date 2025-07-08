#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>

#ifndef VARS_H
#define VARS_H

typedef struct {
    char* filebuf;
    char* parsed_buf;
    size_t parsed_len;
    struct vars_map* map;
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

VARSAPI char* vars_get_string(char* key, vars_file* file, char* buffer);
VARSAPI float vars_get_float(char* key, vars_file* file);
VARSAPI int vars_get_int(char* key, vars_file* file);
VARSAPI int vars_get_bool(char* key, vars_file* file);
VARSAPI vars_vec2 vars_get_vec2(char* key, vars_file* file);
VARSAPI vars_vec3 vars_get_vec3(char* key, vars_file* file);
VARSAPI vars_vec4 vars_get_vec4(char* key, vars_file* file);

VARSAPI int vars_set_string(char* key, const char* value, vars_file* file);
VARSAPI int vars_set_float(char* key, float value, vars_file* file);
VARSAPI int vars_set_int(char* key, int value, vars_file* file);
VARSAPI int vars_set_bool(char* key, int value, vars_file* file);
VARSAPI int vars_set_vec2(char* key, vars_vec2 value, vars_file* file);
VARSAPI int vars_set_vec3(char* key, vars_vec3 value, vars_file* file);
VARSAPI int vars_set_vec4(char* key, vars_vec4 value, vars_file* file);

VARSAPI int vars_save(vars_file* file);

VARSAPI int vars_free(vars_file file);

#ifdef __cplusplus
}
#endif

#endif // VARS_H

#ifdef VARS_IMPLEMENTATION

#ifdef _WIN32
// disables deprecation of strdup on MSVC.
#pragma warning(disable : 4996)
#endif

#include <ctype.h>

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

// ---------------------------------------------
// HASHMAP IMPLEMENTATION
// ---------------------------------------------

typedef struct {
    const char* key;
    const char* value;
} vars_kv_pair;

typedef struct vars_map {
    vars_kv_pair* entries;
    size_t count;
    size_t capacity;
} vars_map;

static uint32_t hash_fnv1a(const char* key) {
    uint32_t hash = 2166136261u;
    while (*key) {
        hash ^= (uint8_t)(*key++);
        hash *= 16777619u;
    }
    return hash;
}

static int vars_map_init(vars_map* map, size_t capacity) {
    map->entries = (vars_kv_pair*)calloc(capacity, sizeof(vars_kv_pair));
    map->count = 0;
    map->capacity = capacity;
    return map->entries != NULL;
}

static void vars_map_insert(vars_map* map, const char* key, const char* value) {
    uint32_t hash = hash_fnv1a(key);
    size_t idx = hash % map->capacity;

    for (size_t i = 0; i < map->capacity; i++) {
        size_t probe = (idx + i) % map->capacity;
        if (map->entries[probe].key == NULL) {
            map->entries[probe].key = key;
            map->entries[probe].value = value;
            map->count++;
            return;
        }
    }
}

static const char* vars_map_get(vars_map* map, const char* key) {
    uint32_t hash = hash_fnv1a(key);
    size_t idx = hash % map->capacity;

    for (size_t i = 0; i < map->capacity; i++) {
        size_t probe = (idx + i) % map->capacity;
        if (map->entries[probe].key == NULL) return NULL;
        if (strcmp(map->entries[probe].key, key) == 0) {
            return map->entries[probe].value;
        }
    }
    return NULL;
}

static void vars_map_free(vars_map* map) {
    free(map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

// ---------------------------------------------
// HOT RELOAD SUPPORT
// ---------------------------------------------

static long vars__get_file_mod_time(const char* file_path) {
    struct stat st;
    if (stat(file_path, &st) == 0) {
        return (long)st.st_mtime;
    }
    return -1;
}

static vars_file vars__load_and_parse_file(const char* file_path) {
    vars_file file = {0};

    FILE* fp = fopen(file_path, "rb");
    if (!fp) {
        fprintf(stderr, "ERROR: vars_load: Failed to open file: %s\n", file_path);
        exit(EXIT_FAILURE);
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    
    if (size <= 0) {
        fprintf(stderr, "ERROR: vars_load: Invalid file size.\n");
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    
    file.filebuf = (char*)malloc(size);
    file.parsed_buf = (char*)malloc(size);
    if (!file.filebuf || !file.parsed_buf) {
        fprintf(stderr, "ERROR: vars_load: Memory allocation failed.\n");
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    
    size_t read = fread(file.filebuf, 1, size, fp);
    fclose(fp);
    
    if (read != (size_t)size) {
        fprintf(stderr, "ERROR: vars_load: File read incomplete (%zu of %ld bytes).\n", read, size);
        free(file.filebuf);
        free(file.parsed_buf);
        exit(EXIT_FAILURE);
    }

    char* cursor = file.filebuf;
    char* end = file.filebuf + size;
    char* dst = file.parsed_buf;
    file.map = (vars_map*)malloc(sizeof(vars_map));
    vars_map_init(file.map, 128);

    char section[256] = "";

    while (cursor < end) {
        while (cursor < end && IS_WHITE_SPACE(*cursor)) cursor++;
        if (cursor >= end) break;
        
        if (*cursor == '#') {
            while (cursor < end && !IS_END_OF_LINE(*cursor)) cursor++;
            while (cursor < end && IS_END_OF_LINE(*cursor)) cursor++;
            continue;
        }

        if (*cursor == ':' && cursor + 1 < end && cursor[1] == '/') {
            cursor += 2;
            size_t i = 0;
            while (cursor < end && !IS_WHITE_SPACE(*cursor) && !IS_END_OF_LINE(*cursor) && i < sizeof(section) - 1) {
                section[i++] = *cursor++;
            }
            section[i] = '\0';
            while (cursor < end && IS_END_OF_LINE(*cursor)) cursor++;
            continue;
        }

        // Skip empty lines
        if (IS_END_OF_LINE(*cursor)) {
            while (cursor < end && IS_END_OF_LINE(*cursor)) cursor++;
            continue;
        }

        char* key = dst;
        while (cursor < end && (IS_LETTER(*cursor) || IS_NUMBER(*cursor) || IS_UNDERSCORE(*cursor))) {
            *dst++ = *cursor++;
        }
        
        if (key == dst) {
            // No key found, skip this line
            while (cursor < end && !IS_END_OF_LINE(*cursor)) cursor++;
            while (cursor < end && IS_END_OF_LINE(*cursor)) cursor++;
            continue;
        }
        
        *dst++ = '\0';

        while (cursor < end && IS_WHITE_SPACE(*cursor)) cursor++;

        char* value = dst;
        if (cursor < end && *cursor == '"') {
            *dst++ = *cursor++;
            while (cursor < end && *cursor != '"') *dst++ = *cursor++;
            if (cursor < end && *cursor == '"') *dst++ = *cursor++;
        } else {
            while (cursor < end && !IS_END_OF_LINE(*cursor) && *cursor != '#') *dst++ = *cursor++;
            // Trim trailing whitespace
            while (dst > value && IS_WHITE_SPACE(*(dst-1))) dst--;
        }
        *dst++ = '\0';

        char qualified_key[512];
        if (section[0]) {
            snprintf(qualified_key, sizeof(qualified_key), "%s/%s", section, key);
        } else {
            snprintf(qualified_key, sizeof(qualified_key), "%s", key);
        }

        vars_map_insert(file.map, strdup(qualified_key), value);
        while (cursor < end && IS_END_OF_LINE(*cursor)) cursor++;
    }

    file.parsed_len = (size_t)(dst - file.parsed_buf);
    return file;
}

// ---------------------------------------------
// HELPER FUNCTIONS
// ---------------------------------------------

static const char* find_key_value(char* key, vars_file* file) {
    if (!file->map) return NULL;
    return vars_map_get(file->map, key);
}

static int parse_vec(const char* val, float* out, int count) {
    if (*val != '(') return 0;
    val++;
    for (int i = 0; i < count; i++) {
        while (*val && IS_WHITE_SPACE(*val)) val++;
        char* end;
        out[i] = strtof(val, &end);
        if (val == end) return 0;
        val = end;
    }
    while (*val && IS_WHITE_SPACE(*val)) val++;
    return *val == ')';
}

// ---------------------------------------------
// SAVE FUNCTIONS
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

static int vars__set_value(char* key, const char* value, vars_file* file) {
    if (!file || !file->map) return 0;
    
    // Check if key already exists in hashmap
    const char* existing = vars_map_get(file->map, key);
    if (existing) {
        // Key exists, update the value in the hashmap
        // We need to update the parsed_buf entry that corresponds to this key
        
        // Find the entry in the hashmap and replace it
        uint32_t hash = hash_fnv1a(key);
        size_t idx = hash % file->map->capacity;
        
        for (size_t i = 0; i < file->map->capacity; i++) {
            size_t probe = (idx + i) % file->map->capacity;
            if (file->map->entries[probe].key && strcmp(file->map->entries[probe].key, key) == 0) {
                // Found the entry, update the value
                file->map->entries[probe].value = strdup(value);
                return 1;
            }
        }
    } else {
        // Key doesn't exist, add it
        vars_map_insert(file->map, strdup(key), strdup(value));
    }
    
    return 1;
}

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
    
    int result = vars__set_value(key, quoted_value, file);
    free(quoted_value);
    return result;
}

VARSAPI int vars_set_float(char* key, float value, vars_file* file) {
    if (!key || !file) return 0;
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.6f", value);
    return vars__set_value(key, buffer, file);
}

VARSAPI int vars_set_int(char* key, int value, vars_file* file) {
    if (!key || !file) return 0;
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return vars__set_value(key, buffer, file);
}

VARSAPI int vars_set_bool(char* key, int value, vars_file* file) {
    if (!key || !file) return 0;
    
    return vars__set_value(key, value ? "true" : "false", file);
}

VARSAPI int vars_set_vec2(char* key, vars_vec2 value, vars_file* file) {
    if (!key || !file) return 0;
    
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "(%.6f %.6f)", value.x, value.y);
    return vars__set_value(key, buffer, file);
}

VARSAPI int vars_set_vec3(char* key, vars_vec3 value, vars_file* file) {
    if (!key || !file) return 0;
    
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "(%.6f %.6f %.6f)", value.x, value.y, value.z);
    return vars__set_value(key, buffer, file);
}

VARSAPI int vars_set_vec4(char* key, vars_vec4 value, vars_file* file) {
    if (!key || !file) return 0;
    
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "(%.6f %.6f %.6f %.6f)", value.x, value.y, value.z, value.w);
    return vars__set_value(key, buffer, file);
}

VARSAPI int vars_save(vars_file* file) {
    if (!file || !file->file_path || !file->map) return 0;
    
    // Create a list of all entries sorted by section and key
    vars_entry_list list;
    vars_entry_list_init(&list);
    
    // Extract all entries from the hashmap
    for (size_t i = 0; i < file->map->capacity; i++) {
        if (file->map->entries[i].key) {
            const char* full_key = file->map->entries[i].key;
            const char* value = file->map->entries[i].value;
            
            // Split the key into section and key parts
            const char* slash = strchr(full_key, '/');
            if (slash) {
                // Has section
                size_t section_len = slash - full_key;
                char* section = (char*)malloc(section_len + 1);
                strncpy(section, full_key, section_len);
                section[section_len] = '\0';
                
                vars_entry_list_add(&list, section, slash + 1, value);
                free(section);
            } else {
                // No section (global)
                vars_entry_list_add(&list, NULL, full_key, value);
            }
        }
    }
    
    // Sort entries by section and key
    qsort(list.entries, list.count, sizeof(vars_entry), vars_entry_compare);
    
    // Write to file
    FILE* fp = fopen(file->file_path, "w");
    if (!fp) {
        vars_entry_list_free(&list);
        return 0;
    }
    
    const char* current_section = NULL;
    int wrote_section_header = 0;
    
    for (size_t i = 0; i < list.count; i++) {
        vars_entry* entry = &list.entries[i];
        
        // Check if we need to make a subfolder
        if (entry->section) {
            if (!current_section || strcmp(current_section, entry->section) != 0) {
                if (i > 0) fprintf(fp, "\n"); // Add blank line before new subfolder
                fprintf(fp, ":/%s\n", entry->section);
                current_section = entry->section;
                wrote_section_header = 1;
            }
        } else {
            // Global section
            if (current_section) {
                if (i > 0) fprintf(fp, "\n"); // Add blank line before global section
                current_section = NULL;
                wrote_section_header = 0;
            }
        }
        
        // Write key-value pair
        fprintf(fp, "%s %s\n", entry->key, entry->value);
    }
    
    fclose(fp);
    vars_entry_list_free(&list);
    
    // Update the file's modification time
    file->last_modified = vars__get_file_mod_time(file->file_path);
    
    return 1;
}

// ---------------------------------------------
// PARSER + GET FUNCTIONS
// ---------------------------------------------

VARSAPI int vars_hot_load(vars_file* file) {
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
        fprintf(stderr, "ERROR: vars_hot_load: Memory allocation failed.\n");
        vars_free(new_file);
        return 0;
    }
    strcpy(new_file.file_path, file->file_path);
    new_file.last_modified = current_mod_time;

    // Free old buffers and hashmap
    if (file->filebuf) free(file->filebuf);
    if (file->parsed_buf) free(file->parsed_buf);
    if (file->map) {
        vars_map_free(file->map);
        free(file->map);
    }

    // Replace with new data
    *file = new_file;
    
    return 1; // Successfully reloaded
}

VARSAPI char* vars_get_string(char* key, vars_file* file, char* buffer) {
    const char* val = find_key_value(key, file);
    if (!val || *val != '"') return NULL;
    val++;
    size_t i = 0;
    while (val[i] && val[i] != '"') {
        buffer[i] = val[i];
        i++;
    }
    buffer[i] = '\0';
    return buffer;
}

VARSAPI float vars_get_float(char* key, vars_file* file) {
    const char* val = find_key_value(key, file);
    if (!val) return 0.0f;
    return strtof(val, NULL);
}

VARSAPI int vars_get_int(char* key, vars_file* file) {
    const char* val = find_key_value(key, file);
    if (!val) return INT_MIN;
    return (int)strtol(val, NULL, 10);
}

VARSAPI int vars_get_bool(char* key, vars_file* file) {
    const char* val = find_key_value(key, file);
    if (!val) return 0;
    return (strcmp(val, "true") == 0) ? 1 : 0;
}

VARSAPI vars_vec2 vars_get_vec2(char* key, vars_file* file) {
    vars_vec2 v = {0};
    const char* val = find_key_value(key, file);
    if (val) parse_vec(val, (float*)&v, 2);
    return v;
}

VARSAPI vars_vec3 vars_get_vec3(char* key, vars_file* file) {
    vars_vec3 v = {0};
    const char* val = find_key_value(key, file);
    if (val) parse_vec(val, (float*)&v, 3);
    return v;
}

VARSAPI vars_vec4 vars_get_vec4(char* key, vars_file* file) {
    vars_vec4 v = {0};
    const char* val = find_key_value(key, file);
    if (val) parse_vec(val, (float*)&v, 4);
    return v;
}

VARSAPI int vars_free(vars_file file) {
    if (file.filebuf) free(file.filebuf);
    if (file.parsed_buf) free(file.parsed_buf);
    if (file.file_path) free(file.file_path);
    if (file.map) {
        vars_map_free(file.map);
        free(file.map);
    }
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
