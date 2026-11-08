// Stand-in <inttypes.h> matching ./stdint.h (see the note there).
#ifndef EINK_XTENSA_INTTYPES_H
#define EINK_XTENSA_INTTYPES_H
#include <stdint.h>
#define PRId8 "d"
#define PRIu8 "u"
#define PRId16 "d"
#define PRIu16 "u"
#define PRId32 "ld"
#define PRIi32 "li"
#define PRIu32 "lu"
#define PRIx32 "lx"
#define PRIX32 "lX"
#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIx64 "llx"
#endif
