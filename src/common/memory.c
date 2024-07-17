void
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

void 
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

void
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

void
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