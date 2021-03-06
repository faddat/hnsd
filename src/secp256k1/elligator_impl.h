/*
 * elligator_impl.h - elligator module for libsecp256k1
 * Copyright (c) 2019, Christopher Jeffrey (MIT License).
 * https://github.com/bcoin-org/bcrypto
 *
 * Parts of this software are based on ElementsProject/secp256k1-zkp:
 *   Copyright (c) 2013, Pieter Wuille
 *   https://github.com/ElementsProject/secp256k1-zkp
 *
 * This module implements the Elligator Squared protocol for secp256k1.
 *
 * See: Elligator Squared.
 *   Mehdi Tibouchi.
 *   Algorithm 1, Page 8, Section 3.3.
 *   https://eprint.iacr.org/2014/043.pdf
 *
 * Also: Indifferentiable Hashing to Barreto-Naehrig Curves.
 *   Pierre-Alain Fouque, Mehdi Tibouchi.
 *   Page 8, Section 3.
 *   Page 15, Section 6, Algorithm 1.
 *   https://www.di.ens.fr/~fouque/pub/latincrypt12.pdf
 */

#ifndef HSK_SECP256K1_MODULE_ELLIGATOR_MAIN_H
#define HSK_SECP256K1_MODULE_ELLIGATOR_MAIN_H

#include "elligator.h"

static void
hsk_secp256k1_fe_sqrn(hsk_secp256k1_fe *out, const hsk_secp256k1_fe *in, int rounds) {
  int i;

  hsk_secp256k1_fe_sqr(out, in);

  for (i = 1; i < rounds; i++)
    hsk_secp256k1_fe_sqr(out, out);
}

static void
hsk_secp256k1_fe_pow_pm3d4(hsk_secp256k1_fe *out, const hsk_secp256k1_fe *in) {
  /* Compute a^((p - 3) / 4) with a modification of the square root chain. */
  /* 14M + 254S */
  hsk_secp256k1_fe x1, x2, x3, x6, x9, x11, x22, x44, x88, x176, x220, x223;

  x1 = *in;

  hsk_secp256k1_fe_sqr(&x2, &x1);
  hsk_secp256k1_fe_mul(&x2, &x2, &x1);

  hsk_secp256k1_fe_sqr(&x3, &x2);
  hsk_secp256k1_fe_mul(&x3, &x3, &x1);

  hsk_secp256k1_fe_sqrn(&x6, &x3, 3);
  hsk_secp256k1_fe_mul(&x6, &x6, &x3);

  hsk_secp256k1_fe_sqrn(&x9, &x6, 3);
  hsk_secp256k1_fe_mul(&x9, &x9, &x3);

  hsk_secp256k1_fe_sqrn(&x11, &x9, 2);
  hsk_secp256k1_fe_mul(&x11, &x11, &x2);

  hsk_secp256k1_fe_sqrn(&x22, &x11, 11);
  hsk_secp256k1_fe_mul(&x22, &x22, &x11);

  hsk_secp256k1_fe_sqrn(&x44, &x22, 22);
  hsk_secp256k1_fe_mul(&x44, &x44, &x22);

  hsk_secp256k1_fe_sqrn(&x88, &x44, 44);
  hsk_secp256k1_fe_mul(&x88, &x88, &x44);

  hsk_secp256k1_fe_sqrn(&x176, &x88, 88);
  hsk_secp256k1_fe_mul(&x176, &x176, &x88);

  hsk_secp256k1_fe_sqrn(&x220, &x176, 44);
  hsk_secp256k1_fe_mul(&x220, &x220, &x44);

  hsk_secp256k1_fe_sqrn(&x223, &x220, 3);
  hsk_secp256k1_fe_mul(&x223, &x223, &x3);

  hsk_secp256k1_fe_sqrn(out, &x223, 23);
  hsk_secp256k1_fe_mul(out, out, &x22);
  hsk_secp256k1_fe_sqrn(out, out, 5);
  hsk_secp256k1_fe_mul(out, out, &x1);
  hsk_secp256k1_fe_sqrn(out, out, 3);
  hsk_secp256k1_fe_mul(out, out, &x2);
}

