#define XXH_INLINE_ALL
#include "../deps/xxHash/xxhash.h"

inline static void 
print_Xxh64(XXH64_hash_t hash)
{
    XXH64_canonical_t cano;
    XXH64_canonicalFromHash(&cano, hash);

    for(size_t i = 0; i < sizeof(cano.digest); ++i) 
    {
        printf("%02x", cano.digest[i]);
    }

    printf("\n");
}
