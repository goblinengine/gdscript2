# Typed Dictionary Schema Implementation Analysis for GDScript2

## Executive Summary

Your request to add "typed dictionary schemas" to GDScript is **technically feasible within gdscript2 module WITHOUT core changes**. However, it requires moderate architectural changes to the parser, analyzer, and type system.

## What You're Asking For

```gdscript
# Define a schema
var Monster[StringName, Variant] = {
    speed: int = 1.0,
    power: float = 1.0
}

# Use it as a type
var dragon : Monster = {
    power = 15.0,
}
```

This is conceptually similar to:
- **Structural typing** (shape-based type compatibility)
- **TypeScript-style type definitions**
- **Enum-like type aliases but for dictionaries**

## Current Type System Architecture

### 1. **How Enums Currently Work** (Your Reference Point)

Enums in GDScript are implemented as:

```cpp
// In gdscript_parser.h
struct EnumNode : public Node {
    struct Value {
        IdentifierNode *identifier = nullptr;
        ExpressionNode *custom_value = nullptr;
        int64_t value = 0;
        // ...
    };
    
    IdentifierNode *identifier = nullptr;
    Vector<Value> values;
    Variant dictionary;  // <-- Stores the enum as a read-only dictionary
};

// In gdscript_analyzer.h
class DataType {
    enum Kind {
        BUILTIN,
        NATIVE,
        SCRIPT,
        CLASS,
        ENUM,         // <-- Special type kind
        UNION,
        VARIANT,
        // ...
    };
    
    StringName enum_type;  // Name of the enum
    HashMap<StringName, int64_t> enum_values;  // Maps enum names to values
};
```

**Key insight**: Enums are **first-class types** in the DataType system, not just special dictionaries.

### 2. **Current Typed Dictionary Support**

GDScript already supports **typed dictionaries**:

```gdscript
var dict: Dictionary[String, int] = {}
var dict2: Dictionary[String, Vector2] = {}
```

This is implemented via:

```cpp
// In DataType
Vector<DataType> container_element_types;  // [0] = key type, [1] = value type

// Functions
bool has_container_element_type(int index);
bool has_container_element_types();
DataType get_container_element_type(int index);
DataType set_container_element_type(int index, DataType type);
```

**Limitation**: Only supports simple key/value typing, not structural schema validation.

## Proposed Implementation Path

### Option A: Dictionary Schema as New DataType.Kind ✅ RECOMMENDED

Create a new type similar to `ENUM`:

```cpp
// In gdscript_parser.h - Add new node
struct DictionarySchemaNode : public Node {
    IdentifierNode *identifier = nullptr;
    
    struct Field {
        IdentifierNode *name = nullptr;
        TypeNode *type_specifier = nullptr;
        ExpressionNode *default_value = nullptr;
        // Metadata
    };
    
    Vector<Field> fields;
    Variant schema_data;  // Metadata dictionary
};

// In DataType
enum Kind {
    // ... existing ...
    DICT_SCHEMA,  // NEW
};

// Add to DataType
struct DictionarySchema {
    StringName schema_name;
    HashMap<StringName, DataType> field_types;  // Field name -> expected type
    HashMap<StringName, Variant> default_values;
};
DictionarySchema *dict_schema = nullptr;
```

### Option B: Extend Enum System (More Hacky)

Reuse the `ENUM` kind with a flag:
- Create "typed enum-dictionaries" that validate structure
- Less clean architecturally

## Implementation Steps (Recommended: Option A)

### Step 1: Parser Changes

**File**: `gdscript_parser.h` and `gdscript_parser.cpp`

1. Add `DictionarySchemaNode` to the Node types
2. Add parsing for new syntax:
   ```gdscript
   var SchemaName[KeyType, ValueType] = {
       field1: type = default_value,
       field2: type = default_value,
   }
   ```
3. Update `parse_variable()` to detect and parse dictionary schemas
4. Store in ClassNode.members as a new member type

### Step 2: Analyzer Changes

**File**: `gdscript_analyzer.h` and `gdscript_analyzer.cpp`

