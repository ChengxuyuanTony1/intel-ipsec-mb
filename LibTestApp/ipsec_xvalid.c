/**********************************************************************
  Copyright(c) 2019, Intel Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <malloc.h> /* memalign() or _aligned_malloc()/aligned_free() */

#ifdef _WIN32
#include <intrin.h>
#define strdup _strdup
#define __forceinline static __forceinline
#else
#include <x86intrin.h>
#define __forceinline static inline __attribute__((always_inline))
#endif

#include <intel-ipsec-mb.h>

/* maximum size of a test buffer */
#define JOB_SIZE_TOP (16 * 1024)
/* min size of a buffer when testing range of buffers */
#define DEFAULT_JOB_SIZE_MIN 16
/* max size of a buffer when testing range of buffers */
#define DEFAULT_JOB_SIZE_MAX (2 * 1024)
/* number of bytes to increase buffer size when testing range of buffers */
#define DEFAULT_JOB_SIZE_STEP 16

#define DEFAULT_JOB_ITER 10

#define AAD_SIZE 12
/* Maximum key size for SHA-512 */
#define MAX_KEY_SIZE    SHA_512_BLOCK_SIZE
#define MAX_DIGEST_SIZE SHA512_DIGEST_SIZE_IN_BYTES

#define DIM(x) (sizeof(x)/sizeof(x[0]))

#define SEED 0xdeadcafe

enum arch_type_e {
        ARCH_SSE = 0,
        ARCH_AESNI_EMU,
        ARCH_AVX,
        ARCH_AVX2,
        ARCH_AVX512,
        NUM_ARCHS
};

/* Struct storing cipher parameters */
struct params_s {
        JOB_CIPHER_MODE         cipher_mode; /* CBC, CNTR, DES, GCM etc. */
        JOB_HASH_ALG            hash_alg; /* SHA-1 or others... */
        uint32_t		key_size;
        uint32_t		buf_size;
        uint64_t		aad_size;
        uint32_t		num_sizes;
};

struct cipher_auth_keys {
        uint8_t ipad[SHA512_DIGEST_SIZE_IN_BYTES];
        uint8_t opad[SHA512_DIGEST_SIZE_IN_BYTES];
        DECLARE_ALIGNED(uint32_t k1_expanded[15 * 4], 16);
        DECLARE_ALIGNED(uint8_t	k2[16], 16);
        DECLARE_ALIGNED(uint8_t	k3[16], 16);
        DECLARE_ALIGNED(uint32_t enc_keys[15 * 4], 16);
        DECLARE_ALIGNED(uint32_t dec_keys[15 * 4], 16);
        DECLARE_ALIGNED(struct gcm_key_data gdata_key, 64);
};

struct custom_job_params {
        JOB_CIPHER_MODE         cipher_mode; /* CBC, CNTR, DES, GCM etc. */
        JOB_HASH_ALG            hash_alg; /* SHA-1 or others... */
        uint32_t                key_size;
};

union params {
        enum arch_type_e         arch_type;
        struct custom_job_params job_params;
};

struct str_value_mapping {
        const char      *name;
        union params    values;
};

struct str_value_mapping arch_str_map[] = {
        {.name = "SSE",         .values.arch_type = ARCH_SSE },
        {.name = "AESNI_EMU",   .values.arch_type = ARCH_AESNI_EMU },
        {.name = "AVX",         .values.arch_type = ARCH_AVX },
        {.name = "AVX2",        .values.arch_type = ARCH_AVX2 },
        {.name = "AVX512",      .values.arch_type = ARCH_AVX512 }
};

struct str_value_mapping cipher_algo_str_map[] = {
        {
                .name = "aes-cbc-128",
                .values.job_params = {
                        .cipher_mode = CBC,
                        .key_size = AES_128_BYTES
                }
        },
        {
                .name = "aes-cbc-192",
                .values.job_params = {
                        .cipher_mode = CBC,
                        .key_size = AES_192_BYTES
                }
        },
        {
                .name = "aes-cbc-256",
                .values.job_params = {
                        .cipher_mode = CBC,
                        .key_size = AES_256_BYTES
                }
        },
        {
                .name = "aes-ctr-128",
                .values.job_params = {
                        .cipher_mode = CNTR,
                        .key_size = AES_128_BYTES
                }
        },
        {
                .name = "aes-ctr-192",
                .values.job_params = {
                        .cipher_mode = CNTR,
                        .key_size = AES_192_BYTES
                }
        },
        {
                .name = "aes-ctr-256",
                .values.job_params = {
                        .cipher_mode = CNTR,
                        .key_size = AES_256_BYTES
                }
        },
        {
                .name = "aes-ecb-128",
                .values.job_params = {
                        .cipher_mode = ECB,
                        .key_size = AES_128_BYTES
                }
        },
        {
                .name = "aes-ecb-192",
                .values.job_params = {
                        .cipher_mode = ECB,
                        .key_size = AES_192_BYTES
                }
        },
        {
                .name = "aes-ecb-256",
                .values.job_params = {
                        .cipher_mode = ECB,
                        .key_size = AES_256_BYTES
                }
        },
        {
                .name = "aes-docsis",
                .values.job_params = {
                        .cipher_mode = DOCSIS_SEC_BPI,
                        .key_size = AES_128_BYTES
                }
        },
        {
                .name = "des-docsis",
                .values.job_params = {
                        .cipher_mode = DOCSIS_DES,
                        .key_size = 8
                }
        },
        {
                .name = "des-cbc",
                .values.job_params = {
                        .cipher_mode = DES,
                        .key_size = 8
                }
        },
        {
                .name = "3des-cbc",
                .values.job_params = {
                        .cipher_mode = DES3,
                        .key_size = 24
                }
        },
        {
                .name = "null",
                .values.job_params = {
                        .cipher_mode = NULL_CIPHER,
                        .key_size = 0
                }
        }
};

