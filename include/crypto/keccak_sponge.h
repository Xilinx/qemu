/*
 * Keccak hashing's "absorb" and "squeeze" primitives
 *
 * Copyright (C) 2012 Niels Möller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef KECCAK_SPONGE_H
#define KECCAK_SPONGE_H

#include <stdint.h>
#include <string.h>

typedef struct keccak_sponge {
  uint64_t a[25];
} keccak_sponge_t;

void keccak_absorb(keccak_sponge_t *state, unsigned length, const void *block);
void keccak_squeeze(keccak_sponge_t *state, unsigned length, void *digest);

static inline void keccak_init(keccak_sponge_t *state)
{
    memset(state, 0, sizeof(*state));
}

#endif
