#include <Windows.h>
#include <Windowsx.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include "common.h"

using SDL_RenderReadPixels_ = int(*)(void* renderer, void* rect,
	uint32_t format, void* pixels, int pitch);
using SDL_GetRendererOutputSize_ = int(*)(void* renderer, int* w, int* h);
using SDL_RenderPresent_ = void(*)(void* renderer);
SDL_RenderReadPixels_ SDL_RenderReadPixels;
SDL_GetRendererOutputSize_ SDL_GetRendererOutputSize;
SDL_RenderPresent_ SDL_RenderPresent;

HANDLE frame_pipe;

void hookRenderPresent(void* renderer) {
	int width, height;
	SDL_GetRendererOutputSize(renderer, &width, &height);
	const int pitch = width * 3;

	const size_t frame_buf_size = width * height * 3;
	static uint8_t* frame_buf = nullptr;
	if (!frame_buf) {
		//frame_buf.resize(width * height * 3);
		frame_buf = new uint8_t[frame_buf_size];
	}

	SDL_RenderReadPixels(renderer,
		nullptr /*entire viewport*/,
		0x17401803 /*SDL_PIXELFORMAT_BGR24*/,
		frame_buf, pitch);

	// printf("Starting send... ");
	DWORD n_written;
	if (WriteFile(frame_pipe, frame_buf, frame_buf_size, &n_written, nullptr)) {
		// printf("Sent frame (%d bytes)\n", frame_buf_size);
	}

	SDL_RenderPresent(renderer);
}

#define CONSOLE_TITLE "SDL2 Capture"

BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lParam) {
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (GetCurrentProcessId() == pid) {
		char title[64] = {0};
		GetWindowTextA(hwnd, title, sizeof title);

		if (strstr(title, "DOOM Shareware - Chocolate Doom 3.0.")) {
			*(HWND*)lParam = hwnd;
			return FALSE;
		}
	}

	return TRUE;
}

WNDPROC original_window_proc;
LRESULT CALLBACK customWindowProc(HWND hwnd, UINT uMsg,
	WPARAM wParam, LPARAM lParam) {

	if (WM_MOUSELEAVE == uMsg)
		return false;

	//printf("Message: %p %p %p\n", uMsg, wParam, lParam);

	//SetWindowLongW(hwnd, GWL_WNDPROC, (LONG)original_window_proc);
	return CallWindowProc(original_window_proc, hwnd, uMsg, wParam, lParam);
}

void extMain() {
	AllocConsole();
	SetConsoleTitle(CONSOLE_TITLE);
	freopen("CONOUT$", "w", stdout);

	frame_pipe = CreateNamedPipeA(
		FRAME_PIPE_NAME,
		PIPE_ACCESS_OUTBOUND,
		PIPE_TYPE_BYTE | PIPE_WAIT,
		1,
		320*240*3,
		320*240*3,
		0,
		nullptr
	);
	if (INVALID_HANDLE_VALUE == frame_pipe) {
		printf("Pipe error: %d\n", GetLastError());
	}
	else {
		printf("Created video pipe: %p\n", frame_pipe);
	}

	HANDLE input_pipe = CreateNamedPipeA(
		INPUT_PIPE_NAME,
		PIPE_ACCESS_INBOUND,
		PIPE_TYPE_BYTE | PIPE_WAIT,
		1,
		3*sizeof(UINT), // uMsg + lParam + wParam
		3*sizeof(UINT),
		0,
		nullptr
	);
	if (INVALID_HANDLE_VALUE == input_pipe) {
		printf("Pipe error: %d\n", GetLastError());
	}
	else {
		printf("Created input pipe: %p\n", input_pipe);
	}

	DWORD pipe_mode = PIPE_READMODE_BYTE;
	SetNamedPipeHandleState(input_pipe, &pipe_mode, nullptr, nullptr);

	HMODULE sdl2 = NULL;
	while (!(sdl2 = GetModuleHandleW(L"SDL2.dll"))) {
		std::cout << "Waiting for SDL2.dll...\n";
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}

	HWND window = NULL;
	for (int i = 0; i < 10; i++) {
		EnumWindows(enum_windows_cb, (LPARAM)&window);
		if (window)
			break;
		std::cout << "Retrying EnumWindows...\n";
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}
	if (window)
		printf("Found game window: %p\n", window);
	else
		std::cout << "Error: cannot find window handle\n";

	original_window_proc = (WNDPROC)GetWindowLongW(window, GWL_WNDPROC);
	//SetWindowLongW(window, GWL_WNDPROC, (LONG)customWindowProc);

	SDL_RenderReadPixels = \
		(SDL_RenderReadPixels_)GetProcAddress(sdl2, "SDL_RenderReadPixels");
	SDL_GetRendererOutputSize = \
		(SDL_GetRendererOutputSize_)GetProcAddress(sdl2, "SDL_GetRendererOutputSize");

	const uint32_t render_present_entry = \
		(uint32_t)GetProcAddress(sdl2, "SDL_RenderPresent");
	// 675FE0B0 | FF25 54566B67            | jmp dword ptr ds:[676B5654]
	SDL_RenderPresent = (SDL_RenderPresent_)(**(uint32_t**)(render_present_entry + 2));

	// The time window to attach the debugger
	std::this_thread::sleep_for(std::chrono::seconds(3));

	DWORD old_perm;
	VirtualProtect((void*)render_present_entry, 8, PAGE_EXECUTE_READWRITE, &old_perm);
	const uint32_t jmp_dst = uint32_t(&hookRenderPresent) - (render_present_entry + 5);
	const uint64_t instr = 0x90909000000000E9 | (uint64_t(jmp_dst) << 8);
	// Atomically store the detour code to avoid crashes when some thread
	// attempts to execute partially written instructions
	// There is enough space for the NOP padding not to mess with anything important
	InterlockedExchange64((LONG64*)render_present_entry, instr);

	while (true) {
		uint32_t received[3];
		DWORD n_read;
		BOOL success = ReadFile(input_pipe,
			received, 3*sizeof(uint32_t),
			&n_read, nullptr);
		if (!success) {
			const DWORD err = GetLastError();
			// ERROR_MORE_DATA?
			//printf("ReadFile failed: %d\n", err);
			continue;
		}
		
		const UINT u_msg = received[0];
		const WPARAM w_param = received[1];
		const LPARAM l_param = received[2];
		//printf("Received input event: %x | %x | %x\n", u_msg, l_param, w_param);

		WNDPROC wndproc = (WNDPROC)GetWindowLongW(window, GWL_WNDPROC);
		if (WM_MOUSEMOVE == u_msg)
			SendMessage(window, u_msg, w_param, l_param);
		else if (WM_LBUTTONDOWN == u_msg || WM_LBUTTONUP == u_msg)
			SendMessage(window, u_msg, w_param, l_param);
	}
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	DisableThreadLibraryCalls(hinstDLL);
	if (DLL_PROCESS_ATTACH == fdwReason)
		std::thread(extMain).detach();
	return TRUE;
}