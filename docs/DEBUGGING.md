# Debugging Guide

This guide covers debugging techniques for Open-Lotto development and troubleshooting.

## GDB Debugging Workflow

### Build with Debug Symbols

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Debug builds disable optimizations and include full symbol information for accurate debugging.

### Starting GDB

```bash
# Run executable under GDB
gdb ./build/open-lotto

# Or attach to running process
gdb ./build/open-lotto <PID>
```

### Essential GDB Commands

#### Breakpoints

```gdb
# Set breakpoint at function
break combogen_draw

# Set breakpoint at file:line
break src/combogen.c:42

# Set breakpoint with condition
break src/combogen.c:42 if main_count > 7

# List all breakpoints
info breakpoints

# Delete breakpoint
delete 1

# Disable/enable breakpoint
disable 1
enable 1
```

#### Execution Control

```gdb
# Run until breakpoint
run --game lotto --draws 5

# Continue execution
continue

# Step into function
step

# Step over function
next

# Step until return
finish

# Run to next source line
line
```

#### Inspection

```gdb
# Print variable value
print variable_name
print &struct_ptr->field

# Print array contents
print array@10        # First 10 elements

# Dereference pointer
print *ptr_to_struct

# Inspect struct fields
print struct_instance

# Watch variable changes
watch variable_name
```

#### Call Stack

```gdb
# Show stack trace
backtrace
backtrace full       # With local variables

# Move to frame
frame 0
frame 1

# Print frame info
info frame
```

### Example Debugging Session

Debugging a crash in `combogen_draw()`:

```gdb
$ gdb ./build/open-lotto
(gdb) break combogen_draw
(gdb) run --game lotto --draws 1
Breakpoint 1, combogen_draw(...) at src/combogen.c:...

(gdb) print main_count
$1 = 6

(gdb) print main_range
$2 = 49

(gdb) step
# ... step through algorithm

(gdb) backtrace
#0 combogen_draw (...) at src/combogen.c:...
#1 0x... in main () at src/main.c:...

(gdb) quit
```

## AddressSanitizer (ASan)

AddressSanitizer detects memory errors at runtime.

### Build with ASan

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
cmake --build build-asan
```

### Run with ASan

```bash
./build-asan/open-lotto --game lotto --draws 5
```

If a memory error is found, ASan prints detailed diagnostic output.

### Common ASan Errors

#### Heap Buffer Overflow

**Error Message:**
```
ERROR: AddressSanitizer: heap-buffer-overflow on unknown address at pc 0x... (T=0 [0]; X [1]; T [2])
```

**Causes:**
- Accessing array beyond bounds
- Writing past end of allocated buffer

**Example:**
```c
int arr[5];
arr[10] = 42;  // Buffer overflow
```

**Fix:** Check array bounds before access.

#### Use After Free

**Error Message:**
```
ERROR: AddressSanitizer: attempting to free address which was not malloc'd
```

**Causes:**
- Freeing same pointer twice
- Accessing freed memory

**Example:**
```c
int* ptr = malloc(10);
free(ptr);
free(ptr);  // Double free
```

**Fix:** Use pointer guards (`if (ptr) free(ptr); ptr = NULL;`).

#### Memory Leak

**Error Message:**
```
Direct leak of N bytes in M objects allocated from:
    #0 ... malloc ...
```

**Causes:**
- Allocating memory without freeing
- Lost reference to allocated block

**Example:**
```c
int* ptr = malloc(100);
return;  // Leak: forgot to free
```

**Fix:** Free all allocations before returning.

#### Uninitialized Variable

**Error Message:**
```
Use of uninitialized value
```

**Causes:**
- Reading variable before assignment
- Using uninitialized struct fields

**Example:**
```c
int x;
if (x > 5) { }  // x uninitialized
```

**Fix:** Initialize all variables at declaration.

### Interpreting AddressSanitizer Output

ASan output includes:
1. **Error type** (e.g., heap-buffer-overflow)
2. **Location** (file:line where error occurred)
3. **Backtrace** (call stack leading to error)
4. **Memory map** (allocated regions)

**Example:**
```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on unknown address 0x60200...
    at pc 0x... T=0; [0],X [1]; T [2] in unknown module