1. Create schema resolution similar to enums:
   ```cpp
   void resolve_dict_schema(ClassNode::Member &member);
   GDScriptParser::DataType make_dict_schema_type(StringName name, ClassNode *p_class);
   ```

2. Update `resolve_datatype()` to recognize schema names as types:
   ```cpp
   // When resolving type "Monster", check if it's a dictionary schema
   if (script_class->has_dict_schema(first)) {
       result = make_dict_schema_type(first, script_class);
       type_found = true;
   }
   ```

3. Type checking for dictionary schema assignments:
   ```cpp
   // In reduce_dictionary() or validate_dict_schema_assignment()
   if (target_type.kind == DICT_SCHEMA) {
       // Validate that all keys in literal match schema
       // Check types of provided values
       // Allow partial/optional fields based on defaults
   }
   ```

### Step 3: Runtime Support

**No core changes needed** - Dictionary schemas are compile-time only! They:
- Validate at parse/analysis time
- Convert to regular `Dictionary` at runtime
- Store schema metadata in generated GDScript class

### Step 4: Bytecode Codegen

**File**: `gdscript_compiler.cpp` and `gdscript_byte_codegen.cpp`

1. When compiling a dictionary literal with schema type:
   - Emit validation bytecode (optional, for strict mode)
   - Or just emit normal dictionary construction (permissive mode)

2. Store schema metadata in compiled script for runtime reflection

## Detailed Changes Required

### Parser (gdscript_parser.cpp)

```cpp
// Add to ClassNode::Member::Type enum
enum Type {
    // ... existing ...
    DICT_SCHEMA,
};

// Update parse_class_variable() or similar
// When you see: var Monster[K, V] = { ... }
// 1. Detect the [K, V] syntax (new)
// 2. Check if RHS is dictionary literal
// 3. Create DictionarySchemaNode
// 4. Store in class members

GDScriptParser::DictionarySchemaNode* parse_dict_schema() {
    // Similar to parse_enum()
    // Parse fields with type annotations and defaults
}
```

### Analyzer (gdscript_analyzer.cpp)

Key modifications:

```cpp
// In resolve_datatype()
if (result.kind != FOUND && script_class->members_indices.has(first)) {
    ClassNode::Member member = script_class->get_member(first);
    if (member.type == ClassNode::Member::DICT_SCHEMA) {  // NEW
        result = make_dict_schema_type(first, script_class);
        type_found = true;
    }
}

// New function
void resolve_dict_schema(ClassNode::Member &p_member) {
    // Validate field types
    // Resolve defaults
    // Build schema metadata
}

// Update reduce_dictionary()
void reduce_dictionary(GDScriptParser::DictionaryNode *p_dict) {
    // ... existing code ...
    
    // NEW: Check if dict should match a schema
    if (p_dict->get_datatype().kind == DICT_SCHEMA) {
        validate_dict_against_schema(p_dict, p_dict->get_datatype());
    }
}

// New validation function
void validate_dict_against_schema(
    DictionaryNode *p_dict, 
    const DataType &p_schema_type
) {
    // For each field in schema:
    //   - Check it exists in literal (if not optional)
    //   - Check type of provided value matches
    //   - Warn about extra keys not in schema
    
    const DictionarySchema &schema = p_schema_type.dict_schema;
    for (auto &field : schema.field_types) {
        StringName field_name = field.key;
        DataType expected_type = field.value;
        
        // Find in dictionary literal
        bool found = false;
        for (auto &dict_element : p_dict->elements) {
            if (dict_element.key->is_constant) {
                String key = dict_element.key->reduced_value;
                if (key == field_name) {
                    // Type check the value
                    if (!is_type_compatible(expected_type, 
                                          dict_element.value->get_datatype())) {
                        push_error(...);
                    }
                    found = true;
                    break;
                }
            }
        }
        
        if (!found && !has_default(field_name)) {
            push_error("Missing required field: " + field_name);
        }
    }
}
```

## Important Considerations

### 1. **Core Changes: NONE REQUIRED** ✅