struct str_value_mapping hash_algo_str_map[] = {
        {
                .name = "sha1-hmac",
                .values.job_params = {
                        .hash_alg = SHA1
                }
        },
        {
                .name = "sha224-hmac",
                .values.job_params = {
                        .hash_alg = SHA_224
                }
        },
        {
                .name = "sha256-hmac",
                .values.job_params = {
                        .hash_alg = SHA_256
                }
        },
        {
                .name = "sha384-hmac",
                .values.job_params = {
                        .hash_alg = SHA_384
                }
        },
        {
                .name = "sha512-hmac",
                .values.job_params = {
                        .hash_alg = SHA_512
                }
        },
        {
                .name = "aes-xcbc",
                .values.job_params = {
                        .hash_alg = AES_XCBC
                }
        },
        {
                .name = "md5-hmac",
                .values.job_params = {
                        .hash_alg = MD5
                }
        },
        {
                .name = "aes-cmac",
                .values.job_params = {
                        .hash_alg = AES_CMAC
                }
        },
        {
                .name = "null",
                .values.job_params = {
                        .hash_alg = NULL_HASH
                }
        },
        {
                .name = "aes-cmac-bitlen",
                .values.job_params = {
                        .hash_alg = AES_CMAC_BITLEN
                }
        },
        {
                .name = "sha1",
                .values.job_params = {
                        .hash_alg = PLAIN_SHA1
                }
        },
        {
                .name = "sha224",
                .values.job_params = {
                        .hash_alg = PLAIN_SHA_224
                }
        },
        {
                .name = "sha256",
                .values.job_params = {
                        .hash_alg = PLAIN_SHA_256
                }
        },
        {
                .name = "sha384",
                .values.job_params = {
                        .hash_alg = PLAIN_SHA_384
                }
        },
        {
                .name = "sha512",
                .values.job_params = {
                        .hash_alg = PLAIN_SHA_512
                }
        },
};

struct str_value_mapping aead_algo_str_map[] = {
        {
                .name = "aes-gcm-128",
                .values.job_params = {
                        .cipher_mode = GCM,
                        .hash_alg = AES_GMAC,
                        .key_size = AES_128_BYTES
                }
        },
        {
                .name = "aes-gcm-192",
                .values.job_params = {
                        .cipher_mode = GCM,
                        .hash_alg = AES_GMAC,
                        .key_size = AES_192_BYTES
                }
        },
        {
                .name = "aes-gcm-256",
                .values.job_params = {
                        .cipher_mode = GCM,
                        .hash_alg = AES_GMAC,
                        .key_size = AES_256_BYTES
                }
        },
        {
                .name = "aes-ccm-128",
                .values.job_params = {
                        .cipher_mode = CCM,
                        .hash_alg = AES_CCM,
                        .key_size = AES_128_BYTES
                }
        }
};

/* This struct stores all information about performed test case */
struct variant_s {
        uint32_t arch;
        struct params_s params;
        uint64_t *avg_times;
};

const uint8_t auth_tag_length_bytes[19] = {
                12, /* SHA1 */
                14, /* SHA_224 */
                16, /* SHA_256 */
                24, /* SHA_384 */
                32, /* SHA_512 */
                12, /* AES_XCBC */
                12, /* MD5 */
                0,  /* NULL_HASH */
#ifndef NO_GCM
                16, /* AES_GMAC */
#endif
                0,  /* CUSTOM HASH */
                16,  /* AES_CCM */
                16, /* AES_CMAC */
                20, /* PLAIN_SHA1 */
                28, /* PLAIN_SHA_224 */
                32, /* PLAIN_SHA_256 */
                48, /* PLAIN_SHA_384 */
                64, /* PLAIN_SHA_512 */
                4,  /* AES_CMAC_BITLEN (3GPP) */
                8,  /* PON */
};

/* Minimum, maximum and step values of key sizes */
const uint8_t key_sizes[12][3] = {
                {16, 32, 16}, /* CBC */
                {16, 32, 16}, /* CNTR */
                {0, 0, 1},    /* NULL */
                {16, 16, 1}, /* DOCSIS_SEC_BPI */
#ifndef NO_GCM
                {16, 32, 16}, /* GCM */
#endif
                {0, 0, 1},    /* CUSTOM_CIPHER */
                {8, 8, 1},    /* DES */
                {8, 8, 1},    /* DOCSIS_DES */
                {16, 16, 1},  /* CCM */
                {24, 24, 1},  /* DES3 */
                {16, 16, 1},  /* PON_AES_CNTR */
                {16, 32, 16}, /* ECB */
};

uint8_t custom_test = 0;
uint8_t verbose = 0;

enum range {
        RANGE_MIN = 0,
        RANGE_STEP,
        RANGE_MAX,
        NUM_RANGE
};

uint32_t job_sizes[NUM_RANGE] = {DEFAULT_JOB_SIZE_MIN,
                                 DEFAULT_JOB_SIZE_STEP,
                                 DEFAULT_JOB_SIZE_MAX};
uint32_t job_iter = DEFAULT_JOB_ITER;

struct custom_job_params custom_job_params = {
        .cipher_mode  = NULL_CIPHER,
        .hash_alg     = NULL_HASH,
        .key_size = 0
};

/* AESNI_EMU disabled by default */
uint8_t enc_archs[NUM_ARCHS] = {1, 0, 1, 1, 1};
uint8_t dec_archs[NUM_ARCHS] = {1, 0, 1, 1, 1};

uint64_t flags = 0; /* flags passed to alloc_mb_mgr() */

/** Generate random buffer */
static void
generate_random_buf(uint8_t *buf, unsigned int length)
{
        unsigned int i;

        for (i = 0; i < length; i++)
                buf[i] = rand() % UINT8_MAX;
}

static void
byte_hexdump(const char *message, const uint8_t *ptr, const uint32_t len)
{
        uint32_t ctr;

        printf("%s:\n", message);
        for (ctr = 0; ctr < len; ctr++) {
                printf("0x%02X ", ptr[ctr] & 0xff);
                if (!((ctr + 1) % 16))
                        printf("\n");
        }
        printf("\n");
        printf("\n");
};

