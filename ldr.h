#ifndef LDR_H
#define LDR_H
#include <Windows.h>

bool spawnAndLoadExtension(const char* exec, const char* cmdline,
	const char* dll_path,
	PROCESS_INFORMATION& info);
#endif
