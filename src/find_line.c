#include "common/common.c"

#define FILE_BUFFER_SIZE (MEGABYTES(5))

typedef struct
{
	char *phrase;
	size_t length;
} phrase_t;

static void
print_about(const char **argv)
{
	printf("Invalid usage\n"
		"%s: file <phrases>\nMust supply at least one phrase", argv[0]);
}

size_t
handle_block_match_whole_line(char *start, size_t length, 
			 char *overflow_start, size_t overflow_length,
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

			size_t up_to_line_count = count_byte_in_block(start, line_end - buf + 1, newline_str);

			printf("\nMATCH! '%.*s' on line %zu\n", (int) line_length - 1, phrase_start + 1, starting_line_count + up_to_line_count);
			
			buf = line_end;
			buf_remain = length - (buf - start);
		}
	}

	size_t leftover_size = 0;

	sz_cptr_t last_newline = sz_rfind_byte_avx2(start, length, newline_str);

	if (last_newline != NULL)
	{
		leftover_size = (start + length) - last_newline;
		leftover_size = leftover_size > overflow_length ? overflow_length : leftover_size;

		sz_copy_avx2(overflow_start + overflow_length - leftover_size, last_newline, leftover_size);
	}

	return leftover_size;
}

int 
main(int argc, const char **argv)
{
	if (argc < 3)
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
		return 1;
	}
	printf("(searching %s)\n", file_path);

	u64 file_size = get_file_size(file_handle);
	printf("(file size is %lf GB)\n", ((double) file_size / (double) GIGABYTES(1)));

	//
	// Assemble phrases
	//
	size_t phrase_count = argc - 2;
	phrase_t *phrases   = (phrase_t*) VirtualAlloc(0, sizeof(phrase_t) * phrase_count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	for (u32 i = 0; i < phrase_count; ++i)
	{
		const char *phrase_raw = argv[2 + i];
		phrases[i].length      = strlen(phrase_raw);

		printf("(searching for %s)\n", phrase_raw);

		phrases[i].phrase = (char*) VirtualAlloc(0, phrases[i].length + 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		sz_copy_avx2(phrases[i].phrase + 1, phrase_raw, phrases[i].length);

		phrases[i].phrase[0]                     = '\n';
		phrases[i].phrase[phrases[i].length + 1] = '\n';

		phrases[i].length += 2;
	}

	//
	// Alloc buffer
	// The second half of the buffer is used to hold our current file read. The first half contains the dangingling
	// last line of the previous buffer
	//
	char *buffer_real     = (char*) VirtualAlloc(0, FILE_BUFFER_SIZE * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	char *buffer          = buffer_real + FILE_BUFFER_SIZE;
	char *leftover_buffer = buffer_real;

	const char *newline_str = "\n";

	u64 line_count   = 0;
	u64 bytes_parsed = 0;

	u64 read_time  = 0;
	u64 think_time = 0;

	u64 print_bytes_parsed = 0;
	u64 print_time_elapsed = 0;

	size_t leftover_block_size = 0;

	do
	{
		u64 block_start = read_os_timer();

		DWORD bytes_read = 0;
		BOOL read_status = ReadFile(file_handle, buffer, FILE_BUFFER_SIZE, &bytes_read, NULL);

		u64 read_end_time = read_os_timer();

		if (!read_status)
		{
			DWORD last_error = GetLastError();
			// https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes
			fprintf(stderr, "(fatal: could not read from file, system code %u)\n", last_error);

			break;
		}

		char *virtual_buffer = buffer - leftover_block_size;

		leftover_block_size = handle_block_match_whole_line(virtual_buffer, bytes_read + leftover_block_size, 
			                                                leftover_buffer, FILE_BUFFER_SIZE,
			                                                phrases, phrase_count, 
			                                                line_count);

		line_count += count_byte_in_block(buffer, bytes_read, newline_str);

		bytes_parsed       += bytes_read;
		print_bytes_parsed += bytes_read;

		u64 block_end     = read_os_timer();
		u64 block_elapsed = block_end - block_start;

		print_time_elapsed += block_elapsed;
		if (print_time_elapsed >= (timer_freq / 5))
		{
			double mb_per_sec  = ((double) print_bytes_parsed / (double) MEGABYTES(1)) / (print_time_elapsed               / (double) timer_freq);
			double total_speed = ((double) bytes_parsed       / (double) MEGABYTES(1)) / ((block_end - program_start_time) / (double) timer_freq);

			double eta_in_sec  = ((double) (file_size - bytes_parsed) / (double) MEGABYTES(1)) / mb_per_sec;

			double gb_parsed = ((double) bytes_parsed / (double) GIGABYTES(1));
			double read_precent = ((double) bytes_parsed / (double) file_size) * 100;

			u64 read_elpased = (read_end_time - block_start);

			read_time  += read_elpased;
			think_time += (block_elapsed - read_elpased);

			double read_process_ratio = ((double) read_time / (double) think_time);

			// printf("\033[2K\r(searched %lf GB, current speed %lf MB/s, overall average %lf MB/s, eta in %.02lfs)", ((double) bytes_parsed / (double) GIGABYTES(1)), mb_per_sec, total_speed, eta_in_sec);
			printf("\033[2K\r(searched %lf GB (%.02lf%%), current %lf MB/s, average %lf MB/s, eta in %.00lfs, read/process ratio %.02lf)", gb_parsed, read_precent, mb_per_sec, total_speed, eta_in_sec, read_process_ratio);

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
	VirtualFree(phrases, 0, MEM_RELEASE);

	CloseHandle(file_handle);

	return 0;
}
