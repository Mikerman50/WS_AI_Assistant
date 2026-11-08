// Stand-in <stdint.h> for the syntax check: on the ESP32-S3 toolchain int32_t is
// `long` and uint32_t is `unsigned long` (since ESP-IDF 5.0), so printing them
// with %d or %u is a -Werror=format build failure there but not on a PC. These
// typedefs make the host compiler flag the same mistakes.
#ifndef EINK_XTENSA_STDINT_H
#define EINK_XTENSA_STDINT_H
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef long int32_t;
typedef unsigned long uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef int8_t int_least8_t;
typedef uint8_t uint_least8_t;
typedef int16_t int_least16_t;
typedef uint16_t uint_least16_t;
typedef int32_t int_least32_t;
typedef uint32_t uint_least32_t;
typedef int64_t int_least64_t;
typedef uint64_t uint_least64_t;
typedef int int_fast8_t;
typedef unsigned uint_fast8_t;
typedef int int_fast16_t;
typedef unsigned uint_fast16_t;
typedef int32_t int_fast32_t;
typedef uint32_t uint_fast32_t;
typedef int64_t int_fast64_t;
typedef uint64_t uint_fast64_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef long long intmax_t;
typedef unsigned long long uintmax_t;
#define INT8_MIN (-128)
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN (-32768)
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT32_MIN (-2147483647L - 1)
#define INT32_MAX 2147483647L
#define UINT32_MAX 4294967295UL
#define INT64_MIN (-9223372036854775807LL - 1)
#define INT64_MAX 9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL
#define INTPTR_MAX __INTPTR_MAX__
#define UINTPTR_MAX __UINTPTR_MAX__
#define SIZE_MAX __SIZE_MAX__
#define INT8_C(c) c
#define UINT8_C(c) c
#define INT16_C(c) c
#define UINT16_C(c) c
#define INT32_C(c) c##L
#define UINT32_C(c) c##UL
#define INT64_C(c) c##LL
#define UINT64_C(c) c##ULL
#endif