static int
hsk_secp256k1_fe_isqrt(hsk_secp256k1_fe *r,
                   const hsk_secp256k1_fe *u,
                   const hsk_secp256k1_fe *v) {
  hsk_secp256k1_fe u2, u3, u5, v3, p, x, c;
  int ret;

  /* x = u^3 * v * (u^5 * v^3)^((p - 3) / 4) mod p */
  hsk_secp256k1_fe_sqr(&u2, u);
  hsk_secp256k1_fe_mul(&u3, &u2, u);
  hsk_secp256k1_fe_mul(&u5, &u3, &u2);
  hsk_secp256k1_fe_sqr(&v3, v);
  hsk_secp256k1_fe_mul(&v3, &v3, v);
  hsk_secp256k1_fe_mul(&p, &u5, &v3);
  hsk_secp256k1_fe_pow_pm3d4(&p, &p);
  hsk_secp256k1_fe_mul(&x, &u3, v);
  hsk_secp256k1_fe_mul(&x, &x, &p);

  /* x^2 * v == u */
  hsk_secp256k1_fe_sqr(&c, &x);
  hsk_secp256k1_fe_mul(&c, &c, v);
  ret = hsk_secp256k1_fe_equal(&c, u);

  *r = x;

  return ret;
}

static void
shallue_van_de_woestijne_xy2(hsk_secp256k1_fe *x,
                             hsk_secp256k1_fe *y,
                             const hsk_secp256k1_fe *u) {
  /* Copyright (c) 2016 Andrew Poelstra & Pieter Wuille */

  /*
   * Map:
   *
   *   c = sqrt(-3)
   *   d = (c - 1) / 2
   *   w = c * u / (1 + b + u^2) [with b = 7]
   *   x1 = d - u * w
   *   x2 = -(x1 + 1)
   *   x3 = 1 + 1 / w^2
   *
   * To avoid the 2 divisions, compute the above in numerator/denominator form:
   *
   *   wn = c * u
   *   wd = 1 + 7 + u^2
   *   x1n = d * wd - u * wn
   *   x1d = wd
   *   x2n = -(x1n + wd)
   *   x2d = wd
   *   x3n = wd^2 + c^2 + u^2
   *   x3d = (c * u)^2
   *
   * The joint denominator j = wd * c^2 * u^2, and
   *   1 / x1d = 1/j * c^2 * u^2
   *   1 / x2d = x3d = 1/j * wd
   */

  static const hsk_secp256k1_fe c = HSK_SECP256K1_FE_CONST(0x0a2d2ba9, 0x3507f1df,
                                                           0x233770c2, 0xa797962c,
                                                           0xc61f6d15, 0xda14ecd4,
                                                           0x7d8d27ae, 0x1cd5f852);

  static const hsk_secp256k1_fe d = HSK_SECP256K1_FE_CONST(0x851695d4, 0x9a83f8ef,
                                                           0x919bb861, 0x53cbcb16,
                                                           0x630fb68a, 0xed0a766a,
                                                           0x3ec693d6, 0x8e6afa40);

  static const hsk_secp256k1_fe b = HSK_SECP256K1_FE_CONST(0, 0, 0, 0,
                                                           0, 0, 0, 7);

  static const hsk_secp256k1_fe b_plus_one = HSK_SECP256K1_FE_CONST(0, 0, 0, 0,
                                                                    0, 0, 0, 8);

  hsk_secp256k1_fe wn, wd, x1n, x2n, x3n, x3d, jinv, tmp, x1, x2, x3;
  hsk_secp256k1_fe y1, y2, y3;
  int alphaquad, betaquad;

  hsk_secp256k1_fe_mul(&wn, &c, u); /* mag 1 */
  hsk_secp256k1_fe_sqr(&wd, u); /* mag 1 */
  hsk_secp256k1_fe_add(&wd, &b_plus_one); /* mag 2 */
  hsk_secp256k1_fe_mul(&tmp, u, &wn); /* mag 1 */
  hsk_secp256k1_fe_negate(&tmp, &tmp, 1); /* mag 2 */
  hsk_secp256k1_fe_mul(&x1n, &d, &wd); /* mag 1 */
  hsk_secp256k1_fe_add(&x1n, &tmp); /* mag 3 */
  x2n = x1n; /* mag 3 */
  hsk_secp256k1_fe_add(&x2n, &wd); /* mag 5 */
  hsk_secp256k1_fe_negate(&x2n, &x2n, 5); /* mag 6 */
  hsk_secp256k1_fe_mul(&x3d, &c, u); /* mag 1 */
  hsk_secp256k1_fe_sqr(&x3d, &x3d); /* mag 1 */
  hsk_secp256k1_fe_sqr(&x3n, &wd); /* mag 1 */
  hsk_secp256k1_fe_add(&x3n, &x3d); /* mag 2 */
  hsk_secp256k1_fe_mul(&jinv, &x3d, &wd); /* mag 1 */
  hsk_secp256k1_fe_inv(&jinv, &jinv); /* mag 1 */
  hsk_secp256k1_fe_mul(&x1, &x1n, &x3d); /* mag 1 */
  hsk_secp256k1_fe_mul(&x1, &x1, &jinv); /* mag 1 */
  hsk_secp256k1_fe_mul(&x2, &x2n, &x3d); /* mag 1 */
  hsk_secp256k1_fe_mul(&x2, &x2, &jinv); /* mag 1 */
  hsk_secp256k1_fe_mul(&x3, &x3n, &wd); /* mag 1 */
  hsk_secp256k1_fe_mul(&x3, &x3, &jinv); /* mag 1 */

  hsk_secp256k1_fe_sqr(&y1, &x1); /* mag 1 */
  hsk_secp256k1_fe_mul(&y1, &y1, &x1); /* mag 1 */
  hsk_secp256k1_fe_add(&y1, &b); /* mag 2 */
  hsk_secp256k1_fe_sqr(&y2, &x2); /* mag 1 */
  hsk_secp256k1_fe_mul(&y2, &y2, &x2); /* mag 1 */
  hsk_secp256k1_fe_add(&y2, &b); /* mag 2 */
  hsk_secp256k1_fe_sqr(&y3, &x3); /* mag 1 */
  hsk_secp256k1_fe_mul(&y3, &y3, &x3); /* mag 1 */
  hsk_secp256k1_fe_add(&y3, &b); /* mag 2 */

  alphaquad = hsk_secp256k1_fe_sqrt(&tmp, &y1);
  betaquad = hsk_secp256k1_fe_sqrt(&tmp, &y2);

  hsk_secp256k1_fe_cmov(&x1, &x2, (!alphaquad) & betaquad);
  hsk_secp256k1_fe_cmov(&y1, &y2, (!alphaquad) & betaquad);
  hsk_secp256k1_fe_cmov(&x1, &x3, (!alphaquad) & !betaquad);
  hsk_secp256k1_fe_cmov(&y1, &y3, (!alphaquad) & !betaquad);

  *x = x1;
  *y = y1;
}