static void
print_algo_info(const struct params_s *params)
{
        struct custom_job_params *job_params;
        uint32_t i;

        for (i = 0; i < DIM(aead_algo_str_map); i++) {
                job_params = &aead_algo_str_map[i].values.job_params;
                if (job_params->cipher_mode == params->cipher_mode &&
                    job_params->hash_alg == params->hash_alg &&
                    job_params->key_size == params->key_size) {
                        printf("AEAD algo = %s\n", aead_algo_str_map[i].name);
                        return;
                }
        }

        for (i = 0; i < DIM(cipher_algo_str_map); i++) {
                job_params = &cipher_algo_str_map[i].values.job_params;
                if (job_params->cipher_mode == params->cipher_mode &&
                    job_params->key_size == params->key_size) {
                        printf("Cipher algo = %s ",
                               cipher_algo_str_map[i].name);
                        break;
                }
        }
        for (i = 0; i < DIM(hash_algo_str_map); i++) {
                job_params = &hash_algo_str_map[i].values.job_params;
                if (job_params->hash_alg == params->hash_alg) {
                        printf("Hash algo = %s\n", hash_algo_str_map[i].name);
                        break;
                }
        }
}

static void
print_arch_info(const enum arch_type_e arch)
{
        uint32_t i;

        for (i = 0; i < DIM(arch_str_map); i++) {
                if (arch_str_map[i].values.arch_type == arch)
                        printf("Architecture = %s\n",
                               arch_str_map[i].name);
        }
}

static int
fill_job(JOB_AES_HMAC *job, const struct params_s *params,
         uint8_t *buf, uint8_t *digest, const uint8_t *aad,
         const uint32_t buf_size, const uint8_t tag_size,
         JOB_CIPHER_DIRECTION cipher_dir,
         struct cipher_auth_keys *keys, uint8_t *iv)
{
        static const void *ks_ptr[3];
        uint32_t *k1_expanded = keys->k1_expanded;
        uint8_t *k2 = keys->k2;
        uint8_t *k3 = keys->k3;
        uint32_t *enc_keys = keys->enc_keys;
        uint32_t *dec_keys = keys->dec_keys;
        uint8_t *ipad = keys->ipad;
        uint8_t *opad = keys->opad;
        struct gcm_key_data *gdata_key = &keys->gdata_key;

        job->msg_len_to_cipher_in_bytes = buf_size;
        job->msg_len_to_hash_in_bytes = buf_size;
        job->hash_start_src_offset_in_bytes = 0;
        job->cipher_start_src_offset_in_bytes = 0;
        job->iv = iv;

        /* In-place operation */
        job->src = buf;
        job->dst = buf;
        job->auth_tag_output = digest;

        job->hash_alg = params->hash_alg;
        switch (params->hash_alg) {
        case AES_XCBC:
                job->u.XCBC._k1_expanded = k1_expanded;
                job->u.XCBC._k2 = k2;
                job->u.XCBC._k3 = k3;
                break;
        case AES_CMAC:
                job->u.CMAC._key_expanded = k1_expanded;
                job->u.CMAC._skey1 = k2;
                job->u.CMAC._skey2 = k3;
                break;
        case AES_CMAC_BITLEN:
                job->u.CMAC_BITLEN._key_expanded = k1_expanded;
                job->u.CMAC_BITLEN._skey1 = k2;
                job->u.CMAC_BITLEN._skey2 = k3;
                job->u.CMAC_BITLEN.msg_len_to_hash_in_bits =
                        (job->msg_len_to_hash_in_bytes * 8) - 4;
                break;
        case SHA1:
        case SHA_224:
        case SHA_256:
        case SHA_384:
        case SHA_512:
        case MD5:
                /* HMAC hash alg is SHA1 or MD5 */
                job->u.HMAC._hashed_auth_key_xor_ipad =
                        (uint8_t *) ipad;
                job->u.HMAC._hashed_auth_key_xor_opad =
                        (uint8_t *) opad;
                break;
        case NULL_HASH:
        case AES_GMAC:
        case AES_CCM:
        case PLAIN_SHA1:
        case PLAIN_SHA_224:
        case PLAIN_SHA_256:
        case PLAIN_SHA_384:
        case PLAIN_SHA_512:
                /* No operation needed */
                break;
        default:
                printf("Unsupported hash algorithm\n");
                return -1;
        }

        job->auth_tag_output_len_in_bytes = tag_size;

        job->cipher_direction = cipher_dir;

        if (params->cipher_mode == NULL_CIPHER) {
                job->chain_order = HASH_CIPHER;
        } else if (params->cipher_mode == CCM) {
                if (job->cipher_direction == ENCRYPT)
                        job->chain_order = HASH_CIPHER;
                else
                        job->chain_order = CIPHER_HASH;
        } else {
                if (job->cipher_direction == ENCRYPT)
                        job->chain_order = CIPHER_HASH;
                else
                        job->chain_order = HASH_CIPHER;
        }

        /* Translating enum to the API's one */
        job->cipher_mode = params->cipher_mode;
        job->aes_key_len_in_bytes = params->key_size;

        switch (job->cipher_mode) {
        case CBC:
        case DOCSIS_SEC_BPI:
                job->aes_enc_key_expanded = enc_keys;
                job->aes_dec_key_expanded = dec_keys;
                job->iv_len_in_bytes = 16;
                break;
        case CNTR:
                job->aes_enc_key_expanded = enc_keys;
                job->aes_dec_key_expanded = enc_keys;
                job->iv_len_in_bytes = 16;
                break;
        case GCM:
                job->aes_enc_key_expanded = gdata_key;
                job->aes_dec_key_expanded = gdata_key;
                job->u.GCM.aad_len_in_bytes = params->aad_size;
                job->u.GCM.aad = aad;
                job->iv_len_in_bytes = 12;
                break;
        case CCM:
                job->msg_len_to_cipher_in_bytes = buf_size;
                job->msg_len_to_hash_in_bytes = buf_size;
                job->hash_start_src_offset_in_bytes = 0;
                job->cipher_start_src_offset_in_bytes = 0;
                job->u.CCM.aad_len_in_bytes = params->aad_size;
                job->u.CCM.aad = aad;
                job->aes_enc_key_expanded = enc_keys;
                job->aes_dec_key_expanded = enc_keys;
                job->iv_len_in_bytes = 13;
                break;
        case DES:
        case DOCSIS_DES:
                job->aes_enc_key_expanded = enc_keys;
                job->aes_dec_key_expanded = enc_keys;
                job->iv_len_in_bytes = 8;
                break;
        case DES3:
                ks_ptr[0] = ks_ptr[1] = ks_ptr[2] = enc_keys;
                job->aes_enc_key_expanded = ks_ptr;
                job->aes_dec_key_expanded = ks_ptr;
                job->iv_len_in_bytes = 8;
                break;
        case ECB:
                job->aes_enc_key_expanded = enc_keys;
                job->aes_dec_key_expanded = dec_keys;
                job->iv_len_in_bytes = 0;
                break;
        case NULL_CIPHER:
                /* No operation needed */
                break;
        default:
                printf("Unsupported cipher mode\n");
                return -1;
        }

