#include <iostream>
#include <filesystem>
#include "ldr.h"

int main(int argc, char* argv[]) {
	if (argc != 2) {
		std::cerr << "Usage: " << argv[0] << " PATH_TO_MSPAINT.EXE\n";
		return EXIT_FAILURE;
	}
	char* paint_path = argv[1];

	if (!std::filesystem::exists(paint_path)) {
		std::cerr << '\'' << paint_path << "' doesn't exist\n";
		return EXIT_FAILURE;
	}

	char ext_path[MAX_PATH] = {0};
	GetFullPathNameA("paintext.dll", sizeof ext_path, ext_path, nullptr);
	if (!std::filesystem::exists(ext_path)) {
		std::cerr << "paintext.dll doesn't exist\n";
		return EXIT_FAILURE;
	}

	PROCESS_INFORMATION info;
	spawnAndLoadExtension(paint_path, "640x480.png", ext_path, info);
	CloseHandle(info.hThread);
	CloseHandle(info.hProcess);

	std::cout << "Spawned " << info.dwProcessId << '\n';

	return EXIT_SUCCESS;
}