static void
shallue_van_de_woestijne(hsk_secp256k1_ge *ge, const hsk_secp256k1_fe *u) {
  /* Note: `u` must be normalized for the is_odd() call. */
  hsk_secp256k1_fe x, y, y2;
  int flip;

  shallue_van_de_woestijne_xy2(&x, &y2, u);
  hsk_secp256k1_fe_sqrt(&y, &y2);
  hsk_secp256k1_fe_normalize(&y);

  flip = hsk_secp256k1_fe_is_odd(&y) ^ hsk_secp256k1_fe_is_odd(u);
  hsk_secp256k1_fe_negate(&y2, &y, 1);
  hsk_secp256k1_fe_cmov(&y, &y2, flip);

  hsk_secp256k1_ge_set_xy(ge, &x, &y);
}

static int
shallue_van_de_woestijne_invert(hsk_secp256k1_fe* u,
                                const hsk_secp256k1_ge *ge,
                                unsigned int hint) {
  size_t shift = sizeof(unsigned int) * 8 - 1;

  static const hsk_secp256k1_fe c = HSK_SECP256K1_FE_CONST(0x0a2d2ba9, 0x3507f1df,
                                                           0x233770c2, 0xa797962c,
                                                           0xc61f6d15, 0xda14ecd4,
                                                           0x7d8d27ae, 0x1cd5f852);

  static const hsk_secp256k1_fe one = HSK_SECP256K1_FE_CONST(0, 0, 0, 0,
                                                             0, 0, 0, 1);

  hsk_secp256k1_fe x, y, c0, c1, n0, n1, n2, n3, d0, t, tmp;
  unsigned int s0, s1, s2, s3, flip;
  unsigned int r = hint & 3;

  /*
   * Map:
   *
   *   c = sqrt(-3)
   *   t = sqrt(6 * (2 * b - 1) * x + 9 * x^2 - 12 * b - 3)
   *   u1 = +-sqrt(((b + 1) * (c - 2 * x - 1) / (c + 2 * x + 1))
   *   u2 = +-sqrt(((b + 1) * (c + 2 * x + 1) / (c - 2 * x - 1))
   *   u3 = +-sqrt((3 * (1 - x) - 2 * (b + 1) +- t) / 2)
   *
   * Because every square root has a denominator, we
   * can optimize with the inverse square root trick.
   *
   * Note that the 32 bit backend has a maximum overflow
   * of 6 bits (mag 32). Multiplication functions have a
   * maximum overflow of 4 bits (mag 8).
   */

  if (hsk_secp256k1_ge_is_infinity(ge)) {
    hsk_secp256k1_fe_set_int(u, 0);
    return 0;
  }

  x = ge->x;
  y = ge->y;

  hsk_secp256k1_fe_normalize(&x);
  hsk_secp256k1_fe_normalize(&y);

  /* t = sqrt(6 * (2 * b - 1) * x + 9 * x^2 - 12 * b - 3) */
  hsk_secp256k1_fe_set_int(&tmp, 78); /* mag 1 */
  hsk_secp256k1_fe_mul(&t, &tmp, &x); /* mag 1 */
  hsk_secp256k1_fe_sqr(&tmp, &x); /* mag 1 */
  hsk_secp256k1_fe_mul_int(&tmp, 9); /* mag 9 */
  hsk_secp256k1_fe_add(&t, &tmp); /* mag 10 */
  hsk_secp256k1_fe_set_int(&tmp, 87); /* mag 1 */
  hsk_secp256k1_fe_negate(&tmp, &tmp, 1); /* mag 2 */
  hsk_secp256k1_fe_add(&t, &tmp); /* mag 12 */
  hsk_secp256k1_fe_normalize(&t); /* mag 1 */
  s0 = hsk_secp256k1_fe_sqrt(&tmp, &t); /* mag 1 */
  s1 = ((r - 2) >> shift) | s0; /* r < 2 or t is square */
  t = tmp; /* mag 1 */

  /* c1 = c + 2 * x + 1 */
  c1 = c; /* mag 1 */
  tmp = x; /* mag 1 */
  hsk_secp256k1_fe_mul_int(&tmp, 2); /* mag 2 */
  hsk_secp256k1_fe_add(&tmp, &one); /* mag 3 */
  hsk_secp256k1_fe_add(&c1, &tmp); /* mag 4 */

  /* c0 = c - 2 * x - 1 */
  c0 = c; /* mag 1 */
  hsk_secp256k1_fe_negate(&tmp, &tmp, 3); /* mag 4 */
  hsk_secp256k1_fe_add(&c0, &tmp); /* mag 5 */

  /* n0 = (b + 1) * c0 */
  n0 = c0; /* mag 5 */
  hsk_secp256k1_fe_normalize(&n0); /* mag 1 */
  hsk_secp256k1_fe_mul_int(&n0, 8); /* mag 8 */

  /* n1 = (b + 1) * c1 */
  n1 = c1; /* mag 4 */
  hsk_secp256k1_fe_normalize(&n1); /* mag 1 */
  hsk_secp256k1_fe_mul_int(&n1, 8); /* mag 8 */

  /* n2 = 3 * (1 - x) - 2 * (b + 1) + t */
  n2 = x; /* mag 1 */
  hsk_secp256k1_fe_negate(&n2, &n2, 1); /* mag 2 */
  hsk_secp256k1_fe_add(&n2, &one); /* mag 3 */
  hsk_secp256k1_fe_mul_int(&n2, 3); /* mag 9 */
  hsk_secp256k1_fe_set_int(&tmp, 16); /* mag 1 */
  hsk_secp256k1_fe_negate(&tmp, &tmp, 1); /* mag 2 */
  hsk_secp256k1_fe_add(&n2, &tmp); /* mag 11 */
  hsk_secp256k1_fe_normalize(&n2); /* mag 1 */
  n3 = n2; /* mag 1 */
  hsk_secp256k1_fe_add(&n2, &t); /* mag 2 */

  /* n3 = 3 * (1 - x) - 2 * (b + 1) - t */
  hsk_secp256k1_fe_negate(&t, &t, 1); /* mag 2 */
  hsk_secp256k1_fe_add(&n3, &t); /* mag 3 */

  /* d0 = 2 */
  hsk_secp256k1_fe_set_int(&d0, 2); /* mag 1 */

  /* Pick numerator and denominator. */
  hsk_secp256k1_fe_cmov(&n0, &n1, ((r ^ 1) - 1) >> shift); /* r = 1 */
  hsk_secp256k1_fe_cmov(&n0, &n2, ((r ^ 2) - 1) >> shift); /* r = 2 */
  hsk_secp256k1_fe_cmov(&n0, &n3, ((r ^ 3) - 1) >> shift); /* r = 3 */
  hsk_secp256k1_fe_cmov(&d0, &c1, ((r ^ 0) - 1) >> shift); /* r = 0 */
  hsk_secp256k1_fe_cmov(&d0, &c0, ((r ^ 1) - 1) >> shift); /* r = 1 */

  /* t = sqrt(n0 / d0) */
  s2 = hsk_secp256k1_fe_isqrt(&t, &n0, &d0); /* mag 1 */
  hsk_secp256k1_fe_normalize(&t);

  /* (n0, d0) = svdw(t) */
  shallue_van_de_woestijne_xy2(&n0, &d0, &t); /* mag 1 */
  s3 = hsk_secp256k1_fe_equal(&n0, &x);

  /* t = sign(y) * abs(t) */
  flip = hsk_secp256k1_fe_is_odd(&t) ^ hsk_secp256k1_fe_is_odd(&y);
  hsk_secp256k1_fe_negate(&tmp, &t, 1); /* mag 2 */
  hsk_secp256k1_fe_cmov(&t, &tmp, flip);

  *u = t;

  return s1 & s2 & s3;
}