        return 0;
}

static int
prepare_keys(MB_MGR *mb_mgr, struct cipher_auth_keys *keys,
             const uint8_t *key, const struct params_s *params)
{
        static uint8_t buf[SHA_512_BLOCK_SIZE];
        static DECLARE_ALIGNED(uint32_t dust[15 * 4], 16);
        uint32_t *k1_expanded = keys->k1_expanded;
        uint8_t *k2 = keys->k2;
        uint8_t *k3 = keys->k3;
        uint32_t *enc_keys = keys->enc_keys;
        uint32_t *dec_keys = keys->dec_keys;
        uint8_t *ipad = keys->ipad;
        uint8_t *opad = keys->opad;
        struct gcm_key_data *gdata_key = &keys->gdata_key;
        uint8_t i;

        switch (params->hash_alg) {
        case AES_XCBC:
                IMB_AES_XCBC_KEYEXP(mb_mgr, key, k1_expanded, k2, k3);
                break;
        case AES_CMAC:
        case AES_CMAC_BITLEN:
                IMB_AES_KEYEXP_128(mb_mgr, key, k1_expanded, dust);
                IMB_AES_CMAC_SUBKEY_GEN_128(mb_mgr, k1_expanded, k2, k3);
                break;
        case SHA1:
                /* compute ipad hash */
                memset(buf, 0x36, sizeof(buf));
                for (i = 0; i < SHA1_BLOCK_SIZE; i++)
                        buf[i] ^= key[i];
                IMB_SHA1_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, sizeof(buf));
                for (i = 0; i < SHA1_BLOCK_SIZE; i++)
                        buf[i] ^= key[i];
                IMB_SHA1_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case SHA_224:
                /* compute ipad hash */
                memset(buf, 0x36, sizeof(buf));
                for (i = 0; i < SHA_256_BLOCK_SIZE; i++)
                        buf[i] ^= key[i];
                IMB_SHA224_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, sizeof(buf));
                for (i = 0; i < SHA_256_BLOCK_SIZE; i++)
                        buf[i] ^= key[i];
                IMB_SHA224_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case SHA_256:
                /* compute ipad hash */
                memset(buf, 0x36, sizeof(buf));
                for (i = 0; i < SHA_256_BLOCK_SIZE; i++)
                        buf[i] ^= key[i];
                IMB_SHA256_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, sizeof(buf));
                for (i = 0; i < SHA_256_BLOCK_SIZE; i++)
                        buf[i] ^= key[i];
                IMB_SHA256_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case SHA_384:
                /* compute ipad hash */
                memset(buf, 0x36, sizeof(buf));
                for (i = 0; i < SHA_384_BLOCK_SIZE; i++)
                        buf[i] ^= key[i];
                IMB_SHA384_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, sizeof(buf));
                for (i = 0; i < SHA_384_BLOCK_SIZE; i++)
                        buf[i] ^= key[i];
                IMB_SHA384_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case SHA_512:
                /* compute ipad hash */
                memset(buf, 0x36, sizeof(buf));
                for (i = 0; i < SHA_512_BLOCK_SIZE; i++)
                        buf[i] ^= key[i];
                IMB_SHA512_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, sizeof(buf));
                for (i = 0; i < SHA_512_BLOCK_SIZE; i++)
                        buf[i] ^= key[i];
                IMB_SHA512_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case MD5:
                /* compute ipad hash */
                memset(buf, 0x36, sizeof(buf));
                for (i = 0; i < 64; i++)
                        buf[i] ^= key[i];
                IMB_MD5_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, sizeof(buf));
                for (i = 0; i < 64; i++)
                        buf[i] ^= key[i];
                IMB_MD5_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case AES_CCM:
        case AES_GMAC:
        case NULL_HASH:
        case PLAIN_SHA1:
        case PLAIN_SHA_224:
        case PLAIN_SHA_256:
        case PLAIN_SHA_384:
        case PLAIN_SHA_512:
                /* No operation needed */
                break;
        default:
                fprintf(stderr, "Unsupported hash algo\n");
                return -1;
        }

        switch (params->cipher_mode) {
        case GCM:
                switch (params->key_size) {
                case AES_128_BYTES:
                        IMB_AES128_GCM_PRE(mb_mgr, key, gdata_key);
                        break;
                case AES_192_BYTES:
                        IMB_AES192_GCM_PRE(mb_mgr, key, gdata_key);
                        break;
                case AES_256_BYTES:
                        IMB_AES256_GCM_PRE(mb_mgr, key, gdata_key);
                        break;
                default:
                        fprintf(stderr, "Wrong key size\n");
                        return -1;
                }
                break;
        case CBC:
        case CCM:
        case CNTR:
        case DOCSIS_SEC_BPI:
        case ECB:
                switch (params->key_size) {
                case AES_128_BYTES:
                        IMB_AES_KEYEXP_128(mb_mgr, key, enc_keys, dec_keys);
                        break;
                case AES_192_BYTES:
                        IMB_AES_KEYEXP_192(mb_mgr, key, enc_keys, dec_keys);
                        break;
                case AES_256_BYTES:
                        IMB_AES_KEYEXP_256(mb_mgr, key, enc_keys, dec_keys);
                        break;
                default:
                        fprintf(stderr, "Wrong key size\n");
                        return -1;
                }
                break;
        case DES:
        case DES3:
        case DOCSIS_DES:
                des_key_schedule((uint64_t *) enc_keys, key);
                break;
        case NULL_CIPHER:
                /* No operation needed */
                break;
        default:
                fprintf(stderr, "Unsupported cipher mode\n");
                return -1;
        }

        return 0;
}

