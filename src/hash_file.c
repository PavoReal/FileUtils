#include "common/common.c"

#define FILE_BUFFER_SIZE (MEGABYTES(5))

#if !defined(HASH_SEED_VALUE)
	#define HASH_SEED_VALUE (0xbadbeef)
#endif

inline static void
print_about(const char **argv)
{
	printf("Invalid usage\n"
		"%s: file\n", argv[0]);
}

inline static bool
hash_buffer(u8 *start, size_t length, XXH64_state_t *hash_state)
{
	return (XXH64_update(hash_state, start, length) == XXH_ERROR);
}

int 
main(int argc, const char **argv)
{
	if (argc < 2)
	{
		print_about(argv);
		return 1;
	}

#if defined(TIMER)
	u64 program_start_time = read_os_timer();
	u64 timer_freq         = get_os_timer_freq();

	u64 read_time  = 0;
	u64 think_time = 0;

	u64 print_bytes_parsed = 0;
	u64 print_time_elapsed = 0;
#endif

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

	XXH64_state_t * const hash_state = XXH64_createState();
	if (hash_state == NULL)
	{
		fprintf(stderr, "(fatal: could not init Xxhash state)\n");
		return 1;
	}

	XXH64_hash_t const hash_seed = HASH_SEED_VALUE;
    if (XXH64_reset(hash_state, hash_seed) == XXH_ERROR)
    {
    	fprintf(stderr, "(fatal: failed to reset hash state to initial seed)\n");
    	return 1;
    }

	u64 file_size = get_file_size(file_handle);

#if defined(DEBUG)
	printf("(hashing %s)\n", file_path);
	printf("(file size is %lf GB)\n", ((double) file_size / (double) GIGABYTES(1)));
#endif

	u8 *buffer = (u8*) VirtualAlloc(0, FILE_BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	u64 bytes_parsed = 0;

	do
	{
#if defined(TIMER)
		u64 block_start = read_os_timer();
#endif

		DWORD bytes_read = 0;
		BOOL read_status = ReadFile(file_handle, buffer, FILE_BUFFER_SIZE, &bytes_read, NULL);

#if defined(TIMER)
		u64 read_end_time = read_os_timer();
#endif

		if (!read_status)
		{
			DWORD last_error = GetLastError();
			// https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes
			fprintf(stderr, "(fatal: could not read from file, system code %u)\n", last_error);

			break;
		}

		if (hash_buffer(buffer, bytes_read, hash_state))
		{
			fprintf(stderr, "(fatal: hashing error)\n");
			break;
		}

		bytes_parsed       += bytes_read;

#if defined(TIMER)
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
			printf("\033[2K\r(processed %lf GB (%.02lf%%), current %lf MB/s, average %lf MB/s, eta in %.00lfs, read/process ratio %.02lf)", gb_parsed, read_precent, mb_per_sec, total_speed, eta_in_sec, read_process_ratio);

			print_time_elapsed = 0;
			print_bytes_parsed = 0;
		}
#endif
	} while (bytes_parsed < file_size);

	XXH64_hash_t const hash = XXH64_digest(hash_state);

	printf("\nHASH: ");
	print_Xxh64(hash);

#if defined(TIMER)
	u64 total_time               = read_os_timer() - program_start_time;
	double total_sec             = (double) total_time   / (double) timer_freq;
	double total_file_size_in_mb = (double) file_size    / MEGABYTES(1);
	double mb_per_sec            = total_file_size_in_mb / (total_time / (double) timer_freq);

	printf("(took %lf sec @ %lf MB/s)\n", total_sec, mb_per_sec);
#endif

	VirtualFree(buffer, 0, MEM_RELEASE);
	XXH64_freeState(hash_state);
	CloseHandle(file_handle);

	return 0;
}
