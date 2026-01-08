# B-tree Implementation for RP6502

A balanced B-tree implementation optimized for the RP6502 platform with cc65 compiler.

## Files Added

- **[btree.h](btree.h)** - B-tree header file with API declarations
- **[btree.c](btree.c)** - Complete B-tree implementation
- **[main.c](main.c)** - Updated with comprehensive sample code

## Key Features

### B-tree Specifications
- **Order**: 3 (max 2 keys per node, max 3 children)
- **Key Type**: unsigned char (0-255)
- **Value Type**: int (16-bit signed)
- **Optimized**: For cc65 constraints (c89 syntax, 16-bit int, 256-byte stack)

### Supported Operations

#### Insert
```c
btree_insert(tree, key, value);
```
- Inserts or updates a key-value pair
- Handles node splitting when nodes become full
- Time complexity: O(log n)

#### Get
```c
int value = btree_get(tree, key);
```
- Retrieves value for a given key
- Returns 0x8000 (-32768) if key not found
- Time complexity: O(log n)

#### Update
```c
unsigned char success = btree_update(tree, key, new_value);
```
- Updates existing key's value
- Returns 1 if successful, 0 if key not found
- Time complexity: O(log n)

#### Delete
```c
unsigned char success = btree_delete(tree, key);
```
- Removes a key from the tree
- Handles node merging to maintain balance
- Returns 1 if successful, 0 if key not found
- Time complexity: O(log n)

#### Utility Functions
- `btree_create()` - Creates new empty tree
- `btree_print(tree)` - Prints tree structure for debugging
- `btree_free(tree)` - Frees all allocated memory

## Sample Usage

The main.c file demonstrates all operations:

1. **Create** - Initialize an empty B-tree
2. **Insert** - Add multiple key-value pairs
3. **Get** - Retrieve values
4. **Update** - Modify existing values
5. **Delete** - Remove keys
6. **Verify** - Check final state

## Implementation Notes

### Design Decisions
- **Minimal node size** for resource-constrained environment
- **Simpler order-3 tree** instead of larger orders to reduce stack usage
- **Borrow/merge operations** for deletion to maintain tree balance
- **cc89 style code** with all variable declarations at block start

### Memory Constraints
- Local stack limited to 256 bytes on RP6502
- All recursive functions kept minimal
- Node structures sized appropriately for limited heap

### Platform Compatibility
- Uses cc65 compiler standards
- No floating-point operations
- No 64-bit integers
- Compatible with 16-bit int arithmetic
- Proper handling of malloc/free for heap management

## Building

```bash
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../tools/rp6502.cmake ..
make
```

The compiled binary is `build/hello.rp6502`

## Testing the Implementation

The sample code in main.c provides comprehensive testing including:
- Multiple insertions with automatic tree rebalancing
- Key searches in populated tree
- Value updates on existing keys
- Deletions with tree restructuring
- Final state verification