/* Performs test using AES_HMAC or DOCSIS */
static int
do_test(MB_MGR *enc_mb_mgr, const enum arch_type_e enc_arch,
        MB_MGR *dec_mb_mgr, const enum arch_type_e dec_arch,
        const struct params_s *params)
{
        JOB_AES_HMAC *job;
        uint32_t i;
        int ret = -1;
        static DECLARE_ALIGNED(uint8_t iv[16], 16);
        static struct cipher_auth_keys keys;
        uint8_t aad[AAD_SIZE];
        uint8_t in_digest[MAX_DIGEST_SIZE];
        uint8_t out_digest[MAX_DIGEST_SIZE];
        uint32_t buf_size = params->buf_size;
        uint8_t *test_buf = NULL;
        uint8_t *src_dst_buf = NULL;
        uint8_t tag_size = auth_tag_length_bytes[params->hash_alg - 1];
        uint8_t key[MAX_KEY_SIZE];

        test_buf = malloc(buf_size);
        if (test_buf == NULL)
                goto exit;

        src_dst_buf = malloc(buf_size);
        if (src_dst_buf == NULL)
                goto exit;

        /* Randomize input buffer, key and IV */
        generate_random_buf(test_buf, buf_size);
        generate_random_buf(key, MAX_KEY_SIZE);
        generate_random_buf(iv, 16);

        /* Expand/schedule keys */
        if (prepare_keys(enc_mb_mgr, &keys, key, params) < 0)
                goto exit;

        for (i = 0; i < job_iter; i++) {
                job = IMB_GET_NEXT_JOB(enc_mb_mgr);
                /*
                 * Encrypt + generate digest from encrypted message
                 * using architecture under test
                 */
                memcpy(src_dst_buf, test_buf, buf_size);
                if (fill_job(job, params, src_dst_buf, in_digest, aad,
                             buf_size, tag_size, ENCRYPT, &keys, iv) < 0)
                        goto exit;

                job = IMB_SUBMIT_JOB(enc_mb_mgr);

                if (!job)
                        job = IMB_FLUSH_JOB(enc_mb_mgr);

                if (!job) {
                        fprintf(stderr, "job not returned\n");
                        goto exit;
                }

                if (job->status != STS_COMPLETED) {
                        fprintf(stderr, "failed job, status:%d\n",
                                job->status);
                        goto exit;
                }

                job = IMB_GET_NEXT_JOB(dec_mb_mgr);
                /*
                 * Generate digest from encrypted message and decrypt
                 * using reference architecture
                 */
                if (fill_job(job, params, src_dst_buf, out_digest, aad,
                             buf_size, tag_size, DECRYPT, &keys, iv) < 0)
                        goto exit;

                job = IMB_SUBMIT_JOB(dec_mb_mgr);

                if (!job)
                        job = IMB_FLUSH_JOB(dec_mb_mgr);

                if (!job) {
                        fprintf(stderr, "job not returned\n");
                        goto exit;
                }

                if (job->status != STS_COMPLETED) {
                        fprintf(stderr, "failed job, status:%d\n",
                                job->status);
                        goto exit;
                }

                if (params->hash_alg != NULL_HASH &&
                    memcmp(in_digest, out_digest, tag_size) != 0) {
                        fprintf(stderr,
                                "\nInput and output tags don't match\n");
                        byte_hexdump("Input digest", in_digest, tag_size);
                        byte_hexdump("Output digest", out_digest, tag_size);
                        goto exit;
                }

                if (params->cipher_mode != NULL_CIPHER &&
                    memcmp(src_dst_buf, test_buf, buf_size) != 0) {
                        fprintf(stderr,
                                "\nDecrypted text and plaintext don't match\n");
                        byte_hexdump("Plaintext (orig)", test_buf, buf_size);
                        byte_hexdump("Decrypted msg", src_dst_buf, buf_size);
                        goto exit;
                }
        }

        ret = 0;

exit:
        free(src_dst_buf);
        free(test_buf);

        if (ret < 0) {
                printf("Failures in\n");
                print_algo_info(params);
                printf("Encrypting ");
                print_arch_info(enc_arch);
                printf("Decrypting ");
                print_arch_info(dec_arch);
                printf("Buffer size = %u\n", buf_size);
                printf("Key size = %u\n", params->key_size);
                printf("Tag size = %u\n", tag_size);
        }

        return ret;
}

/* Runs test for each buffer size */
static void
process_variant(MB_MGR *enc_mgr, const enum arch_type_e enc_arch,
                MB_MGR *dec_mgr, const enum arch_type_e dec_arch,
                struct params_s *params)
{
        const uint32_t sizes = params->num_sizes;
        uint32_t sz;

        if (verbose) {
                printf("Testing ");
                print_algo_info(params);
        }

        for (sz = 0; sz < sizes; sz++) {
                const uint32_t buf_size = job_sizes[RANGE_MIN] +
                                        (sz * job_sizes[RANGE_STEP]);
                params->aad_size = AAD_SIZE;

                params->buf_size = buf_size;

                if (do_test(enc_mgr, enc_arch, dec_mgr, dec_arch, params) < 0)
                        exit(EXIT_FAILURE);
        }
}

/* Sets cipher direction and key size  */
static void
run_test(const enum arch_type_e enc_arch, const enum arch_type_e dec_arch,
         struct params_s *params)
{
        MB_MGR *enc_mgr = NULL;
        MB_MGR *dec_mgr = NULL;

        if (enc_arch == ARCH_AESNI_EMU)
                enc_mgr = alloc_mb_mgr(flags | IMB_FLAG_AESNI_OFF);
        else
                enc_mgr = alloc_mb_mgr(flags);

        if (enc_mgr == NULL) {
                fprintf(stderr, "MB MGR could not be allocated\n");
                exit(EXIT_FAILURE);
        }

        switch (enc_arch) {
        case ARCH_SSE:
        case ARCH_AESNI_EMU:
                init_mb_mgr_sse(enc_mgr);
                break;
        case ARCH_AVX:
                init_mb_mgr_avx(enc_mgr);
                break;
        case ARCH_AVX2:
                init_mb_mgr_avx2(enc_mgr);
                break;
        case ARCH_AVX512:
                init_mb_mgr_avx512(enc_mgr);
                break;
        default:
                fprintf(stderr, "Invalid architecture\n");
                exit(EXIT_FAILURE);
        }

