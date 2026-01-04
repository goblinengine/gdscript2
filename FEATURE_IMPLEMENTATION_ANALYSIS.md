# GDScript2 Feature Implementation Difficulty Analysis

## Overview
This document analyzes the feasibility and difficulty of implementing various features in GDScript2. Most features would require modifications to the **parser**, **analyzer**, and **compiler** layers. The GDScript2 module is largely self-contained and doesn't require core Godot changes for most features.

---

## Architecture Summary

GDScript2 compilation pipeline:
1. **Tokenizer** (`gdscript_tokenizer.h/.cpp`) - converts source code to tokens
2. **Parser** (`gdscript_parser.h/.cpp`) - builds Abstract Syntax Tree (AST)
3. **Analyzer** (`gdscript_analyzer.h/.cpp`) - type checking, semantic analysis, constant folding
4. **Compiler** (`gdscript_compiler.h/.cpp`) - generates bytecode from AST
5. **VM** (`gdscript_vm.h/.cpp`) - executes bytecode

Key design principle: **Single-token lookahead** in parser ensures simplicity and prevents over-complexity.

---

## Feature Analysis

### 1. **Final Keyword Inlining**
**Difficulty: MEDIUM-EASY** (within gdscript2 module)

**Current Status:** `FunctionNode` has `is_abstract` flag but no `is_final` flag

**Implementation Steps:**
1. Add `bool is_final` to `FunctionNode` struct in parser
2. Modify tokenizer to recognize `final` keyword
3. Update parser to set `is_final = true` when parsing function
4. In compiler (`gdscript_byte_codegen.h`), check `is_final` flag
5. Generate inline bytecode directly instead of function call instruction
6. Analyzer validates that final functions aren't overridden in child classes

**Inline Strategy:**
- Copy function bytecode at call site
- Adjust stack offsets for nested calls
- Update jump targets in inlined code

**Estimated LOC:** 150-300 lines

---

### 2. **C-Interoperability: Why Direct C Header Import Isn't Useful**

**Why it's not feasible/useful:**

- **Type System Mismatch**: C headers contain preprocessor directives, macros, and pointer types that don't map to GDScript's type system
  - Example: `#define SIZE 10` is compile-time, GDScript needs runtime values
  - C pointers (`void*`, `int*`) have no direct GDScript equivalent

- **Memory Model Incompatibility**: 
  - GDScript uses garbage collection; C uses manual memory management
  - Struct layout/packing differs between platforms
  - No way to know field offsets at parse time

- **ABI Complexity**: 
  - Calling conventions vary by platform (x86-64, ARM, etc.)
  - Function signatures require FFI bindings

- **Current Solution - GDExtension**: 
  - Write C/C++ bindings that expose functions as GDScript-callable
  - Godot's `ClassDB` registration handles type mapping
  - This is the intended pattern

---

### 3. **Project-Wide Search-and-Replace (Smart) Limitation**
**Why it's a real limitation:**

The lack of scope-aware replacement is because:
- GDScript is dynamically typed (gradual typing)
- Names can be shadowed at multiple scopes
- Static analysis can't reliably determine ALL occurrences of a symbol
- Would require full program analysis (expensive)

**Example of why it's hard:**
```gdscript
var x = 5  # Module-level x
func test():
    var x = 10  # Local x shadows module x
    use_x(x)  # Which x?
```

**To implement smart replacement, you'd need:**
- Full symbol resolution (scope analysis)
- Type inference engine running continuously
- Potentially multiple replacements per symbol
- Manual verification UI
- Performance impact on editor

**Verdict:** Not worth implementing; focus on IDE language server for "rename symbol" feature instead.

---

### 4. **Namespace Support: MyGame.UI.Button**
**Difficulty: HARD** (~1500-2500 LOC)

**Why it's complex:**
- Requires changes to how class names are stored (`fqcn` exists but not for symbols)
- Must modify symbol resolution throughout analyzer
- Affects import system
- Breaking change to existing code

**Current Architecture:**
- `ClassNode.fqcn` stores fully-qualified class name (path-based, not namespace-based)
- Classes are resolved via file paths: `res://ui/button.gd`

**Implementation Strategy:**

1. **Tokenizer**: Recognize `::`operator or `.` for namespaces
2. **Parser**: Build namespace hierarchy during class parsing
   - Track namespace prefix for each class
   - Store in `ClassNode` struct: `Vector<StringName> namespace`
3. **Analyzer**: Update `resolve_identifier()` to search namespace hierarchy
   - Implement namespace lookup rules (local → parent → global)
4. **Class Registration**: Register with namespace-prefixed name in engine
5. **Imports**: Support `using MyGame.UI` syntax

