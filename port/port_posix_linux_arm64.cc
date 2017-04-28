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
#define HWCAP_PMULL    (1 << 4)

#define KBYTES 1032
#define SEGMENTBYTES 256

// compute 8bytes for each segment parallelly
#define CRC32C32BYTES(P, IND) do {\
	crc1 = __crc32cd(crc1, *((const uint64_t *)(P) + (SEGMENTBYTES/8)*1 + (IND)));\
	crc2 = __crc32cd(crc2, *((const uint64_t *)(P) + (SEGMENTBYTES/8)*2 + (IND)));\
	crc3 = __crc32cd(crc3, *((const uint64_t *)(P) + (SEGMENTBYTES/8)*3 + (IND)));\
	crc0 = __crc32cd(crc0, *((const uint64_t *)(P) + (SEGMENTBYTES/8)*0 + (IND)));\
	} while(0);

// compute 8*8 bytes for each segment parallelly
#define CRC32C256BYTES(P, IND) do {\
	CRC32C32BYTES((P), (IND)*8+0) \
	CRC32C32BYTES((P), (IND)*8+1) \
	CRC32C32BYTES((P), (IND)*8+2) \
	CRC32C32BYTES((P), (IND)*8+3) \
	CRC32C32BYTES((P), (IND)*8+4) \
	CRC32C32BYTES((P), (IND)*8+5) \
	CRC32C32BYTES((P), (IND)*8+6) \
	CRC32C32BYTES((P), (IND)*8+7) \
	} while(0);

// compute 4*8*8 bytes for each segment parallelly
#define CRC32C1024BYTES(P) do {\
	CRC32C256BYTES((P), 0) \
	CRC32C256BYTES((P), 1) \
	CRC32C256BYTES((P), 2) \
	CRC32C256BYTES((P), 3) \
	(P) += 4*SEGMENTBYTES; \
	} while(0)

#endif // defined(LEVELDB_PLATFORM_POSIX_ARMV8_CRC_CRYPTO)

namespace leveldb {
namespace port {

static inline bool CanAccelerateCRC32C() {
    unsigned long hwcap = getauxval(AT_HWCAP);
    if ((hwcap & HWCAP_CRC32) && (hwcap & HWCAP_PMULL)) {
        return true;
    }
    return false;
}

uint32_t AcceleratedCRC32C(uint32_t crc, const char* buf, size_t size) {
#if !defined(LEVELDB_PLATFORM_POSIX_ARMV8_CRC_CRYPTO)
    return 0;
#else
    static bool can = CanAccelerateCRC32C();
    if(!can) {
        return 0;
    }

    int64_t length = size;
    uint32_t crc0, crc1, crc2, crc3;
    uint64_t t0, t1, t2;

    // k0=CRC(x^(3*SEGMENTBYTES*8)), k1=CRC(x^(2*SEGMENTBYTES*8)), k2=CRC(x^(SEGMENTBYTES*8))
    const poly64_t k0 = 0x8d96551c, k1 = 0xbd6f81f8, k2 = 0xdcb17aa4;

    crc = crc ^ 0xffffffffu;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(buf);

    while ( length >= KBYTES) {
        crc0 = crc;
        crc1 = 0;
        crc2 = 0;
        crc3 = 0;

        // compute 1024bytes parallelly
        CRC32C1024BYTES(p);

        // merge crc0 crc1 crc2 crc3
        t2 = (uint64_t)vmull_p64(crc2, k2);
        t1 = (uint64_t)vmull_p64(crc1, k1);
        t0 = (uint64_t)vmull_p64(crc0, k0);
        crc = __crc32cd(crc3, *(uint64_t *)p);
        p += sizeof(uint64_t);
        crc ^= __crc32cd(0, t2);
        crc ^= __crc32cd(0, t1);
        crc ^= __crc32cd(0, t0);

        length -= KBYTES;
    }

    while(length >= sizeof(uint64_t)) {
        crc = __crc32cd(crc, *(uint64_t *)p);
        p += sizeof(uint64_t);
        length -= sizeof(uint64_t);
    }

    if(length & sizeof(uint32_t)) {
        crc = __crc32cw(crc, *(uint32_t *)p);
        p += sizeof(uint32_t);
    }

    if(length & sizeof(uint16_t)) {
        crc = __crc32ch(crc, *(uint16_t *)p);
        p += sizeof(uint16_t);
    }

    if(length & sizeof(uint8_t)) {
        crc = __crc32cb(crc, *p);
    }

    return crc ^ 0xffffffffu;

#endif // defined(LEVELDB_PLATFORM_POSIX_ARMV8_CRC_CRYPTO)
}

} // namespace port
} // namespace leveldb
