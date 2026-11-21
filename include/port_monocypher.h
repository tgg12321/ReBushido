#ifndef PORT_MONOCYPHER_H
#define PORT_MONOCYPHER_H

/* Monocypher 4.0.2 (src/port/vendor/monocypher/, CC0 or BSD-2, as
 * shipped), in every program of the port: the updater's check of a
 * release's signature (src/port/window/update_core.c) and the ranked
 * ladder's identities and digests (src/port/ranked.c; docs/NETPLAY.md,
 * section 17).
 *
 * The port's programs have no C library, so its two files are compiled
 * with what they take from <stdint.h> and <stddef.h> given here
 * (PORT_MONOCYPHER_BUILD, tools/port_build.py, VENDOR_FILES) and the
 * port's empty headers of src/port/vendor/libc/; the port's own files see
 * the few functions they call, declared with the port's types: u8 is
 * uint8_t and PortSize is size_t, so the two declarations agree. */

typedef __SIZE_TYPE__ PortSize;

#ifdef PORT_MONOCYPHER_BUILD

typedef __UINT8_TYPE__ uint8_t;
typedef __INT8_TYPE__ int8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT16_TYPE__ int16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT32_TYPE__ int32_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __INT64_TYPE__ int64_t;
typedef __SIZE_TYPE__ size_t;
#ifndef NULL
#define NULL ((void *)0)
#endif

#else

#include "common.h"

#define PORT_ED25519_SECRET_BYTES 64
#define PORT_ED25519_PUBLIC_BYTES 32
#define PORT_ED25519_SEED_BYTES 32
#define PORT_ED25519_SIGNATURE_BYTES 64

/* A key pair made of a seed of 32 random bytes, which it wipes. */
extern void crypto_ed25519_key_pair(
    u8 secret_key[PORT_ED25519_SECRET_BYTES],
    u8 public_key[PORT_ED25519_PUBLIC_BYTES],
    u8 seed[PORT_ED25519_SEED_BYTES]);
/* An Ed25519 signature (RFC 8032) of `message`. */
extern void crypto_ed25519_sign(
    u8 signature[PORT_ED25519_SIGNATURE_BYTES],
    const u8 secret_key[PORT_ED25519_SECRET_BYTES], const u8 *message,
    PortSize message_size);
/* BLAKE2b (RFC 7693) of `message`, `hash_size` bytes of it (1 to 64). */
extern void crypto_blake2b(
    u8 *hash, PortSize hash_size, const u8 *message, PortSize message_size);
/* Bytes of a secret cleared where the compiler cannot leave them. */
extern void crypto_wipe(void *secret, PortSize size);

#if !defined(__x86_64__)
/* What a 32-bit compiler calls for Monocypher's one `%` of two 64-bit
 * numbers (its Argon2, which the port never calls): the port's own, as
 * the program links no compiler's library (src/port/api.c). */
extern u64 __umoddi3(u64 dividend, u64 divisor);
#endif

#endif

#endif /* PORT_MONOCYPHER_H */
