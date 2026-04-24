#ifndef KLIB_MACROS_H
#define KLIB_MACROS_H

// Lightweight stubs for compatibility. Extend as needed.
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#endif // KLIB_MACROS_H