**Challenges:**
- Backward compatibility (current code is namespace-less)
- How to handle implicit namespaces vs explicit?
- Interaction with file-based modules

**Note on Code Regions:** 
- Code regions (collapsable sections) are purely UI-level (editor feature)
- Already implemented in editor, not language feature
- Different from true namespaces (no scoping effect)

**Verdict:** Worth implementing but requires careful design to avoid breaking changes.

---

### 5. **Private/Protected Keywords**
**Difficulty: EASY-MEDIUM** (~400-600 LOC)

**Current Status:** Convention-based (underscore prefix = private)

**Implementation:**

1. **Tokenizer**: Add `private`, `protected` keywords
2. **Parser**: Add to `ClassNode::Member`
   ```cpp
   enum AccessLevel { PUBLIC, PRIVATE, PROTECTED };
   AccessLevel access_level = PUBLIC;
   ```
3. **Analyzer**: 
   - Check access violations in `reduce_member_access()`
   - Allow access if: same class, subclass (for protected), or member is public
4. **Compiler**: No changes needed (access checking is analyzer job)

**Example:**
```gdscript
class_name Player

private var health = 100  # Can't access from outside

protected func take_damage():  # Accessible in subclasses only
    health -= 10
```

**Warnings/Errors:** Generate errors at compile-time, not runtime

**Estimated LOC:** 400-600 lines

---

### 6. **Named Arguments**
**Difficulty: MEDIUM** (~800-1200 LOC)

**Current Architecture:**
- `CallNode.arguments` is `Vector<ExpressionNode*>` (positional only)

**Implementation:**

1. **Tokenizer**: No new tokens (uses existing `=`)
2. **Parser**: Extend `CallNode`
   ```cpp
   struct CallArgument {
       StringName name;  // nullptr for positional
       ExpressionNode* value;
   };
   Vector<CallArgument> arguments;  // Replace current Vector
   ```
3. **Analyzer**: In `reduce_call()`
   - Match named args to function parameters by name
   - Validate all required parameters are provided
   - Allow positional + named (positional must come first)
   - Reorder arguments to match function signature
4. **Compiler**: Generate code in parameter order (analyzer already reordered)

**Example:**
```gdscript
func spawn(name: String, hp: int, damage: int):
    pass

spawn("Goblin", hp=100, damage=15)  # Valid
spawn(name="Goblin", hp=100, damage=15)  # Also valid
spawn(hp=100, name="Goblin", damage=15)  # Valid (reordered internally)
```

**Backward Compatibility:** ✅ Positional calls still work

**Estimated LOC:** 800-1200 lines

---

### 7. **List Comprehensions**
**Difficulty: MEDIUM-HARD** (~1000-1500 LOC)

**Example:**
```gdscript
var evens = [x for x in range(10) if x % 2 == 0]
```

**Implementation:**

1. **Parser**: Create `ComprehensionNode` (subclass of `ExpressionNode`)
   ```cpp
   struct ComprehensionNode : public ExpressionNode {
       ExpressionNode* element;      # x
       IdentifierNode* iterator;     # x
       ExpressionNode* iterable;     # range(10)
       ExpressionNode* condition;    # x % 2 == 0 (optional)
   };
   ```
2. **Analyzer**: `reduce_comprehension()`
   - Create temporary scope for iterator variable
   - Reduce element expression in that scope
   - Infer result type based on element type
3. **Compiler**: Convert to bytecode equivalent of:
   ```gdscript
   var result = []
   for x in range(10):
       if x % 2 == 0:
           result.append(x)
   ```

**Challenges:**
- Dict comprehensions: `{k: v for k, v in items}`
- Set comprehensions (if sets existed)
- Nested comprehensions
- Type inference for heterogeneous lists

**Estimated LOC:** 1000-1500 lines

---

### 8. **Destructuring: var [x, y] = get_vector()**
**Difficulty: MEDIUM** (~700-1100 LOC)

**Example:**
```gdscript
var [x, y] = [10, 20]
var [a, [b, c]] = [1, [2, 3]]  # Nested destructuring
var [first, ..., last] = [1, 2, 3, 4, 5]  # Rest pattern
```

**Implementation:**

1. **Parser**: Extend `VariableNode`
   ```cpp
   struct VariableNode {
       // Current: IdentifierNode* identifier
       // New:
       ExpressionNode* destructure_pattern;  # [x, y]
   };
   ```
   Create `PatternNode` variants (already exists for match!)
   - Use existing `PatternNode::PT_ARRAY`, `PT_REST`, etc.

2. **Analyzer**: 
   - Already has pattern analysis for match statements!
   - Extend to work on variable initialization
   - `resolve_destructuring_pattern()` (similar to match patterns)

