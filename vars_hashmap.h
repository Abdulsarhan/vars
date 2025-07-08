#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#ifndef VARS_H
#define VARS_H

typedef struct {
    char* filebuf;
    char* parsed_buf;
    size_t parsed_len;
    struct vars_map* map;
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
VARSAPI char* vars_get_string(char* key, vars_file* file, char* buffer);
VARSAPI float vars_get_float(char* key, vars_file* file);
VARSAPI int vars_get_int(char* key, vars_file* file);
VARSAPI int vars_get_bool(char* key, vars_file* file);
VARSAPI vars_vec2 vars_get_vec2(char* key, vars_file* file);
VARSAPI vars_vec3 vars_get_vec3(char* key, vars_file* file);
VARSAPI vars_vec4 vars_get_vec4(char* key, vars_file* file);
VARSAPI int vars_free(vars_file file);

#ifdef __cplusplus
}
#endif

#endif // vars_h

#ifdef VARS_IMPLEMENTATION

#ifdef _WIN32
// disables deprecation of stdup on MSVC.
#pragma warning(disable : 4996)
#endif

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
// PARSER + GET FUNCTIONS
// ---------------------------------------------

static const char* find_key_value(const char* key, vars_file* file) {
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

VARSAPI vec2 vars_get_vec2(char* key, vars_file* file) {
    vec2 v = {0};
    const char* val = find_key_value(key, file);
    if (val) parse_vec(val, (float*)&v, 2);
    return v;
}

VARSAPI vec3 vars_get_vec3(char* key, vars_file* file) {
    vec3 v = {0};
    const char* val = find_key_value(key, file);
    if (val) parse_vec(val, (float*)&v, 3);
    return v;
}

VARSAPI vec4 vars_get_vec4(char* key, vars_file* file) {
    vec4 v = {0};
    const char* val = find_key_value(key, file);
    if (val) parse_vec(val, (float*)&v, 4);
    return v;
}

VARSAPI int vars_free(vars_file file) {
    if (file.filebuf) free(file.filebuf);
    if (file.parsed_buf) free(file.parsed_buf);
    if (file.map) vars_map_free(file.map);
    free(file.map);
    return 0;
}

VARSAPI vars_file vars_load(const char* file_path) {
    vars_file file = {0};

    FILE* fp = fopen(file_path, "rb");
    if (!fp) exit(EXIT_FAILURE);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    file.filebuf = (char*)malloc(size);
    file.parsed_buf = (char*)malloc(size);
    fread(file.filebuf, 1, size, fp);
    fclose(fp);

    char* cursor = file.filebuf;
    char* end = file.filebuf + size;
    char* dst = file.parsed_buf;
    file.map = (vars_map*)malloc(sizeof(vars_map));
    vars_map_init(file.map, 128);

    char section[256] = "";

    while (cursor < end) {
        while (cursor < end && IS_WHITE_SPACE(*cursor)) cursor++;
        if (*cursor == '#') {
            while (cursor < end && !IS_END_OF_LINE(*cursor)) cursor++;
            continue;
        }

        if (*cursor == ':' && cursor[1] == '/') {
            cursor += 2;
            size_t i = 0;
            while (cursor < end && !IS_WHITE_SPACE(*cursor) && !IS_END_OF_LINE(*cursor) && i < sizeof(section) - 1) {
                section[i++] = *cursor++;
            }
            section[i] = '\0';
            while (cursor < end && IS_END_OF_LINE(*cursor)) cursor++;
            continue;
        }

        char* key = dst;
        while (cursor < end && (IS_LETTER(*cursor) || IS_NUMBER(*cursor) || IS_UNDERSCORE(*cursor))) {
            *dst++ = *cursor++;
        }
        *dst++ = '\0';

        while (cursor < end && IS_WHITE_SPACE(*cursor)) cursor++;

        char* value = dst;
        if (*cursor == '"') {
            *dst++ = *cursor++;
            while (cursor < end && *cursor != '"') *dst++ = *cursor++;
            if (*cursor == '"') *dst++ = *cursor++;
        } else {
            while (cursor < end && !IS_END_OF_LINE(*cursor) && *cursor != '#') *dst++ = *cursor++;
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

#endif // VARS_IMPLEMENTATION
