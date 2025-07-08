# vars

A simple key-value pair file format, with support for comments.
There are two implementations: one that uses a hashmap, and one that doesn't.
The difference is that the hashmap implementation uses a bit more memory and takes a little longer
to load the files initially since it has to cache everything.
But it's a LOT faster than the no-hashmap implementation
when fetching a value from a file.
## Format

```
# A valid key can contain: letters, hyphens and underscores. Other kinds of characters are not permitted to be used in keys.
# A value can be of type: int, float, bool, string, vec2, vec3, and vec4.
# The function that gets a bool returns an int for the purpose of returning -1 on failure.
# The key-value pairs must be separated by at least one space. strings and vectors work without a space, but that's more of a bug than a feature.

# This is a comment.

name "joe mama" # string

is_fullscreen true # bool

player_count 4  # int

three-halfs 1.5 # float

pos (1.123 1.5) # vec2

size (1.5 1.5 1.5) # vec3

:/game # Yes, we even support subfolders!
rotation (1.5 1.5 1.5 1.5) # vec4
```

## Usage
```C
#define VARS_IMPLEMENTATION
#define VARS_DO_NOT_PREFIX_TYPES // this removes the vars_ prefix from the vector types. Not recommended if you want to use a math library in your project.
#include "vars_hashmap.h"
#include <stdint.h>

int main(void) {
    vars_file vars = vars_load("example.vars"); // Your file name

    char name_buffer[256];
    vars_get_string("name", &vars, name_buffer);
    printf("name: %s\n", name_buffer);
    uint8_t is_fullscreen = vars_get_bool("is_fullscreen", &vars);
    printf("is_fullscreen: %d\n", is_fullscreen);

    int player_count = vars_get_int("player_count", &vars);
    printf("player_count: %d\n", player_count);


    float three-halfs = vars_get_float("three_halfs", &vars);
    printf("three_halfs (float): %f\n", three_halfs);

    vec2 pos = vars_get_vec2("pos", &vars);
    printf("pos (vars_vec2): %f, %f\n", pos.x, pos.y);

    vars_vec3 size = vars_get_vec3("size", &vars);
    printf("size (vars_vec3): %f, %f, %f\n", size.x, size.y, size.z);

    vars_vec4 rotation = vars_get_vec4("game/rotation", &vars); // this variable is under the "game" subfolder in the file.
    printf("rotation (vars_vec4): %f, %f, %f, %f\n", rotation.x, rotation.y, rotation.z, rotation.w);

    vars_free(vars);
    return 0;
}
```
