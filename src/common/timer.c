inline static u64 
get_os_timer_freq()
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);

	return freq.QuadPart;
}

inline static u64 
read_os_timer()
{
	LARGE_INTEGER value;
	QueryPerformanceCounter(&value);

	return value.QuadPart;
}
