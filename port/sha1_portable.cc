// Portions copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// This module provides a slow but portable implementation of
// the SHA1 hash function.
//
// It is adapted from free code written by Paul E. Jones
// <paulej@packetizer.com>.  See http://www.packetizer.com/security/sha1/
//
// The license for the original code is:
/*
  Copyright (C) 1998, 2009
  Paul E. Jones <paulej@packetizer.com>

  Freeware Public License (FPL)

  This software is licensed as "freeware."  Permission to distribute
  this software in source and binary forms, including incorporation
  into other products, is hereby granted without a fee.  THIS SOFTWARE
  IS PROVIDED 'AS IS' AND WITHOUT ANY EXPRESSED OR IMPLIED WARRANTIES,
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
  AND FITNESS FOR A PARTICULAR PURPOSE.  THE AUTHOR SHALL NOT BE HELD
  LIABLE FOR ANY DAMAGES RESULTING FROM THE USE OF THIS SOFTWARE, EITHER
  DIRECTLY OR INDIRECTLY, INCLUDING, BUT NOT LIMITED TO, LOSS OF DATA
  OR DATA BEING RENDERED INACCURATE.
*/

#include "port/sha1_portable.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

namespace leveldb {
namespace port {

/*
 *  Description:
 *      This class implements the Secure Hashing Standard as defined
 *      in FIPS PUB 180-1 published April 17, 1995.
 */

/*
 *  This structure will hold context information for the hashing
 *  operation
 */
typedef struct SHA1Context {
  unsigned Message_Digest[5]; /* Message Digest (output)          */

  unsigned Length_Low;        /* Message length in bits           */
  unsigned Length_High;       /* Message length in bits           */

  unsigned char Message_Block[64]; /* 512-bit message blocks      */
  int Message_Block_Index;    /* Index into message block array   */

  bool Computed;               /* Is the digest computed?          */
  bool Corrupted;              /* Is the message digest corruped?  */
} SHA1Context;

/*
 *  Portability Issues:
 *      SHA-1 is defined in terms of 32-bit "words".  This code was
 *      written with the expectation that the processor has at least
 *      a 32-bit machine word size.  If the machine word size is larger,
 *      the code should still function properly.  One caveat to that
 *      is that the input functions taking characters and character
 *      arrays assume that only 8 bits of information are stored in each
 *      character.
 */

/*
 *  Define the circular shift macro
 */
#define SHA1CircularShift(bits,word) \
                ((((word) << (bits)) & 0xFFFFFFFF) | \
                ((word) >> (32-(bits))))

/* Function prototypes */
static void SHA1ProcessMessageBlock(SHA1Context *);
static void SHA1PadMessage(SHA1Context *);

// Initialize the SHA1Context in preparation for computing a new
// message digest.
static void SHA1Reset(SHA1Context* context) {
  context->Length_Low             = 0;
  context->Length_High            = 0;
  context->Message_Block_Index    = 0;

  context->Message_Digest[0]      = 0x67452301;
  context->Message_Digest[1]      = 0xEFCDAB89;
  context->Message_Digest[2]      = 0x98BADCFE;
  context->Message_Digest[3]      = 0x10325476;
  context->Message_Digest[4]      = 0xC3D2E1F0;

  context->Computed   = false;
  context->Corrupted  = false;
}

// This function will return the 160-bit message digest into the
// Message_Digest array within the SHA1Context provided
static bool SHA1Result(SHA1Context *context) {
  if (context->Corrupted) {
    return false;
  }

  if (!context->Computed) {
    SHA1PadMessage(context);
    context->Computed = true;
  }
  return true;
}

// This function accepts an array of bytes as the next portion of
// the message.
static void SHA1Input(SHA1Context         *context,
                      const unsigned char *message_array,
                      unsigned            length) {
  if (!length) return;

  if (context->Computed || context->Corrupted) {
    context->Corrupted = true;
    return;
  }

  while(length-- && !context->Corrupted) {
    context->Message_Block[context->Message_Block_Index++] =
        (*message_array & 0xFF);

    context->Length_Low += 8;
    /* Force it to 32 bits */
    context->Length_Low &= 0xFFFFFFFF;
    if (context->Length_Low == 0) {
      context->Length_High++;
      /* Force it to 32 bits */
      context->Length_High &= 0xFFFFFFFF;
      if (context->Length_High == 0)
      {
        /* Message is too long */
        context->Corrupted = true;
      }
    }

    if (context->Message_Block_Index == 64)
    {
      SHA1ProcessMessageBlock(context);
    }

    message_array++;
  }
}

// This function will process the next 512 bits of the message stored
// in the Message_Block array.
static void SHA1ProcessMessageBlock(SHA1Context *context) {
  const unsigned K[] =            // Constants defined in SHA-1
      {
        0x5A827999,
        0x6ED9EBA1,
        0x8F1BBCDC,
        0xCA62C1D6
      };
  int         t;                  // Loop counter
  unsigned    temp;               // Temporary word value
  unsigned    W[80];            // Word sequence
  unsigned    A, B, C, D, E;    // Word buffers

  // Initialize the first 16 words in the array W
  for(t = 0; t < 16; t++) {
    W[t] = ((unsigned) context->Message_Block[t * 4]) << 24;
    W[t] |= ((unsigned) context->Message_Block[t * 4 + 1]) << 16;
    W[t] |= ((unsigned) context->Message_Block[t * 4 + 2]) << 8;
    W[t] |= ((unsigned) context->Message_Block[t * 4 + 3]);
  }

  for(t = 16; t < 80; t++) {
    W[t] = SHA1CircularShift(1,W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16]);
  }

