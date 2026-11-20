#!/usr/bin/env python3
"""Ed25519 (RFC 8032) in Python alone, for the release's signature
(tools/release_sign.py; docs/PORT.md, "Updates").

    import ed25519
    public = ed25519.public_key(secret)          # 32 bytes of each
    signature = ed25519.sign(secret, message)    # 64 bytes
    ed25519.verify(public, message, signature)   # True or False

The arithmetic of the RFC, written for being read, not for speed: a
signature takes a few hundredths of a second, which is nothing beside the
one signature a release has. Its secrets are not kept from a clock or a
cache measuring this process: run it only on the owner's own PC, where the
key is. The program checks the signature with Monocypher
(src/port/vendor/monocypher/; src/port/window/update_core.c); `--self-test`
holds this to the RFC's own cases.
"""
import hashlib
import sys

P = 2 ** 255 - 19
L = 2 ** 252 + 27742317777372353535851937790883648493
D = (-121665 * pow(121666, P - 2, P)) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _inverse(x):
    return pow(x, P - 2, P)


def _recover_x(y, sign):
    """The x of the point with this y, of this parity; None if none."""
    if y >= P:
        return None
    x2 = (y * y - 1) * _inverse(D * y * y + 1) % P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P != 0:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P != 0:
        return None
    if x & 1 != sign:
        x = P - x
    return x


# points in extended coordinates (X, Y, Z, T), x = X/Z, y = Y/Z, xy = T/Z
_BY = 4 * _inverse(5) % P
_BX = _recover_x(_BY, 0)
BASE = (_BX, _BY, 1, _BX * _BY % P)
IDENTITY = (0, 1, 1, 0)


def _add(p, q):
    a = (p[1] - p[0]) * (q[1] - q[0]) % P
    b = (p[1] + p[0]) * (q[1] + q[0]) % P
    c = 2 * p[3] * q[3] * D % P
    d = 2 * p[2] * q[2] % P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _multiply(scalar, point):
    result = IDENTITY
    while scalar > 0:
        if scalar & 1:
            result = _add(result, point)
        point = _add(point, point)
        scalar >>= 1
    return result


def _equal(p, q):
    return (p[0] * q[2] - q[0] * p[2]) % P == 0 and (p[1] * q[2] - q[1] * p[2]) % P == 0


def _compress(point):
    z = _inverse(point[2])
    x, y = point[0] * z % P, point[1] * z % P
    return int.to_bytes(y | ((x & 1) << 255), 32, 'little')


def _decompress(data):
    if len(data) != 32:
        return None
    y = int.from_bytes(data, 'little')
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return (x, y, 1, x * y % P)


def _hash_int(*parts):
    return int.from_bytes(hashlib.sha512(b''.join(parts)).digest(), 'little')


def _expand(secret):
    if len(secret) != 32:
        raise ValueError('an Ed25519 secret key is 32 bytes')
    h = hashlib.sha512(secret).digest()
    a = int.from_bytes(h[:32], 'little')
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(secret):
    a, _ = _expand(secret)
    return _compress(_multiply(a, BASE))


def sign(secret, message):
    a, prefix = _expand(secret)
    public = _compress(_multiply(a, BASE))
    r = _hash_int(prefix, message) % L
    big_r = _compress(_multiply(r, BASE))
    k = _hash_int(big_r, public, message) % L
    s = (r + k * a) % L
    return big_r + int.to_bytes(s, 32, 'little')


def verify(public, message, signature):
    if len(public) != 32 or len(signature) != 64:
        return False
    a = _decompress(public)
    big_r = _decompress(signature[:32])
    if a is None or big_r is None:
        return False
    s = int.from_bytes(signature[32:], 'little')
    if s >= L:
        return False
    k = _hash_int(signature[:32], public, message) % L
    return _equal(_multiply(s, BASE), _add(big_r, _multiply(k, a)))


# RFC 8032, 7.1: tests 1, 2, 3 and SHA(abc)
RFC_CASES = [
    ('9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60',
     'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a', '',
     'e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b'),
    ('4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb',
     '3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c', '72',
     '92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00'),
    ('c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7',
     'fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025', 'af82',
     '6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a'),
    ('833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42',
     'ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf',
     'ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f',
     'dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704'),
]


def self_test():
    failures = []
    for secret, public, message, signature in RFC_CASES:
        secret, public = bytes.fromhex(secret), bytes.fromhex(public)
        message, signature = bytes.fromhex(message), bytes.fromhex(signature)
        if public_key(secret) != public:
            failures.append('the public key of %s' % secret.hex()[:16])
        if sign(secret, message) != signature:
            failures.append('the signature of %s' % secret.hex()[:16])
        if not verify(public, message, signature):
            failures.append('the signature of %s is refused' % secret.hex()[:16])
        bad = bytearray(signature)
        bad[5] ^= 0x10
        if verify(public, message, bytes(bad)):
            failures.append('a changed signature of %s passes' % secret.hex()[:16])
        if verify(public, message + b'x', signature):
            failures.append('a changed message of %s passes' % secret.hex()[:16])
    return failures


if __name__ == '__main__':
    if '--self-test' in sys.argv[1:]:
        failed = self_test()
        for failure in failed:
            print('FAILED: ' + failure)
        print('ed25519 self-test: %s' % ('FAILED' if failed else 'OK'))
        sys.exit(1 if failed else 0)
    print(__doc__)
