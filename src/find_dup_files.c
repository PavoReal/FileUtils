#include "common/common.c"

#define STB_DS_IMPLEMENTATION
#include "../deps/stb/stb_ds.h"

#define FILE_BUFFER_SIZE (MEGABYTES(5))

#if !defined(HASH_SEED_VALUE)
	#define HASH_SEED_VALUE (0xbadbeef)
#endif

typedef struct 
{
	XXH64_hash_t key;
	char *value;
} file_hash_t;

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

	const char *folder_path_raw         = argv[1];
	size_t      folder_path_length_raw  = strlen(folder_path_raw);

	if (folder_path_length_raw > (MAX_PATH - 3))
	{
		fprintf(stderr, "(fatal: folder path is too long, max supported length is %d)\n", MAX_PATH - 3);
		return 1;
	}

	const char *slash_star = "\\\\*";

	char folder_path[MAX_PATH + 1];
	sz_copy_avx2(folder_path, folder_path_raw, folder_path_length_raw);
	sz_copy_avx2(folder_path + folder_path_length_raw, slash_star, strlen(slash_star) + 1);

	WIN32_FIND_DATA find_data;
	HANDLE find = FindFirstFile(folder_path, &find_data);

	if (find == INVALID_HANDLE_VALUE)
	{
		fprintf(stderr, "(fatal: FindFirstFile failed)\n");
		return 1;
	}

	u8 *buffer = (u8*) VirtualAlloc(0, FILE_BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	XXH64_state_t * const hash_state = XXH64_createState();
	if (hash_state == NULL)
	{
		fprintf(stderr, "(fatal: could not init Xxhash state)\n");
		return 1;
	}

	printf("\n");

	file_hash_t *file_hash_map = NULL;

	do
	{
		if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// Is a dir
			continue;
		}
		else
		{
			sz_copy_avx2(folder_path + folder_path_length_raw + 1, find_data.cFileName, strlen(find_data.cFileName) + 1);

			HANDLE file_handle = CreateFileA(folder_path, 
								 GENERIC_READ,
								 FILE_SHARE_READ,
								 NULL,
								 OPEN_EXISTING,
								 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
								 NULL);

			if (file_handle == INVALID_HANDLE_VALUE)
			{
				fprintf(stderr, "(fatal: could not open file %s)\n", folder_path);
				return 1;
			}

			LARGE_INTEGER _file_size;
			_file_size.LowPart  = find_data.nFileSizeLow;
			_file_size.HighPart = find_data.nFileSizeHigh;

			u64 file_size = _file_size.QuadPart;
			// printf("Found %s with size %zu bytes\n", find_data.cFileName, file_size);

			XXH64_hash_t const hash_seed = HASH_SEED_VALUE;
		    if (XXH64_reset(hash_state, hash_seed) == XXH_ERROR)
		    {
		    	fprintf(stderr, "(fatal: failed to reset hash state to initial seed)\n");
		    	return 1;
		    }

			u64 bytes_parsed = 0;

			do
			{
				DWORD bytes_read = 0;
				BOOL read_status = ReadFile(file_handle, buffer, FILE_BUFFER_SIZE, &bytes_read, NULL);

				if (!read_status)
				{
					DWORD last_error = GetLastError();
					// https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes
					fprintf(stderr, "(fatal: could not read from file, system code %u)\n", last_error);
					CloseHandle(file_handle);

					break;
				}

				if (hash_buffer(buffer, bytes_read, hash_state))
				{
					fprintf(stderr, "(fatal: hashing error)\n");
					CloseHandle(file_handle);

					break;
				}

				bytes_parsed += bytes_read;

			} while (bytes_parsed < file_size);

			XXH64_hash_t const hash = XXH64_digest(hash_state);

			char *match = hmget(file_hash_map, hash);
			if (match)
			{
				// Hash exists
				printf("DUP FOUND! %s:%s\n", match, folder_path);
			}
			else
			{
				char *entry_path = (char*) VirtualAlloc(0, MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
				sz_copy_avx2(entry_path, folder_path, MAX_PATH);

				hmput(file_hash_map, hash, entry_path);

				printf("%s: ", folder_path);
				print_Xxh64(hash);

				char *test = hmget(file_hash_map, hash);
				printf("%s\n", test);

				
			}
			
			CloseHandle(file_handle);
		}
	} while (FindNextFile(find, &find_data) != 0);

	for (u32 i = 0; i < hmlen(file_hash_map); ++i)
	{
		VirtualFree(file_hash_map[i].value, 0, MEM_RELEASE);
	}

	hmfree(file_hash_map);

	FindClose(find);
	VirtualFree(buffer, 0, MEM_RELEASE);
	XXH64_freeState(hash_state);

	return 0;
}
