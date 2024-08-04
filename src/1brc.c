#include "common/common.c"

#define STB_DS_IMPLEMENTATION
#include "../deps/stb/stb_ds.h"

#include <time.h>
#include <float.h>
#include <math.h>

#define FILE_BUFFER_SIZE (MEGABYTES(5))
#define STATION_COUNT (10000)
#define KEY_SIZE (100)

#if !defined(HASH_SEED_VALUE)
	#define HASH_SEED_VALUE (0xbadbeef)
#endif

static void
print_about(const char **argv)
{
	printf("Invalid usage\n"
		"%s: file\n", argv[0]);
}

typedef struct record {
	u32 count;
	f32 min;
	f32 max;
	f64 sum;

	char key[100];
} record_t;

inline static void
update_record(record_t *record, f32 temp)
{
	record->count += 1;
	record->sum   += temp;
	record->min    = min(record->min, temp);
	record->max    = max(record->max, temp);
}

inline static f64
get_record_avg_temp(record_t *record)
{
	f64 result = record->sum / (f64) record->count;

	return result;
}

inline static size_t 
get_record_index_from_key(char key[KEY_SIZE], XXH64_state_t * const hash_state)
{
	XXH64_hash_t const hash_seed = HASH_SEED_VALUE;
    if (XXH64_reset(hash_state, hash_seed) == XXH_ERROR)
    {
    	fprintf(stderr, "(fatal: failed to reset hash state to initial seed)\n");
    	exit(1);
    }

    XXH64_update(hash_state, key, sizeof(key));

    XXH64_hash_t const hash = XXH64_digest(hash_state);

    return (size_t) hash % STATION_COUNT;
}

inline static bool
is_char_a_number(char c)
{
	bool result = (c >= '0') && (c <= '9');

	return result;
}

int 
compare_record_t(const void *a, const void *b)
{
	record_t const *aa = a;
	record_t const *bb = b;

	return (int) sz_order(aa->key, KEY_SIZE, bb->key, KEY_SIZE);
}

size_t
handle_block(char const * const start, size_t length, 
			 char *overflow_start, size_t overflow_length,
			 record_t *records, XXH64_state_t * const hash_state)
{
	const char *newline_str   = "\n";
	const char *semicolon_str = ";";

	char const * current_pos  = start;
	size_t working_len = length;

	for (;;)
	{
		sz_cptr_t semicolon = sz_find_byte_avx2(current_pos, working_len, semicolon_str);

		if (semicolon == NULL)
		{
			break;
		}

		working_len -= (semicolon - current_pos);

		if (working_len < 4)
		{
			break;
		}

		working_len -= 1;

		sz_cptr_t key_start = sz_rfind_byte_avx2(current_pos, semicolon - current_pos, newline_str);

		if (key_start == NULL)
		{
			key_start = current_pos;
		}

		char key[KEY_SIZE] = {0};
		sz_copy_avx2(key, key_start, semicolon - current_pos);

		current_pos = semicolon + 1;

		f32 temp = 0;

		f32 sign = 1;
		if (*current_pos == '-')
		{
			sign = -1;
			++current_pos;
			--working_len;
		}

		if (working_len == 0)
		{
			break;
		}

		temp = (f32) (*current_pos++ - '0');

		if (is_char_a_number(*current_pos))
		{
			temp = (temp * 10) + (*current_pos++ - '0');
			--working_len;
		}

		if (working_len < 3)
		{
			break;
		}

		if (*current_pos++ != '.')
		{
			fprintf(stderr, "Sanity check, . in wrong spot...\n");
			exit(4);
		}

		temp += ((f32) (*current_pos++ - '0')) / 10.0f;
		temp *= sign;

		working_len -= 3;

		record_t *record = &records[get_record_index_from_key(key, hash_state)];

		update_record(record, temp);

		if (record->key[0] == 0)
		{
			sz_copy_avx2(record->key, key, KEY_SIZE);
		}

		// printf("Updating %s\n", record->key);
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

	const char *file_path    = argv[1];
	const char *results_path = argv[2];

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
	// Alloc buffer
	// The second half of the buffer is used to hold our current file read. The first half contains the dangingling
	// last line of the previous buffer
	//
	char *buffer_real     = (char*) VirtualAlloc(0, FILE_BUFFER_SIZE * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	char *buffer          = buffer_real + FILE_BUFFER_SIZE;
	char *leftover_buffer = buffer_real;

	u64 bytes_parsed = 0;

	u64 read_time  = 0;
	u64 think_time = 0;

	u64 print_bytes_parsed = 0;
	u64 print_time_elapsed = 0;

	size_t leftover_block_size = 0;

	record_t *records = (record_t*) VirtualAlloc(0, sizeof(record_t) * STATION_COUNT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	for (u32 i = 0; i < STATION_COUNT; ++i)
	{
		records[i].min = FLT_MAX;
	}

	stbds_rand_seed(time(NULL));

	XXH64_state_t * const hash_state = XXH64_createState();
	if (hash_state == NULL)
	{
		fprintf(stderr, "(fatal: could not init Xxhash state)\n");
		return 1;
	}

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

		leftover_block_size = handle_block(virtual_buffer, bytes_read + leftover_block_size, 
			                               leftover_buffer, FILE_BUFFER_SIZE,
			                               records, hash_state);

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
			printf("\033[2K\r(parsed %lf GB (%.02lf%%), current %lf MB/s, average %lf MB/s, eta in %.00lfs, read/process ratio %.02lf)", gb_parsed, read_precent, mb_per_sec, total_speed, eta_in_sec, read_process_ratio);

			print_time_elapsed = 0;
			print_bytes_parsed = 0;
		}
	} while (bytes_parsed < file_size);

	qsort(records, STATION_COUNT, sizeof(record_t), compare_record_t);

	u64 total_time               = read_os_timer() - program_start_time;
	double total_sec             = (double) total_time / (double) timer_freq;
	double total_file_size_in_mb = (double) file_size / MEGABYTES(1);
	double mb_per_sec            = total_file_size_in_mb / (total_time / (double) timer_freq);

	printf("\n");
	printf("(took %lf sec @ average of %lf MB/s)\n", total_sec, mb_per_sec);

	FILE *results_file = fopen(results_path, "wt");

	if (results_file == NULL)
	{
		fprintf(stderr, "(error opening results file: %s. defaulting to stdout)\n", results_path);
		results_file = stdout;
	}

	for (u32 i = 0; i < STATION_COUNT; ++i)
	{
		if (records[i].key[0] != 0)
		{
			fprintf(results_file, "%s;%.01lf;%.01lf;%.01lf\n", records[i].key, roundf(records[i].min * 10) / 10.0, round(get_record_avg_temp(&records[i]) * 10) / 10.0,  roundf(records[i].max * 10) / 10.0);
		}
	}

	if (results_file != stdout)
	{
		fclose(results_file);
	}

	CloseHandle(file_handle);

	VirtualFree(buffer_real, 0, MEM_RELEASE);
	VirtualFree(records, 0 , MEM_RELEASE);
	XXH64_freeState(hash_state);

	return 0;
}