READ of size 4 at 0x602000xxx thread T0
    #0 0x... in combogen_draw src/combogen.c:42:10
    #1 0x... in main src/main.c:100:5
```

To fix:
1. Open `src/combogen.c:42`
2. Check array access at that line
3. Verify bounds

## Valgrind Memcheck

Valgrind's memcheck tool finds memory errors more comprehensively than ASan.

### Run Memcheck

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

valgrind --leak-check=full --show-leak-kinds=all \
  ./build/open-lotto --game lotto --draws 10 --validate-only
```

### Valgrind Output

```
==12345== HEAP SUMMARY:
==12345==     in use at exit: 0 bytes in 0 blocks
==12345==   total heap alloc'd: 1,234 bytes in 100 blocks
==12345==   total heap freed: 1,234 bytes in 100 blocks
==12345==   total reachable: 0 bytes in 0 blocks
==12345== 
==12345== ERROR SUMMARY: 0 errors from 0 contexts
```

**Meaning:**
- `in use at exit` — Memory still allocated (leak)
- `ERROR SUMMARY` — 0 errors = clean run

### Memory Leak Example

If Valgrind reports a leak:

```
==12345== 100 bytes in 1 blocks are definitely lost in loss record 1 of 2
==12345==    at 0x... in malloc
==12345==    by 0x... in plugin_loader_load src/plugin_loader.c:50
==12345==    by 0x... in main src/main.c:100
```

**Fix in code:**
```c
// In plugin_loader.c, ensure cleanup
dlclose(handle);  // Added
```

## Massif (Heap Profiler)

Massif profiles heap memory usage over time.

### Run Massif

```bash
valgrind --tool=massif --massif-out-file=massif.out \
  ./build/open-lotto --game lotto --draws 1000
```

### Analyze Results

```bash
ms_print massif.out
```

**Output Example:**
```
      KB
     1000|          #
      750|        ##
      500|      ####
      250|    ######
        0|################## main
           |← malloc call site
```

**Interpreting:**
- Peak memory usage
- When allocations occur
- Which functions allocate most

## Callgrind (Call Graph Profiler)

Detailed analysis of which functions consume CPU.

### Run Callgrind

```bash
valgrind --tool=callgrind \
  ./build/open-lotto --game lotto --draws 10000 --validate-only

kcachegrind callgrind.out.12345  # GUI analysis (optional)
```

### Command-Line Analysis

```bash
# Count calls to specific function
cg_annotate callgrind.out.12345 src/combogen.c
```

**Output:**
```
Ir         File:Function
100,000    src/combogen.c:combogen_draw
  50,000   src/combogen.c:... (inline)
```

## Clang Sanitizers

Compile with multiple sanitizers for broader error detection:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined,signed-integer-overflow"
cmake --build build

./build/open-lotto --game lotto --draws 5
```

### Common Undefined Behavior

#### Integer Overflow

```c
uint32_t a = UINT32_MAX;
a = a + 1;  // Undefined for signed; wraps for unsigned
```

#### Signed Integer Overflow

```c
int a = INT_MAX;
a = a + 1;  // Undefined behavior in C
```

**Fix:** Use unsigned types or checked arithmetic.

#### Null Pointer Dereference

```c
int* ptr = NULL;
*ptr = 42;  // Undefined
```

**Fix:** Always check pointers before use.

## GDB with Sanitizers

Combine GDB and ASan for interactive debugging:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build

gdb ./build/open-lotto
(gdb) run --game lotto --draws 5
# ASan error occurs; GDB catches it
(gdb) backtrace
(gdb) frame 0
(gdb) print variable_name
```

## Static Analysis with Clang-Tidy

Clang-tidy performs static analysis without running code:

```bash
./scripts/run_clang_tidy.sh
```

**Output Example:**
```
src/combogen.c:42:5: warning: unused variable 'x' [clang-analyzer-deadcode.DeadStores]
    int x = 5;
```

**Interpret:**
- Line 42 in combogen.c has unused variable
- Category: dead store (assignment never used)

## Static Analysis with Cppcheck

Cppcheck finds common C/C++ errors:

```bash
./scripts/run_cppcheck.sh
```

**Output Example:**
```
[src/combogen.c:42]: (warning) Variable 'x' is assigned a value that is never used.
```

