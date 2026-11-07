/* SPDX-License-Identifier: CC0-1.0 */
/**
 * ============================================================================
 * NIST FIPS-197 AES-128 Reference Standard & Test Vectors
 * ============================================================================
 *
 * Source: National Institute of Standards and Technology (NIST)
 * Publication: Federal Information Processing Standards Publication 197 (FIPS-197)
 * URL: https://csrc.nist.gov/publications/detail/fips/197/final
 *
 * This reference file documents the exact mathematical specification and
 * official test vectors utilized to build and verify OpenFTHD's clean-room
 * AES-128 hardware/firmware cryptographic offload engine.
 *
 * Test Vector: FIPS-197 Appendix C.1 (AES-128 Example)
 * ----------------------------------------------------
 * Key:        2b 7e 15 16 28 ae d2 a6 ab f7 15 88 09 cf 4f 3c
 * Plaintext:  32 43 f6 a8 88 5a 30 8d 31 31 98 a2 e0 37 07 34
 * Ciphertext: 39 25 84 1d 02 dc 09 fb dc 11 85 97 19 6a 0b 32
 */

#include <stdint.h>
#include <stddef.h>

/* NIST FIPS-197 Official Test Vectors */
const uint8_t NIST_FIPS197_KEY[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

const uint8_t NIST_FIPS197_PLAINTEXT[16] = {
    0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
    0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34
};

const uint8_t NIST_FIPS197_CIPHERTEXT[16] = {
    0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb,
    0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32
};

/* Round Constants (Rcon) according to FIPS-197 Section 5.2 */
const uint32_t NIST_RCON[10] = {
    0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000,
    0x1b000000, 0x36000000
};