        if (dec_arch == ARCH_AESNI_EMU)
                dec_mgr = alloc_mb_mgr(flags | IMB_FLAG_AESNI_OFF);
        else
                dec_mgr = alloc_mb_mgr(flags);

        if (dec_mgr == NULL) {
                fprintf(stderr, "MB MGR could not be allocated\n");
                exit(EXIT_FAILURE);
        }

        switch (dec_arch) {
        case ARCH_SSE:
        case ARCH_AESNI_EMU:
                init_mb_mgr_sse(dec_mgr);
                break;
        case ARCH_AVX:
                init_mb_mgr_avx(dec_mgr);
                break;
        case ARCH_AVX2:
                init_mb_mgr_avx2(dec_mgr);
                break;
        case ARCH_AVX512:
                init_mb_mgr_avx512(dec_mgr);
                break;
        default:
                fprintf(stderr, "Invalid architecture\n");
                exit(EXIT_FAILURE);
        }

        if (custom_test) {
                params->key_size = custom_job_params.key_size;
                params->cipher_mode = custom_job_params.cipher_mode;
                params->hash_alg = custom_job_params.hash_alg;
                process_variant(enc_mgr, enc_arch, dec_mgr, dec_arch, params);
                return;
        }

        JOB_HASH_ALG    hash_alg;
        JOB_CIPHER_MODE c_mode;

        for (c_mode = CBC; c_mode <= ECB; c_mode++) {
                /* Skip CUSTOM_CIPHER and PON */
                if (c_mode == CUSTOM_CIPHER || c_mode == PON_AES_CNTR)
                        continue;
                params->cipher_mode = c_mode;
                uint8_t min_sz = key_sizes[c_mode - 1][0];
                uint8_t max_sz = key_sizes[c_mode - 1][1];
                uint8_t step_sz = key_sizes[c_mode - 1][2];
                uint8_t key_sz;

                for (key_sz = min_sz; key_sz <= max_sz; key_sz += step_sz) {
                        params->key_size = key_sz;
                        for (hash_alg = SHA1; hash_alg <= AES_CMAC_BITLEN;
                             hash_alg++) {
                                /* Skip CUSTOM_HASH and PON */
                                if (hash_alg == CUSTOM_HASH ||
                                    hash_alg == PON_CRC_BIP)
                                        continue;
                                /* Skip not supported combinations */
                                if ((c_mode == GCM && hash_alg != AES_GMAC) ||
                                    (c_mode != GCM && hash_alg == AES_GMAC))
                                        continue;
                                if ((c_mode == CCM && hash_alg != AES_CCM) ||
                                    (c_mode != CCM && hash_alg == AES_CCM))
                                        continue;

                                params->hash_alg = hash_alg;
                                process_variant(enc_mgr, enc_arch, dec_mgr,
                                                dec_arch, params);
                        }
                }
        }

        free_mb_mgr(enc_mgr);
        free_mb_mgr(dec_mgr);
}

/* Prepares data structure for test variants storage, sets test configuration */
static void
run_tests(void)
{
        struct params_s params;
        enum arch_type_e enc_arch, dec_arch;
        const uint32_t min_size = job_sizes[RANGE_MIN];
        const uint32_t max_size = job_sizes[RANGE_MAX];
        const uint32_t step_size = job_sizes[RANGE_STEP];

        params.num_sizes = ((max_size - min_size) / step_size) + 1;

        if (verbose) {
                if (min_size == max_size)
                        printf("Testing buffer size = %u bytes\n", min_size);
                else
                        printf("Testing buffer sizes from %u to %u "
                               "in steps of %u bytes\n",
                               min_size, max_size, step_size);
        }
        /* Performing tests for each selected architecture */
        for (enc_arch = ARCH_SSE; enc_arch < NUM_ARCHS; enc_arch++) {
                if (enc_archs[enc_arch] == 0)
                        continue;
                printf("\nEncrypting with ");
                print_arch_info(enc_arch);

                for (dec_arch = ARCH_SSE; dec_arch < NUM_ARCHS; dec_arch++) {
                        if (dec_archs[dec_arch] == 0)
                                continue;
                        printf("\tDecrypting with ");
                        print_arch_info(dec_arch);
                        run_test(enc_arch, dec_arch, &params);
                }

        } /* end for run */
}

static void usage(void)
{
        fprintf(stderr, "Usage: exhaustive_test [args], "
                "where args are zero or more\n"
                "-h: print this message\n"
                "-v: verbose, prints extra information\n"
                "--enc-arch: encrypting with architecture (AESNI_EMU/SSE/AVX/AVX2/AVX512)\n"
                "--dec-arch: decrypting with architecture (AESNI_EMU/SSE/AVX/AVX2/AVX512)\n"
                "--cipher-algo: Select cipher algorithm to run on the custom test\n"
                "--hash-algo: Select hash algorithm to run on the custom test\n"
                "--aead-algo: Select AEAD algorithm to run on the custom test\n"
                "--no-avx512: Don't do AVX512\n"
                "--no-avx2: Don't do AVX2\n"
                "--no-avx: Don't do AVX\n"
                "--no-sse: Don't do SSE\n"
                "--aesni-emu: Do AESNI_EMU (disabled by default)\n"
                "--shani-on: use SHA extensions, default: auto-detect\n"
                "--shani-off: don't use SHA extensions\n"
                "--job-size: size of the cipher & MAC job in bytes. It can be:\n"
                "            - single value: test single size\n"
                "            - range: test multiple sizes with following format"
                " min:step:max (e.g. 16:16:256)\n"
                "            (-o still applies for MAC)\n"
                "--job-iter: number of tests iterations for each job size\n");
}