The `DataType` system in gdscript2 is flexible enough:
- Already supports custom `kind` values
- Already has `HashMap<StringName, T>` for metadata
- Parser and Analyzer are modular within the module

### 2. **Syntax Ambiguity to Solve**

```gdscript
# This looks like a subscript but isn't:
var Monster[StringName, Variant] = { ... }

# Current parser might confuse with:
var arr: Array[int] = []
var dict: Dictionary[String, int] = {}
```

**Solution**: 
- In `parse_variable()`, after `identifier`, check for `[`
- If followed by type list and `=`, it's a schema (not a type hint)
- If followed by nothing or `:`, treat normally

### 3. **Inheritance & Composition**

Not supported in initial version but can be added:
```gdscript
# Future enhancement
var PowerMonster extends Monster = {
    stamina: int = 100,
}
```

### 4. **Type Checking Modes**

**Strict Mode** (default):
- All schema fields must be present or have defaults
- No extra keys allowed

**Permissive Mode** (opt-in):
```gdscript
@allow_extra_keys
var schema = { ... }
```

### 5. **Optional Fields**

```gdscript
var Monster = {
    speed: int = 1.0,
    power: float = 1.0,
    # future: optional_field: int?
}
```

## Code Modification Checklist

```
gdscript_parser.h
  ├─ Add DictionarySchemaNode struct
  ├─ Add DICT_SCHEMA to ClassNode::Member::Type
  └─ Add parse_dict_schema() declaration

gdscript_parser.cpp
  ├─ Implement parse_dict_schema()
  ├─ Update parse_class_variable() to detect schema syntax
  └─ Handle field parsing with type specifiers

gdscript_analyzer.h
  ├─ Add DictionarySchema struct to DataType
  ├─ Add make_dict_schema_type() declaration
  └─ Add validate_dict_against_schema() declaration

gdscript_analyzer.cpp
  ├─ Implement resolve_dict_schema()
  ├─ Update resolve_datatype() for schema recognition
  ├─ Update reduce_dictionary() for schema validation
  ├─ Implement validate_dict_against_schema()
  └─ Handle type compatibility checking for schemas

gdscript_compiler.cpp (minor)
  └─ Compile dictionary schemas (mostly pass-through)

gdscript_byte_codegen.cpp (minor)
  └─ Bytecode generation (store metadata)
```

## Estimated Effort

- **Parser**: 200-400 lines (low complexity)
- **Analyzer**: 400-600 lines (moderate complexity)
- **Compiler**: 50-100 lines (minimal)
- **Tests**: 200-300 lines
- **Total**: ~1000-1500 lines of code

## Comparison with Alternatives

| Approach | Effort | Cleanliness | Extensibility |
|----------|--------|-------------|---------------|
| **Option A: New Kind** | Medium | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Option B: Enum Hack** | Low | ⭐⭐ | ⭐⭐ |
| **Option C: Runtime Only** | Medium | ⭐⭐⭐ | ⭐⭐⭐ |
| **Option D: Core Change** | High | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |

## Proof of Concept: Minimal Example

To test feasibility, start with:

1. **Parser only**: Can you tokenize `var Monster[K,V] = {}`?
2. **Analyzer only**: Can you store schema metadata?
3. **Type checking**: Can you validate one field?

Build incrementally rather than all-at-once.

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Type system complexity | Start simple: no inheritance, no unions |
| Performance | Schemas resolved once at compile-time |
| Backward compatibility | Feature gated or opt-in via annotation |
| Syntax conflicts | Use clear syntax that doesn't overlap |

## Conclusion

**This is absolutely implementable in gdscript2 without core changes.** The type system already has all the necessary infrastructure. You're essentially creating a new type kind similar to ENUM, but with field validation instead of value enumeration.

### Recommended First Step
1. Create a simple test case that should work
2. Implement schema parsing (tokens → AST)
3. Implement schema resolution (AST → type metadata)
4. Implement validation (type checking)
5. Iterate on edge cases

Would you like me to start implementing Option A with a minimal proof-of-concept?