static void
hsk_secp256k1_fe_random(hsk_secp256k1_fe *fe, hsk_secp256k1_rfc6979_hmac_sha256 *rng) {
  unsigned char raw[32];

  for (;;) {
    hsk_secp256k1_rfc6979_hmac_sha256_generate(rng, raw, 32);

    if (hsk_secp256k1_fe_set_b32(fe, raw))
      break;
  }
}

static unsigned int
hsk_secp256k1_random_int(hsk_secp256k1_rfc6979_hmac_sha256 *rng) {
  unsigned char ch;

  /* We only need one byte (the index ranges from 0-3). */
  hsk_secp256k1_rfc6979_hmac_sha256_generate(rng, &ch, sizeof(ch));

  return ch;
}

int
hsk_secp256k1_ec_pubkey_from_uniform(const hsk_secp256k1_context *ctx,
                                 hsk_secp256k1_pubkey *pubkey,
                                 const unsigned char *bytes32) {
  hsk_secp256k1_ge p;
  hsk_secp256k1_fe u;

  (void)ctx;
  ARG_CHECK(pubkey != NULL);
  ARG_CHECK(bytes32 != NULL);

  hsk_secp256k1_fe_set_b32(&u, bytes32);
  hsk_secp256k1_fe_normalize(&u);

  shallue_van_de_woestijne(&p, &u);

  hsk_secp256k1_pubkey_save(pubkey, &p);

  hsk_secp256k1_ge_clear(&p);
  hsk_secp256k1_fe_clear(&u);

  return 1;
}