static int
get_next_num_arg(const char * const *argv, const int index, const int argc,
                 void *dst, const size_t dst_size)
{
        char *endptr = NULL;
        uint64_t val;

        if (dst == NULL || argv == NULL || index < 0 || argc < 0) {
                fprintf(stderr, "%s() internal error!\n", __func__);
                exit(EXIT_FAILURE);
        }

        if (index >= (argc - 1)) {
                fprintf(stderr, "'%s' requires an argument!\n", argv[index]);
                exit(EXIT_FAILURE);
        }

#ifdef _WIN32
        val = _strtoui64(argv[index + 1], &endptr, 0);
#else
        val = strtoull(argv[index + 1], &endptr, 0);
#endif
        if (endptr == argv[index + 1] || (endptr != NULL && *endptr != '\0')) {
                fprintf(stderr, "Error converting '%s' as value for '%s'!\n",
                        argv[index + 1], argv[index]);
                exit(EXIT_FAILURE);
        }

        switch (dst_size) {
        case (sizeof(uint8_t)):
                *((uint8_t *)dst) = (uint8_t) val;
                break;
        case (sizeof(uint16_t)):
                *((uint16_t *)dst) = (uint16_t) val;
                break;
        case (sizeof(uint32_t)):
                *((uint32_t *)dst) = (uint32_t) val;
                break;
        case (sizeof(uint64_t)):
                *((uint64_t *)dst) = val;
                break;
        default:
                fprintf(stderr, "%s() invalid dst_size %u!\n",
                        __func__, (unsigned) dst_size);
                exit(EXIT_FAILURE);
                break;
        }

        return index + 1;
}

static int
detect_arch(unsigned int arch_support[NUM_ARCHS])
{
        const uint64_t detect_sse =
                IMB_FEATURE_SSE4_2 | IMB_FEATURE_CMOV | IMB_FEATURE_AESNI;
        const uint64_t detect_avx =
                IMB_FEATURE_AVX | IMB_FEATURE_CMOV | IMB_FEATURE_AESNI;
        const uint64_t detect_avx2 = IMB_FEATURE_AVX2 | detect_avx;
        const uint64_t detect_avx512 = IMB_FEATURE_AVX512_SKX | detect_avx2;
        MB_MGR *p_mgr = NULL;
        enum arch_type_e arch_id;

        if (arch_support == NULL) {
                fprintf(stderr, "Array not passed correctly\n");
                return -1;
        }

        for (arch_id = ARCH_SSE; arch_id < NUM_ARCHS; arch_id++)
                arch_support[arch_id] = 1;

        p_mgr = alloc_mb_mgr(0);
        if (p_mgr == NULL) {
                fprintf(stderr, "Architecture detect error!\n");
                return -1;
        }

        if ((p_mgr->features & detect_avx512) != detect_avx512)
                arch_support[ARCH_AVX512] = 0;

        if ((p_mgr->features & detect_avx2) != detect_avx2)
                arch_support[ARCH_AVX2] = 0;

        if ((p_mgr->features & detect_avx) != detect_avx)
                arch_support[ARCH_AVX] = 0;

        if ((p_mgr->features & detect_sse) != detect_sse) {
                arch_support[ARCH_SSE] = 0;
                arch_support[ARCH_AESNI_EMU] = 0;
        }

        free_mb_mgr(p_mgr);

        return 0;
}

/*
 * Check string argument is supported and if it is, return values associated
 * with it.
 */
static const union params *
check_string_arg(const char *param, const char *arg,
                 const struct str_value_mapping *map,
                 const unsigned int num_avail_opts)
{
        unsigned int i;

        if (arg == NULL) {
                fprintf(stderr, "%s requires an argument\n", param);
                goto exit;
        }

        for (i = 0; i < num_avail_opts; i++)
                if (strcmp(arg, map[i].name) == 0)
                        return &(map[i].values);

        /* Argument is not listed in the available options */
        fprintf(stderr, "Invalid argument for %s\n", param);
exit:
        fprintf(stderr, "Accepted arguments: ");
        for (i = 0; i < num_avail_opts; i++)
                fprintf(stderr, "%s ", map[i].name);
        fprintf(stderr, "\n");

        return NULL;
}

static int
parse_range(const char * const *argv, const int index, const int argc,
            uint32_t range_values[NUM_RANGE])
{
        char *token;
        uint32_t number;
        unsigned int i;


        if (range_values == NULL || argv == NULL || index < 0 || argc < 0) {
                fprintf(stderr, "%s() internal error!\n", __func__);
                exit(EXIT_FAILURE);
        }

        if (index >= (argc - 1)) {
                fprintf(stderr, "'%s' requires an argument!\n", argv[index]);
                exit(EXIT_FAILURE);
        }

        char *copy_arg = strdup(argv[index + 1]);

        if (copy_arg == NULL) {
                fprintf(stderr, "%s() internal error!\n", __func__);
                exit(EXIT_FAILURE);
        }

        errno = 0;
        token = strtok(copy_arg, ":");

        /* Try parsing range (minimum, step and maximum values) */
        for (i = 0; i < NUM_RANGE; i++) {
                if (token == NULL)
                        goto no_range;

                number = strtoul(token, NULL, 10);

                if (errno != 0)
                        goto no_range;

                range_values[i] = number;
                token = strtok(NULL, ":");
        }

        if (token != NULL)
                goto no_range;

        if (range_values[RANGE_MAX] < range_values[RANGE_MIN]) {
                fprintf(stderr, "Maximum value of range cannot be lower "
                        "than minimum value\n");
                exit(EXIT_FAILURE);
        }

        if (range_values[RANGE_STEP] == 0) {
                fprintf(stderr, "Step value in range cannot be 0\n");
                exit(EXIT_FAILURE);
        }

        goto end_range;
no_range:
        /* Try parsing as single value */
        get_next_num_arg(argv, index, argc, &job_sizes[RANGE_MIN],
                     sizeof(job_sizes[RANGE_MIN]));

        job_sizes[RANGE_MAX] = job_sizes[RANGE_MIN];

end_range:
        free(copy_arg);
        return (index + 1);

}