## Debugging GUIs

### SDL2 GUI Debugging

Add debug output to `src/gui_sdl.c`:

```c
log_debug("Ball %d: pos=(%f,%f,%f) vel=(%f,%f,%f)",
          i, ball->x, ball->y, ball->z, ball->vx, ball->vy, ball->vz);
```

Run with debug logging:

```bash
./build/open-lotto --game lotto --gui 2d --log debug 2>&1 | tee debug.log
```

### OpenGL GUI Debugging

Enable OpenGL debug output (if supported):

```c
glEnable(GL_DEBUG_OUTPUT);
glDebugMessageCallback(debug_callback, NULL);
```

Or use RenderDoc (external tool):
```bash
renderdoc ./build/open-lotto --game lotto --gui 3d
```

## Debugging Physics Simulation

The 3D physics engine in `src/gui_opengl.c` has complex interactions. Debug it step-by-step:

```c
// Add to update_drum_instance()
log_debug("Phase: %d, Elapsed: %f", phase, elapsed_time);
log_debug("Ball 0: pos=(%f,%f,%f) vel=(%f,%f,%f)",
          balls[0].x, balls[0].y, balls[0].z,
          balls[0].vx, balls[0].vy, balls[0].vz);
```

### Physics Visualization

Plot ball trajectory:

```bash
# Capture positions to file
./build/open-lotto --game lotto --log debug 2>&1 | \
  grep "Ball 0" > trajectory.txt

# Analyze with Python/matplotlib
python3 <<'EOF'
import matplotlib.pyplot as plt
# Parse trajectory.txt and plot
EOF
```

## Debugging Plugin Loading

If plugins fail to load:

```bash
# Check for symbols
nm ./build/plugins/lotto.so | grep game

# Test dlopen manually
gdb ./build/open-lotto
(gdb) break plugin_loader_load
(gdb) run --game lotto
(gdb) print dlerror()

# Or use ldd to check dependencies
ldd ./build/plugins/lotto.so
```

## Core Dump Analysis

When a process crashes, generate a core dump:

```bash
ulimit -c unlimited  # Enable core dumps
./build/open-lotto --game lotto --draws 5

# Analyze with GDB
gdb ./build/open-lotto ./core

(gdb) backtrace  # Where did it crash?
(gdb) frame 0
(gdb) info locals  # Local variables at crash
```

## Debugging Checklist

- [ ] Build with `-DCMAKE_BUILD_TYPE=Debug`
- [ ] Enable ASan: `-fsanitize=address,undefined`
- [ ] Run with GDB if needed: `gdb ./executable`
- [ ] Use `log_debug()` liberally
- [ ] Profile with `perf` or Valgrind
- [ ] Check for undefined behavior with clang sanitizers
- [ ] Verify no memory leaks with Valgrind memcheck

## Performance Debugging

See [PERFORMANCE_TUNING.md](PERFORMANCE_TUNING.md) for profiling with:
- `perf stat` — CPU counters
- `perf record` — Sampling profiler
- `valgrind --tool=massif` — Memory profiling
- `valgrind --tool=callgrind` — Call graph

## Common Issues

### GDB "Reading symbols" Hangs

**Cause:** Debug symbols too large

**Solution:**
```bash
# Use objcopy to strip and re-add symbols (advanced)
# Or compile with -gdwarf-2 instead of -g
cmake -DCMAKE_C_FLAGS="-gdwarf-2" ...
```

### Segmentation Fault with No Backtrace

**Cause:** Signal handler interfered or stack corrupted

**Solution:**
```bash
# Run with core dump
ulimit -c unlimited
./program
gdb ./program ./core
backtrace
```

### AddressSanitizer Noise

**Cause:** Third-party libraries with issues

**Solution:**
```bash
# Suppress specific errors
ASAN_OPTIONS="suppressions=my_suppressions.txt" ./program
```

Create `my_suppressions.txt`:
```
leak:external_library.so
```

## References

- **GDB Manual:** https://sourceware.org/gdb/documentation/
- **Valgrind User Manual:** https://valgrind.org/docs/manual/
- **AddressSanitizer:** https://github.com/google/sanitizers
- **Clang Static Analyzer:** https://clang-analyzer.llvm.org/