int
hsk_secp256k1_ec_pubkey_to_uniform(const hsk_secp256k1_context *ctx,
                               unsigned char *bytes32,
                               const hsk_secp256k1_pubkey *pubkey,
                               unsigned int hint) {
  hsk_secp256k1_ge p;
  hsk_secp256k1_fe u;
  int ret;

  VERIFY_CHECK(ctx != NULL);
  ARG_CHECK(bytes32 != NULL);
  ARG_CHECK(pubkey != NULL);

  if (!hsk_secp256k1_pubkey_load(ctx, &p, pubkey))
    return 0;

  ret = shallue_van_de_woestijne_invert(&u, &p, hint);

  hsk_secp256k1_fe_normalize(&u);
  hsk_secp256k1_fe_get_b32(bytes32, &u);

  hsk_secp256k1_ge_clear(&p);
  hsk_secp256k1_fe_clear(&u);

  return ret;
}

int
hsk_secp256k1_ec_pubkey_from_hash(const hsk_secp256k1_context *ctx,
                              hsk_secp256k1_pubkey *pubkey,
                              const unsigned char *bytes64) {
  hsk_secp256k1_gej j, r;
  hsk_secp256k1_ge p1, p2;
  hsk_secp256k1_fe u1, u2;
  int ret;

  (void)ctx;
  ARG_CHECK(pubkey != NULL);
  ARG_CHECK(bytes64 != NULL);

  hsk_secp256k1_fe_set_b32(&u1, bytes64);
  hsk_secp256k1_fe_set_b32(&u2, bytes64 + 32);

  hsk_secp256k1_fe_normalize(&u1);
  hsk_secp256k1_fe_normalize(&u2);

  shallue_van_de_woestijne(&p1, &u1);
  shallue_van_de_woestijne(&p2, &u2);

  hsk_secp256k1_gej_set_ge(&j, &p1);
  hsk_secp256k1_gej_add_ge(&r, &j, &p2);
  hsk_secp256k1_ge_set_gej(&p1, &r);

  ret = !hsk_secp256k1_ge_is_infinity(&p1);

  if (ret)
    hsk_secp256k1_pubkey_save(pubkey, &p1);

  hsk_secp256k1_gej_clear(&r);
  hsk_secp256k1_gej_clear(&j);
  hsk_secp256k1_ge_clear(&p1);
  hsk_secp256k1_ge_clear(&p2);
  hsk_secp256k1_fe_clear(&u1);
  hsk_secp256k1_fe_clear(&u2);

  return ret;
}

