#include "common/common.c"

#define FILE_BUFFER_SIZE (MEGABYTES(5))
#define MAX_PHRASE_COUNT (64)

typedef struct
{
	char *phrase;
	size_t length;
} phrase_t;

static void
print_about(const char **argv)
{
	printf("Invalid usage\n"
		"%s: file <phrases>\nMin number of phrases is 1, max number is %d\n", argv[0], MAX_PHRASE_COUNT);
}

u64
count_lines_in_block(char *block, size_t block_size)
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

size_t
handle_block(char *start, size_t length, 
	         phrase_t *phrases, size_t num_phrases, 
	         u64 starting_line_count)
{
	const char *newline_str = "\n";

	for (u32 i = 0; i < num_phrases; ++i)
	{
		const char *phrase   = phrases[i].phrase;
		size_t phrase_length = phrases[i].length;

		size_t buf_remain = length;
		sz_cptr_t buf     = start;

		while (buf_remain)
		{
			sz_cptr_t phrase_start = sz_find_avx2(buf, buf_remain, phrase, phrase_length);

			if (phrase_start == NULL)
			{
				break;
			}

			sz_cptr_t line_end = sz_find_byte_avx2(phrase_start + 1, length - (phrase_start - start), newline_str);
			size_t line_length = line_end - phrase_start;

			size_t up_to_line_count = count_lines_in_block(start, line_end - buf + 1);

			printf("\nMATCH! %.*s on line %zu\n", (int) line_length - 1, phrase_start + 1, starting_line_count + up_to_line_count);
			
			buf = line_end;
			buf_remain = length - (buf - start);
		}
	}

	size_t leftover_size = 0;

	sz_cptr_t last_newline = sz_rfind_byte_avx2(start, length, newline_str);

	if (last_newline != NULL)
	{
		leftover_size = (length - (last_newline - start));
		sz_copy_avx2(start - leftover_size, last_newline, leftover_size);
	}

	return leftover_size;
}

int 
main(int argc, const char **argv)
{
	if (argc < 3 || argc > MAX_PHRASE_COUNT + 2)
	{
		print_about(argv);
		return 1;
	}

	u64 program_start_time = read_os_timer();
	u64 timer_freq         = get_os_timer_freq();

	const char *file_path  = argv[1];

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

	//
	// Calculate file size
	//

	DWORD file_size_upper_bits = 0;
	DWORD file_size_lower_bits = GetFileSize(file_handle, &file_size_upper_bits);

	u64 file_size = ((uint64_t) file_size_upper_bits << (sizeof(file_size_lower_bits) * 8)) + (uint64_t) file_size_lower_bits;

	printf("(file size is %lf GB)\n", ((double) file_size / (double) GIGABYTES(1)));

	//
	// Assemble phrases
	//
	phrase_t phrases[MAX_PHRASE_COUNT];
	u32 phrase_count = argc - 2;

	for (u32 i = 0; i < phrase_count; ++i)
	{
		const char *phrase_raw = argv[2 + i];
		phrases[i].length      = strlen(phrase_raw);

		printf("(searching for %s)\n", phrase_raw);

		phrases[i].phrase = (char*) VirtualAlloc(0, phrases[i].length + 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		sz_copy_avx2(phrases[i].phrase + 1, phrase_raw, phrases[i].length);
		phrases[i].phrase[0]                 = '\n';
		phrases[i].phrase[phrases[i].length + 1] = '\n';
		phrases[i].length += 2;
	}

	//
	// Alloc buffer
	// The second half of the buffer is the new FILE_BUFFER_SIZE from the file. The first half is handling last line
	// of the previous read.
	//
	char *buffer_real = (char*) VirtualAlloc(0, FILE_BUFFER_SIZE * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	char *buffer      = buffer_real + FILE_BUFFER_SIZE;

	u64 line_count   = 0;
	u64 bytes_parsed = 0;

	u64 print_bytes_parsed = 0;
	u64 print_time_elapsed = 0;

	size_t leftover_block_size = 0;

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

			break;
		}

		char *virtual_buffer = buffer - leftover_block_size;

		leftover_block_size = handle_block(virtual_buffer, bytes_read + leftover_block_size, phrases, phrase_count, line_count);
		line_count += count_lines_in_block(buffer, bytes_read);

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

	printf("\nsearched %zu lines\n", line_count);

	u64 total_time               = read_os_timer() - program_start_time;
	double total_sec             = (double) total_time / (double) timer_freq;
	double total_file_size_in_mb = (double) file_size / MEGABYTES(1);
	double mb_per_sec            = total_file_size_in_mb / (total_time / (double) timer_freq);

	printf("(took %lf sec @ average of %lf MB/s)\n", total_sec, mb_per_sec);

	VirtualFree(buffer_real, 0, MEM_RELEASE);

	for (u32 i = 0; i < phrase_count; ++i)
	{
		VirtualFree(phrases[i].phrase, 0, MEM_RELEASE);
	}

	CloseHandle(file_handle);

	return 0;
}
