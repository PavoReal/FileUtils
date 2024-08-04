#include <Windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <immintrin.h>

#define KILOBYTES(v) ((v) * 1024)
#define MEGABYTES(v) (KILOBYTES(v) * 1024)
#define GIGABYTES(v) (MEGABYTES(v) * 1024)

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef float  f32;
typedef double f64;

#define SZ_USE_X86_AVX2 (1)

#pragma warning(push)
#pragma warning(disable: 4068)
#pragma warning(disable: 4146)
#include "common/stringzilla.h"
#pragma warning(pop)

#include "common/memory.c"
#include "common/timer.c"
#include "common/file.c"
#include "common/hash.c"

