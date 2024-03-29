# IndiaNavi C coding style

For low-level styling (spaces, parentheses, brace placement, etc), all code should follow the format specified in `.clang-format` in the project root.

**Important: Make sure you use `clang-format` version 14 or later!**

### Names

Use snake\_case (all lowercase, with underscores separating words) for struct, variable and function names.
Typedef structs get an additional `_t` at the end.

###### Right:

```c
typedef struct component_t;
size_t buffer_size;
void run_path();
```

###### Wrong:

```c
typedef struct component;
size_t bufferSize;
void RunPath();
```

Use full words, except in the rare case where an abbreviation would be more canonical and easier to understand.

###### Right:

```c
size_t character_size;
size_t length;
short tab_index; // More canonical.
```

###### Wrong:

```c
size_t char_size;
size_t len;
short tabulation_index; // Goofy.
```

Use descriptive verbs in function names.

###### Right:

```c
void convert_to_ascii(char*, size_t);
```

###### Wrong:

```c
void to_ascii(char*, size_t);
```

Prefix functions with module name.

###### Right:

```c
map_t* map_create(...);
void map_update_coordinates(map_t* map, map_position_t* pos)
void label_fit_to_text(label_t * label);
```

###### Wrong:

```c
map_t* create_label(...);
void update_map_coordinats(map_t* m, map_position_t* p);
void fit_text_to_label(label_t *l);
```