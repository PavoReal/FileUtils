inline static u64
get_file_size(HANDLE handle)
{
	DWORD file_size_upper_bits = 0;
	DWORD file_size_lower_bits = GetFileSize(handle, &file_size_upper_bits);

	u64 file_size = ((uint64_t) file_size_upper_bits << (sizeof(file_size_lower_bits) * 8)) + (uint64_t) file_size_lower_bits;

	return file_size;
}