3. **Compiler**:
   - Unpack array/dict into individual variables
   - Generate temporary indices

**Good News:** Pattern infrastructure already exists (for `match`), just needs reuse!

**Estimated LOC:** 700-1100 lines (most code already exists)

---

### 9. **Comparisons in Match Statements**
**Difficulty: EASY-MEDIUM** (~400-600 LOC)

**Current Status:** Match only supports literal/bind/array/dict patterns

**Feature Desired:**
```gdscript
match value:
    < 10:
        print("less than 10")
    > 100:
        print("greater than 100")
    !null:
        print("not null")
    _:
        print("default")
```

**Implementation:**

1. **Parser**: Extend `PatternNode`
   ```cpp
   enum Type {
       PT_LITERAL,
       PT_EXPRESSION,
       PT_BIND,
       PT_ARRAY,
       PT_DICTIONARY,
       PT_REST,
       PT_WILDCARD,
       PT_COMPARISON,  # NEW
   };
   
   struct ComparisonPattern {
       BinaryOpNode::OpType op;  # <, >, <=, >=, !=, ==
       ExpressionNode* value;
   };
   ```

2. **Analyzer**: Update `reduce_match_pattern()`
   - Reduce comparison expression
   - Type-check that operands are comparable

3. **Compiler**: Generate comparison bytecode in pattern matching logic
   ```cpp
   // For < 10:
   if (value < 10) { ... }
   ```

**Estimated LOC:** 400-600 lines

---

### 10. **Fallthrough in Match (with continue)**
**Difficulty: EASY-MEDIUM** (~300-500 LOC)

**Feature Desired:**
```gdscript
match value:
    1:
        print("one or two")
        continue  # Fall through to next branch
    2:
        print("also two")
    3:
        print("three")
```

**Current Status:** Fallthrough was removed (by design choice)

**Implementation:**

1. **Tokenizer**: `continue` already recognized (used in loops)
2. **Parser**: Allow `continue` in match branches
   ```cpp
   struct MatchBranchNode {
       bool has_continue = false;  # NEW
   };
   ```
3. **Analyzer**: Validate `continue` only in match context
4. **Compiler**: Don't jump to end-of-match after branch with `continue`
   ```cpp
   if (branch_matches && !has_continue) {
       jump_to_end;
   }
   ```

**Why it was removed:** Confusing with loop `continue`; too complex

**Estimated LOC:** 300-500 lines

---

### 11. **Typing for Dictionary Properties**
**Difficulty: MEDIUM** (~600-1000 LOC)

**Current Status:** Dicts are untyped (`Variant`)

**Feature Desired:**
```gdscript
var config: Dict[String, int] = {
    "health": 100,
    "damage": 15
}

var mixed: Dict[String, Variant] = {
    "name": "Player",
    "level": 5
}
```

**Implementation:**

1. **Parser**: Extend `TypeNode` to support generics
   ```cpp
   struct TypeNode {
       StringName type_name;
       Vector<TypeNode*> generic_args;  # For Dict[String, int]
   };
   ```

2. **Analyzer**: 
   - Resolve generic type arguments in `resolve_datatype()`
   - Validate dictionary literals match schema
   - Track in `DataType.container_element_types` (already used for arrays!)

3. **Compiler**:
   - No changes (validation happens in analyzer)
   - Type info used for optimization

**Current Support:** Arrays already support typed elements!
```gdscript
var numbers: Array[int] = [1, 2, 3]  # Works
```

**Challenge:** Dicts have key AND value types to track

**Estimated LOC:** 600-1000 lines

---

### 12. **Tuple Support: (1, "health", true)**
**Difficulty: HARD** (~1200-2000 LOC)

**Example:**
```gdscript
var player_data: (String, int, bool) = ("Archer", 85, true)
var name = player_data.0  # Access by index
var (n, hp, alive) = player_data  # Destructure

func get_position() -> (float, float):
    return (10.5, 20.3)
```

**Why it's hard:**
- Tuples are NOT arrays (fixed size, mixed types)
- Need dedicated `TupleNode` in parser
- Must extend type system with tuple types
- Interaction with destructuring
- Syntax ambiguity (parentheses used for grouping elsewhere)

**Implementation:**

1. **Parser**: Detect tuple syntax
   - `(expr1, expr2)` vs `(expr)` (grouping)
   - Heuristic: More than one comma = tuple
   - `TupleNode` extends `ExpressionNode`

2. **Analyzer**: Create `DataType::TUPLE`
   ```cpp
   struct DataType {
       Vector<DataType> tuple_types;  # Type of each element
   };
   ```

