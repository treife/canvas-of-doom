#include "ldr.h"
#include <string>
#include <format>
#include <iostream>

bool spawnAndLoadExtension(const char* exec, const char* cmdline,
	const char* dll_path,
	PROCESS_INFORMATION& info) {

	std::string s_cmdline = std::format("\"{}\" {}", exec, cmdline);
	STARTUPINFO si = {0};
	si.cb = sizeof si;
	constexpr auto FLAGS = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
	if (!CreateProcessA(exec, s_cmdline.data(), nullptr, nullptr,
		false, FLAGS, nullptr, nullptr, &si, &info)) {

		std::cerr << "CreateProcessA error: " << GetLastError() << '\n';
		return false;
	}

	const size_t path_len = strlen(dll_path);
	void* const dll_path_space = VirtualAllocEx(info.hProcess, nullptr,
		path_len + 1 /*+NULL*/, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	WriteProcessMemory(info.hProcess, dll_path_space, dll_path, path_len + 1,
		nullptr);

	const auto load_library_a = (LPTHREAD_START_ROUTINE)GetProcAddress(
		GetModuleHandleW(L"kernel32.dll"), "LoadLibraryA");
	HANDLE thread = CreateRemoteThread(info.hProcess, nullptr, 0,
		load_library_a, dll_path_space, 0, nullptr);
	if (WAIT_OBJECT_0 != WaitForSingleObject(thread, 2000)) {
		std::cerr << "WaitForSingleObject on loader thread failed\n";
		return false;
	}
	VirtualFreeEx(info.hProcess, dll_path_space, 0, MEM_RELEASE);

	using NtResumeProcess_ = LONG(NTAPI*)(HANDLE ProcessHandle);
	auto nt_resume_process = (NtResumeProcess_)GetProcAddress(
		GetModuleHandleW(L"ntdll.dll"), "NtResumeProcess");
	nt_resume_process(info.hProcess);

	return true;
}