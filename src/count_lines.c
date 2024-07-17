#include "common/common.c"

#define FILE_BUFFER_SIZE (MEGABYTES(5))

static void
print_about(const char **argv)
{
	printf("Invalid usage\n"
		"%s: file\n", argv[0]);
}

u64
handle_block(char *block, size_t block_size)
{
	const char *newline_str = "\n";

	size_t buf_remain = block_size;
	sz_cptr_t buf     = block;

	u64 newline_count = 0;

	while (buf_remain)
	{
		sz_cptr_t found_newline = sz_find_byte_avx2(buf, buf_remain, newline_str);

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

int 
main(int argc, const char **argv)
{
	if (argc < 2)
	{
		print_about(argv);
		return 1;
	}

	u64 program_start_time = read_os_timer();
	u64 timer_freq         = get_os_timer_freq();

	const char *file_path = argv[1];

	HANDLE file_handle = CreateFileA(file_path,
									 GENERIC_READ,
									 FILE_SHARE_READ,
									 NULL,
									 OPEN_EXISTING,
									 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
									 NULL);

	if (file_handle == INVALID_HANDLE_VALUE)
	{
		fprintf(stderr, "(fatal: could not open file %s)\n", file_path);
		exit(1);
	}

	DWORD file_size_upper_bits = 0;
	DWORD file_size_lower_bits = GetFileSize(file_handle, &file_size_upper_bits);

	u64 file_size = ((uint64_t) file_size_upper_bits << (sizeof(file_size_lower_bits) * 8)) + (uint64_t) file_size_lower_bits;

	printf("(file size is %lf GB)\n", ((double) file_size / (double) GIGABYTES(1)));

	char *buffer = (char*) VirtualAlloc(0, FILE_BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	u64 line_count   = 0;
	u64 bytes_parsed = 0;

	u64 print_bytes_parsed = 0;
	u64 print_time_elapsed = 0;

#if defined(DEBUG)
	u64 setup_time_elapsed = read_os_timer() - program_start_time;
	double setup_time_ms   =  (double) (setup_time_elapsed * 1000) / (double) timer_freq;

	printf("(setup took %lf ms)\n", setup_time_ms);
#endif

	do
	{
		u64 block_start = read_os_timer();

		DWORD bytes_read = 0;
		BOOL read_status = ReadFile(file_handle, buffer, FILE_BUFFER_SIZE, &bytes_read, NULL);

		if (!read_status)
		{
			DWORD last_error = GetLastError();
			// https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes
			fprintf(stderr, "(fatal: could not read from file, system code %u)\n", last_error);

			goto cleanup;
		}

		line_count += handle_block(buffer, bytes_read);

		bytes_parsed += bytes_read;

		u64 block_end     = read_os_timer();
		u64 block_elapsed = block_end - block_start;

		print_time_elapsed += block_elapsed;
		print_bytes_parsed += bytes_read;

		if (print_bytes_parsed >= MEGABYTES(50))
		{
			double mb_per_sec  = ((double) print_bytes_parsed / (double) MEGABYTES(1)) / (print_time_elapsed / (double) timer_freq);
			double total_speed = ((double) bytes_parsed / (double) MEGABYTES(1)) / ((block_end - program_start_time) / (double) timer_freq);

			printf("\033[2K\r(current speed %lf MB/s, overall average %lf MB/s)", mb_per_sec, total_speed);

			print_time_elapsed = 0;
			print_bytes_parsed = 0;
		}
	} while (bytes_parsed < file_size);

	printf("\ncounted %zu lines\n", line_count);

	u64 total_time               = read_os_timer() - program_start_time;
	double total_sec             = (double) total_time / (double) timer_freq;
	double total_file_size_in_mb = (double) file_size / MEGABYTES(1);
	double mb_per_sec            = total_file_size_in_mb / (total_time / (double) timer_freq);

	printf("(took %lf sec @ average of %lf MB/s)\n", total_sec, mb_per_sec);

cleanup:
	VirtualFree(buffer, 0, MEM_RELEASE);

	CloseHandle(file_handle);

	return 0;
}
