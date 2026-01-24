# Example Code Review

**Code to Review:**
```cpp
void processArray(int* arr, int size) {
    for (int i = 0; i <= size; i++) {
        std::cout << arr[i] << std::endl;
    }
}
```

## Review Findings

### 1. Correctness
- **Issue**: Off-by-one error in the loop condition (`i <= size`).
- **Fix**: Change to `i < size`. Accessing `arr[size]` is out of bounds.

### 2. Edge Cases
- **Issue**: No check for `nullptr` or zero `size`.
- **Fix**: Add `if (arr == nullptr || size <= 0) return;`.

### 3. Style
- **Recommendation**: Use `std::span` (C++20) or `std::vector` instead of raw pointers and size for better safety and modern style.

### 4. Performance
- **Optimization**: `std::endl` flushes the stream. If this is a hot loop, use `\n` instead of `std::endl` to avoid unnecessary flushes.

## Summary
The code has a critical buffer overflow bug. Please fix the loop boundary and add null checks.