3. **Compiler**: Tuples compile to arrays (engine doesn't have tuple type)
   - Trade-off: lose type safety at element access

4. **Destructuring**: Integrate with existing pattern system

**Challenges:**
- Syntax ambiguity with function argument lists
- Return type syntax: `-> (int, String)` vs `-> int`
- `tuple.0` syntax (currently invalid)

**Verdict:** Useful but high implementation cost. Array of typed elements may be sufficient alternative.

**Estimated LOC:** 1200-2000 lines

---

### 13. **Structs: Stack-Allocated Value Types**
**Difficulty: VERY HARD** (~3000-5000+ LOC, requires core changes)

**Example:**
```gdscript
struct Vec3:
    var x: float
    var y: float
    var z: float

func test():
    var v: Vec3 = {x: 1, y: 2, z: 3}  # Stack allocated
    var v2 = v  # Copy by value, not reference
```

**Why it's extremely complex:**

1. **Memory Model**: GDScript uses reference semantics for classes
   - Structs need value semantics (copy-on-assign)
   - Requires changes to VM (memory management)
   - Interacts with Godot's object system

2. **Type System**: 
   - Structs ≠ Classes
   - Can't inherit from classes
   - Different layout than objects
   - Must add `STRUCT` kind to `DataType`

3. **Compiler Changes**:
   - Stack allocation instead of heap
   - Value copy operations
   - Field access bytecode differs
   - Method calls on stack values

4. **Core Integration**:
   - Engine may not recognize struct types
   - Serialization/debugging impacts
   - GDExtension compatibility

**Implementation Requires:**

- Major parser changes (struct syntax)
- New compiler backend (stack vs heap)
- VM memory management refactor
- Possibly core Godot changes

**Verdict:** NOT recommended for GDScript alone. Better as separate feature with full design.

**Estimated LOC:** 3000-5000+ (partially core)

---

## Summary Table

| Feature | Difficulty | Module-Only | Estimated LOC | Priority |
|---------|-----------|------------|---------------|----------|
| Final keyword inlining | Medium-Easy | ✅ | 150-300 | ⭐⭐⭐ |
| Named arguments | Medium | ✅ | 800-1200 | ⭐⭐⭐⭐ |
| Comparisons in match | Easy-Medium | ✅ | 400-600 | ⭐⭐⭐ |
| Fallthrough in match | Easy-Medium | ✅ | 300-500 | ⭐⭐ |
| Private/Protected keywords | Easy-Medium | ✅ | 400-600 | ⭐⭐⭐⭐⭐ |
| Destructuring | Medium | ✅ | 700-1100 | ⭐⭐⭐ |
| List comprehensions | Medium-Hard | ✅ | 1000-1500 | ⭐⭐⭐ |
| Dict typing | Medium | ✅ | 600-1000 | ⭐⭐⭐ |
| Namespaces | Hard | ✅ | 1500-2500 | ⭐⭐ |
| Tuple support | Hard | ✅ | 1200-2000 | ⭐⭐ |
| Structs | Very Hard | ❌ | 3000-5000+ | ⭐ |
| C interop | N/A | N/A | Use GDExtension | N/A |
| Smart search/replace | N/A | N/A | Use LSP | N/A |

---

## Recommended Implementation Order

**Phase 1 (High ROI, Easy):**
1. Private/Protected keywords
2. Final keyword inlining
3. Comparisons in match

**Phase 2 (Very Useful):**
4. Named arguments
5. Destructuring
6. Dict typing

**Phase 3 (Nice to Have):**
7. Fallthrough in match
8. List comprehensions

**Phase 4 (Major Features):**
9. Namespaces
10. Tuple support

**Not Recommended:**
- Structs (use classes with value-by-copy semantics)
- C interop (use GDExtension)
- Smart search/replace (use LSP in editor)

---

## Code Locations to Modify

For most features, you'll need to modify:

1. **gdscript_tokenizer.h/.cpp** - Add keywords
2. **gdscript_parser.h/.cpp** - Add AST nodes and parsing logic
3. **gdscript_analyzer.h/.cpp** - Type checking and semantic analysis
4. **gdscript_compiler.h/.cpp** and **gdscript_byte_codegen.h/.cpp** - Code generation
5. **gdscript_vm.h/.cpp** - Only for significant runtime changes (unlikely needed)

---

## Key Insights

✅ **Most features are implementable within gdscript2 module alone**
✅ **Parser already uses single-token lookahead, limiting complexity**
✅ **Pattern matching infrastructure can be reused (destructuring, comprehensions)**
✅ **Type system is flexible enough for most features**
❌ **Struct value semantics requires VM-level changes**
❌ **Some features (smart replace, C interop) are better solved differently**

