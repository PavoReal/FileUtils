inline static void
copy_serial(void *target, void const *source, size_t length)
{
	if (length < 1)
	{
		return;
	}

	u8 *dest      = target;
	u8 const *src = source;

	while (length--)
	{
		*(dest++) = *(src++);
	}
}

inline static void 
copy(void *target, void const *source, size_t length) 
{
	u8 *dest      = target;
	u8 const *src = source;

    for (; length >= 32; src += 32, dest += 32, length -= 32)
    {
    	_mm256_storeu_si256((__m256i *)target, _mm256_lddqu_si256((__m256i const *) src));
    }

    copy_serial(dest, src, length);
}

inline static void
clear_serial(void *target, size_t length)
{
	if (length < 1)
	{
		return;
	}

	u8 *dest = target;

	while (length--)
	{
		*(dest++) = 0;
	}
}

inline static void
clear(void *target, size_t length)
{
	u8 *dest = target;

	const __m256i zero = _mm256_set1_epi8(0);

    for (; length >= 32; dest += 32, length -= 32)
    {
    	_mm256_storeu_si256((__m256i *) target, zero);
    }

    clear_serial(dest, length);
}

inline static u64
count_byte_in_block(char *block, size_t block_size, const char *byte)
{
	size_t buf_remain = block_size;
	sz_cptr_t buf     = block;

	u64 newline_count = 0;

	while (buf_remain)
	{
		sz_cptr_t found_newline = sz_find_byte_avx2(buf, buf_remain, byte);

		if (found_newline == NULL)
		{
			break;
		}

		newline_count += 1;

		buf = found_newline + 1;
		buf_remain = block_size - (buf - block);
	}

	return newline_count;
}