int main(int argc, char *argv[])
{
        int i;
        unsigned int arch_id;
        unsigned int arch_support[NUM_ARCHS];
        const union params *values;
        unsigned int cipher_algo_set = 0;
        unsigned int hash_algo_set = 0;
        unsigned int aead_algo_set = 0;

        for (i = 1; i < argc; i++)
                if (strcmp(argv[i], "-h") == 0) {
                        usage();
                        return EXIT_SUCCESS;
                } else if (strcmp(argv[i], "-v") == 0) {
                        verbose = 1;
                } else if (strcmp(argv[i], "--no-avx512") == 0) {
                        enc_archs[ARCH_AVX512] = 0;
                        dec_archs[ARCH_AVX512] = 0;
                } else if (strcmp(argv[i], "--no-avx2") == 0) {
                        enc_archs[ARCH_AVX2] = 0;
                        dec_archs[ARCH_AVX2] = 0;
                } else if (strcmp(argv[i], "--no-avx") == 0) {
                        enc_archs[ARCH_AVX] = 0;
                        dec_archs[ARCH_AVX] = 0;
                } else if (strcmp(argv[i], "--no-sse") == 0) {
                        enc_archs[ARCH_SSE] = 0;
                        dec_archs[ARCH_SSE] = 0;
                } else if (strcmp(argv[i], "--aesni-emu") == 0) {
                        enc_archs[ARCH_AESNI_EMU] = 1;
                        dec_archs[ARCH_AESNI_EMU] = 1;
                } else if (strcmp(argv[i], "--shani-on") == 0) {
                        flags &= (~IMB_FLAG_SHANI_OFF);
                } else if (strcmp(argv[i], "--shani-off") == 0) {
                        flags |= IMB_FLAG_SHANI_OFF;
                } else if (strcmp(argv[i], "--enc-arch") == 0) {
                        values = check_string_arg(argv[i], argv[i+1],
                                                  arch_str_map,
                                                  DIM(arch_str_map));
                        if (values == NULL)
                                return EXIT_FAILURE;

                        /*
                         * Disable all the other architectures
                         * and enable only the specified
                         */
                        memset(enc_archs, 0, sizeof(enc_archs));
                        enc_archs[values->arch_type] = 1;
                        i++;
                } else if (strcmp(argv[i], "--dec-arch") == 0) {
                        values = check_string_arg(argv[i], argv[i+1],
                                                  arch_str_map,
                                                  DIM(arch_str_map));
                        if (values == NULL)
                                return EXIT_FAILURE;

                        /*
                         * Disable all the other architectures
                         * and enable only the specified
                         */
                        memset(dec_archs, 0, sizeof(dec_archs));
                        dec_archs[values->arch_type] = 1;
                        i++;
                } else if (strcmp(argv[i], "--cipher-algo") == 0) {
                        values = check_string_arg(argv[i], argv[i+1],
                                        cipher_algo_str_map,
                                        DIM(cipher_algo_str_map));
                        if (values == NULL)
                                return EXIT_FAILURE;

                        custom_job_params.cipher_mode =
                                        values->job_params.cipher_mode;
                        custom_job_params.key_size =
                                        values->job_params.key_size;
                        custom_test = 1;
                        cipher_algo_set = 1;
                        i++;
                } else if (strcmp(argv[i], "--hash-algo") == 0) {
                        values = check_string_arg(argv[i], argv[i+1],
                                        hash_algo_str_map,
                                        DIM(hash_algo_str_map));
                        if (values == NULL)
                                return EXIT_FAILURE;

                        custom_job_params.hash_alg =
                                        values->job_params.hash_alg;
                        custom_test = 1;
                        hash_algo_set = 1;
                        i++;
                } else if (strcmp(argv[i], "--aead-algo") == 0) {
                        values = check_string_arg(argv[i], argv[i+1],
                                        aead_algo_str_map,
                                        DIM(aead_algo_str_map));
                        if (values == NULL)
                                return EXIT_FAILURE;

                        custom_job_params.cipher_mode =
                                        values->job_params.cipher_mode;
                        custom_job_params.key_size =
                                        values->job_params.key_size;
                        custom_job_params.hash_alg =
                                        values->job_params.hash_alg;
                        custom_test = 1;
                        aead_algo_set = 1;
                        i++;
                } else if (strcmp(argv[i], "--job-size") == 0) {
                        /* Try parsing the argument as a range first */
                        i = parse_range((const char * const *)argv, i, argc,
                                          job_sizes);
                        if (job_sizes[RANGE_MAX] > JOB_SIZE_TOP) {
                                fprintf(stderr,
                                       "Invalid job size %u (max %u)\n",
                                       (unsigned) job_sizes[RANGE_MAX],
                                       JOB_SIZE_TOP);
                                return EXIT_FAILURE;
                        }
                } else if (strcmp(argv[i], "--job-iter") == 0) {
                        i = get_next_num_arg((const char * const *)argv, i,
                                             argc, &job_iter, sizeof(job_iter));
                } else {
                        usage();
                        return EXIT_FAILURE;
                }

        if (custom_test) {
                if (aead_algo_set && (cipher_algo_set || hash_algo_set)) {
                        fprintf(stderr, "AEAD algorithm cannot be used "
                                        "combined with another cipher/hash "
                                        "algorithm\n");
                        return EXIT_FAILURE;
                }
        }

        if (job_sizes[RANGE_MIN] == 0) {
                fprintf(stderr, "Buffer size cannot be 0 unless only "
                                "an AEAD algorithm is tested\n");
                return EXIT_FAILURE;
        }

        if (detect_arch(arch_support) < 0)
                return EXIT_FAILURE;

        /* disable tests depending on instruction sets supported */
        for (arch_id = 0; arch_id < NUM_ARCHS; arch_id++) {
                if (arch_support[arch_id] == 0) {
                        enc_archs[arch_id] = 0;
                        dec_archs[arch_id] = 0;
                        fprintf(stderr,
                                "%s not supported. Disabling %s tests\n",
                                arch_str_map[arch_id].name,
                                arch_str_map[arch_id].name);
                }
        }

        if (enc_archs[ARCH_SSE] || dec_archs[ARCH_SSE]) {
                MB_MGR *p_mgr = alloc_mb_mgr(flags);

                if (p_mgr == NULL) {
                        fprintf(stderr, "Error allocating MB_MGR structure!\n");
                        return EXIT_FAILURE;
                }
                init_mb_mgr_sse(p_mgr);
                fprintf(stderr, "%s SHA extensions (shani) for SSE arch\n",
                        (p_mgr->features & IMB_FEATURE_SHANI) ?
                        "Using" : "Not using");
                free_mb_mgr(p_mgr);
        }

        srand(SEED);

        run_tests();

        return EXIT_SUCCESS;
}