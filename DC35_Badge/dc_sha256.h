// dc_sha256.h - small portable SHA-256 (used for move commitments).
// Portable so the battle engine can also be compiled and tested on a PC.
#pragma once
#include <stdint.h>
#include <stddef.h>

void dcSha256(const uint8_t *data, size_t len, uint8_t out[32]);
