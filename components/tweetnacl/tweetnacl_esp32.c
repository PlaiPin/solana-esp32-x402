/**
 * ESP32-S3 integration for TweetNaCl
 * 
 * Provides randombytes() implementation using ESP32 hardware RNG
 */

#include "esp_random.h"
#include "tweetnacl.h"

/**
 * Random number generator required by TweetNaCl
 * Uses ESP32-S3's hardware True Random Number Generator (TRNG)
 */
void randombytes(unsigned char *buf, unsigned long long len)
{
    esp_fill_random(buf, (size_t)len);
}

