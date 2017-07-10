// Copyright 2017 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// A portable implementation of crc32c, optimized to handle
// up to eight bytes at a time.
//
// In a separate source file to allow this accelerated CRC32C function to be
// compiled with the appropriate compiler flags to enable aarch64 crc32
// instructions.

#include <stdint.h>
#include <string.h>
#include "port/port.h"


#if defined(LEVELDB_PLATFORM_POSIX_ARMV8_CRC_CRYPTO)

#include <arm_acle.h>
#include <arm_neon.h>
#include <sys/auxv.h>

// see kernel file 'arch/arm64/include/uapi/asm/hwcap.h'
#define HWCAP_CRC32    (1 << 7)

#define CRC32CX(crc, value) crc = __crc32cd((crc), (value))
#define CRC32CW(crc, value) crc = __crc32cw((crc), (value))
#define CRC32CH(crc, value) crc = __crc32ch((crc), (value))
#define CRC32CB(crc, value) crc = __crc32cb((crc), (value))

// split 1KB into segments as below
// -----------------------------------------------------------------
// |8bytes |   336bytes   |    336bytes    |    336bytes   | 8bytes|
// -----------------------------------------------------------------
#define KBYTES 1024
#define SEGMENTBYTES 336

// compute 8bytes for each segment parallelly
// process crc0 last to avoid dependency with crc32 above
#define CRC32C3X8(BUF, ITR) \
	crc1 = __crc32cd(crc1, *((const uint64_t *)(BUF) + (SEGMENTBYTES/8)*1 + (ITR)));\
	crc2 = __crc32cd(crc2, *((const uint64_t *)(BUF) + (SEGMENTBYTES/8)*2 + (ITR)));\
	crc0 = __crc32cd(crc0, *((const uint64_t *)(BUF) + (SEGMENTBYTES/8)*0 + (ITR)));

// compute 7*8 bytes for each segment parallelly
#define CRC32C7X3X8(BUF, ITR) \
	CRC32C3X8((BUF), (ITR)*7+0) \
	CRC32C3X8((BUF), (ITR)*7+1) \
	CRC32C3X8((BUF), (ITR)*7+2) \
	CRC32C3X8((BUF), (ITR)*7+3) \
	CRC32C3X8((BUF), (ITR)*7+4) \
	CRC32C3X8((BUF), (ITR)*7+5) \
	CRC32C3X8((BUF), (ITR)*7+6)

// compute 6*7*8 bytes for each segment parallelly
#define CRC326X7X3X8(BUF) do {\
	CRC32C7X3X8((BUF), 0) \
	CRC32C7X3X8((BUF), 1) \
	CRC32C7X3X8((BUF), 2) \
	CRC32C7X3X8((BUF), 3) \
	CRC32C7X3X8((BUF), 4) \
	CRC32C7X3X8((BUF), 5) \
	(BUF) += 3*SEGMENTBYTES; \
	} while(0)

#endif // defined(LEVELDB_PLATFORM_POSIX_ARMV8_CRC_CRYPTO)

namespace leveldb {
namespace port {

static inline bool HaveCRC32() {
    unsigned long hwcap = getauxval(AT_HWCAP);
    if (hwcap & HWCAP_CRC32) {
        return true;
    }
    return false;
}

uint32_t AcceleratedCRC32C(uint32_t crc, const char* buf, size_t size) {
#if !defined(LEVELDB_PLATFORM_POSIX_ARMV8_CRC_CRYPTO)
    return 0;
#else
    static bool have = HaveCRC32();
    if(!have) {
        return 0;
    }

    int64_t length = size;
    uint32_t crc0, crc1, crc2;
    uint64_t t0, t1;

    // k1=CRC(x^(2*SEGMENTBYTES*8)), k2=CRC(x^(SEGMENTBYTES*8))
    const poly64_t k1 = 0xe417f38a, k2 = 0x8f158014;

    crc = crc ^ 0xffffffffu;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(buf);

    while ( length >= KBYTES) {
        // do first 8 bytes here for better pipelining
        crc0 = __crc32cd(crc, *(uint64_t *)p);
        crc1 = 0;
        crc2 = 0;
        p += sizeof(uint64_t);

        // process block inline
        CRC326X7X3X8(p);

        // merge crc0 crc1 crc2
        // CRCM(x)=(CRC(C).K2)+(CRC(B).K1)+(CRC(A))
        t1 = (uint64_t)vmull_p64(crc1, k2);
        t0 = (uint64_t)vmull_p64(crc0, k1);
        crc = __crc32cd(crc2, *(uint64_t *)p);
        p += sizeof(uint64_t);
        crc ^= __crc32cd(0, t1);
        crc ^= __crc32cd(0, t0);

        length -= KBYTES;
    }

    while(length >= sizeof(uint64_t)) {
        CRC32CX(crc, *(uint64_t *)p);
        p += sizeof(uint64_t);
        length -= sizeof(uint64_t);
    }

    if(length & sizeof(uint32_t)) {
        CRC32CW(crc, *(uint32_t *)p);
        p += sizeof(uint32_t);
    }

    if(length & sizeof(uint16_t)) {
        CRC32CH(crc, *(uint16_t *)p);
        p += sizeof(uint16_t);
    }

    if(length & sizeof(uint8_t)) {
        CRC32CB(crc, *p);
    }

    return crc ^ 0xffffffffu;

#endif // defined(LEVELDB_PLATFORM_POSIX_ARMV8_CRC_CRYPTO)
}

} // namespace port
} // namespace leveldb