  A = context->Message_Digest[0];
  B = context->Message_Digest[1];
  C = context->Message_Digest[2];
  D = context->Message_Digest[3];
  E = context->Message_Digest[4];

  for(t = 0; t < 20; t++) {
    temp =  SHA1CircularShift(5,A) +
        ((B & C) | ((~B) & D)) + E + W[t] + K[0];
    temp &= 0xFFFFFFFF;
    E = D;
    D = C;
    C = SHA1CircularShift(30,B);
    B = A;
    A = temp;
  }

  for(t = 20; t < 40; t++) {
    temp = SHA1CircularShift(5,A) + (B ^ C ^ D) + E + W[t] + K[1];
    temp &= 0xFFFFFFFF;
    E = D;
    D = C;
    C = SHA1CircularShift(30,B);
    B = A;
    A = temp;
  }

  for(t = 40; t < 60; t++) {
    temp = SHA1CircularShift(5,A) +
        ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
    temp &= 0xFFFFFFFF;
    E = D;
    D = C;
    C = SHA1CircularShift(30,B);
    B = A;
    A = temp;
  }

  for(t = 60; t < 80; t++) {
    temp = SHA1CircularShift(5,A) + (B ^ C ^ D) + E + W[t] + K[3];
    temp &= 0xFFFFFFFF;
    E = D;
    D = C;
    C = SHA1CircularShift(30,B);
    B = A;
    A = temp;
  }

  context->Message_Digest[0] = (context->Message_Digest[0] + A) & 0xFFFFFFFF;
  context->Message_Digest[1] = (context->Message_Digest[1] + B) & 0xFFFFFFFF;
  context->Message_Digest[2] = (context->Message_Digest[2] + C) & 0xFFFFFFFF;
  context->Message_Digest[3] = (context->Message_Digest[3] + D) & 0xFFFFFFFF;
  context->Message_Digest[4] = (context->Message_Digest[4] + E) & 0xFFFFFFFF;

  context->Message_Block_Index = 0;
}

// According to the standard, the message must be padded to an even
// 512 bits.  The first padding bit must be a '1'.  The last 64 bits
// represent the length of the original message.  All bits in between
// should be 0.  This function will pad the message according to those
// rules by filling the Message_Block array accordingly.  It will also
// call SHA1ProcessMessageBlock() appropriately.  When it returns, it
// can be assumed that the message digest has been computed.
static void SHA1PadMessage(SHA1Context *context) {
  // Check to see if the current message block is too small to hold
  // the initial padding bits and length.  If so, we will pad the
  // block, process it, and then continue padding into a second block.
  if (context->Message_Block_Index > 55) {
    context->Message_Block[context->Message_Block_Index++] = 0x80;
    while(context->Message_Block_Index < 64) {
      context->Message_Block[context->Message_Block_Index++] = 0;
    }

    SHA1ProcessMessageBlock(context);

    while(context->Message_Block_Index < 56) {
      context->Message_Block[context->Message_Block_Index++] = 0;
    }
  } else {
    context->Message_Block[context->Message_Block_Index++] = 0x80;
    while(context->Message_Block_Index < 56) {
      context->Message_Block[context->Message_Block_Index++] = 0;
    }
  }

  // Store the message length as the last 8 octets
  context->Message_Block[56] = (context->Length_High >> 24) & 0xFF;
  context->Message_Block[57] = (context->Length_High >> 16) & 0xFF;
  context->Message_Block[58] = (context->Length_High >> 8) & 0xFF;
  context->Message_Block[59] = (context->Length_High) & 0xFF;
  context->Message_Block[60] = (context->Length_Low >> 24) & 0xFF;
  context->Message_Block[61] = (context->Length_Low >> 16) & 0xFF;
  context->Message_Block[62] = (context->Length_Low >> 8) & 0xFF;
  context->Message_Block[63] = (context->Length_Low) & 0xFF;

  SHA1ProcessMessageBlock(context);
}


void SHA1_Hash_Portable(const char* data, size_t len, char* hash_array) {
  SHA1Context context;
  SHA1Reset(&context);
  SHA1Input(&context, reinterpret_cast<const unsigned char*>(data), len);
  bool ok = SHA1Result(&context);
  if (!ok) {
    fprintf(stderr, "Unexpected error in SHA1_Hash_Portable code\n");
    exit(1);
  }
  for (int i = 0; i < 5; i++) {
    uint32_t value = context.Message_Digest[i];
    hash_array[i*4 + 0] = (value >> 24) & 0xff;
    hash_array[i*4 + 1] = (value >> 16) & 0xff;
    hash_array[i*4 + 2] = (value >> 8) & 0xff;
    hash_array[i*4 + 3] = value & 0xff;
  }
}

}
}
