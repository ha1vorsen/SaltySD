#ifndef SALTYSD_SE_PUBLIC_TYPES_H
#define SALTYSD_SE_PUBLIC_TYPES_H

#include <stdint.h>

typedef uint8_t se_u8;
typedef uint16_t se_u16;
typedef uint32_t se_u32;
typedef uint64_t se_u64;
typedef int32_t se_s32;

#define SE_INDEX_NONE 0xFFFFFFFFu

#if defined(__GNUC__) && defined(__arm__)
#define SE_CALL __attribute__((pcs("aapcs")))
#else
#define SE_CALL
#endif

#endif