int
hsk_secp256k1_ec_pubkey_to_hash(const hsk_secp256k1_context *ctx,
                            unsigned char *bytes64,
                            const hsk_secp256k1_pubkey *pubkey,
                            const unsigned char *entropy) {
  hsk_secp256k1_rfc6979_hmac_sha256 rng;
  hsk_secp256k1_ge p, p1, p2;
  hsk_secp256k1_gej j, r;
  hsk_secp256k1_fe u1, u2;
  unsigned int hint;

  VERIFY_CHECK(ctx != NULL);
  ARG_CHECK(bytes64 != NULL);
  ARG_CHECK(pubkey != NULL);
  ARG_CHECK(entropy != NULL);

  if (!hsk_secp256k1_pubkey_load(ctx, &p, pubkey))
    return 0;

  hsk_secp256k1_gej_set_ge(&j, &p);
  hsk_secp256k1_rfc6979_hmac_sha256_initialize(&rng, entropy, 32);

  for (;;) {
    hsk_secp256k1_fe_random(&u1, &rng);
    shallue_van_de_woestijne(&p1, &u1);

    hsk_secp256k1_ge_neg(&p1, &p1);
    hsk_secp256k1_gej_add_ge(&r, &j, &p1);
    hsk_secp256k1_ge_set_gej(&p2, &r);

    hint = hsk_secp256k1_random_int(&rng);

    if (shallue_van_de_woestijne_invert(&u2, &p2, hint))
      break;
  }

  hsk_secp256k1_fe_normalize(&u1);
  hsk_secp256k1_fe_normalize(&u2);

  hsk_secp256k1_fe_get_b32(bytes64, &u1);
  hsk_secp256k1_fe_get_b32(bytes64 + 32, &u2);

  hsk_secp256k1_rfc6979_hmac_sha256_finalize(&rng);
  hsk_secp256k1_ge_clear(&p);
  hsk_secp256k1_ge_clear(&p1);
  hsk_secp256k1_ge_clear(&p2);
  hsk_secp256k1_gej_clear(&j);
  hsk_secp256k1_gej_clear(&r);
  hsk_secp256k1_fe_clear(&u1);
  hsk_secp256k1_fe_clear(&u2);

  return 1;
}

#endif /* HSK_SECP256K1_MODULE_ELLIGATOR_MAIN_H */
