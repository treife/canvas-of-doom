#include <Windows.h>
#include <Windowsx.h>
#include <thread>
#include <chrono>
#include <string>
#include <filesystem>
#include <iostream>
#include <stdint.h>
#include "ldr.h"
#include "common.h"

HDC canvas = NULL;
HWND window;
void __cdecl initCanvas(HDC hdc) {
	canvas = hdc;
	// It's not unreasonable to assume that the active window will be our window
	// Because using the color picker tool requires user interaction
	window = GetActiveWindow();
}

uint32_t paint_base;
uint32_t original_get_pixel_impl; // Past the 2-byte detour
__declspec(naked) void hookGetPixel() {
	__asm {
		push eax
		mov eax, canvas
		cmp eax, 0
		// Pass control to the original function
		// If canvas has already been initialized
		// TODO: Unhook
		jnz loc_end
		mov eax, [esp+4] // Return address
		sub eax, paint_base
		sub eax, 0x2f060 // Callee: color picker
		pop eax
		jnz loc_end // The callee is not the color picker
		push [esp+4] // HDC
		call initCanvas
		add esp, 4
		loc_end:
		jmp original_get_pixel_impl
	}
}

void customShowAbout() {
	ShellAboutW(NULL, L"Canvas of DOOM", NULL, NULL);
}

const int FRAME_WIDTH = 320;
const int FRAME_HEIGHT = 240;

int viewport_x = 96;
int viewport_y = 96;

inline bool isInViewportBounds(int x, int y) noexcept {
	return x >= viewport_x && y >= viewport_y &&
	       x <= viewport_x + FRAME_WIDTH &&
	       y <= viewport_y + FRAME_HEIGHT;
}

HANDLE input_pipe = INVALID_HANDLE_VALUE;
WNDPROC original_canvas_wndproc;
LRESULT CALLBACK canvasWindowProc(HWND hwnd, UINT uMsg,
	WPARAM wParam, LPARAM lParam) {

	auto send_event = [](UINT uMsg, WPARAM wParam, LPARAM lParam) {
		uint32_t buf[3] = {uMsg, wParam, lParam};
		DWORD n_written;
		WriteFile(input_pipe, buf, 3*sizeof(uint32_t), &n_written, nullptr);
	};

	static bool dragging = false;
	static uint32_t dragging_offset_x = 0, dragging_offset_y = 0;
	uint32_t* canvas_x = (uint32_t*)(paint_base + 0xA2F2C),
	        * canvas_y = (uint32_t*)(paint_base + 0xA2F30);
	if (WM_MOUSEMOVE == uMsg) {
		// In terms of canvas (incl. non-drawable regions)
		WORD x = GET_X_LPARAM(lParam);
		WORD y = GET_Y_LPARAM(lParam);

		if (dragging) {
			viewport_x = x - dragging_offset_x;
			viewport_y = y - dragging_offset_y;
		}
		else if (isInViewportBounds(x, y)) {
			const int game_x = x - viewport_x;
			const int game_y = y - viewport_y;
			// Make sure the game receives coordinates in terms of its window
			uint32_t crafted_lparam = (uint16_t)game_x;
			crafted_lparam |= (uint16_t(game_y) << 16);
			//send_event(WM_MOUSEMOVE, wParam, crafted_lparam);

			return TRUE;
		}
	}
	else if (WM_MBUTTONDOWN == uMsg) {
		const uint32_t x = *canvas_x;
		const uint32_t y = *canvas_y;

		if (isInViewportBounds(x, y)) {
			dragging_offset_x = x - viewport_x;
			dragging_offset_y = y - viewport_y;
			dragging = true;
		}
	}
	else if (WM_MBUTTONUP == uMsg) {
		dragging = false;
		dragging_offset_x = dragging_offset_y = 0;
	}
	else if (WM_LBUTTONUP == uMsg ||
	         WM_LBUTTONDOWN == uMsg) {

		const uint32_t x = *canvas_x;
		const uint32_t y = *canvas_y;
		if (isInViewportBounds(x, y)) {
			const int game_x = x - viewport_x;
			const int game_y = y - viewport_y;
			// Make sure the game receives coordinates in terms of its window
			uint32_t crafted_lparam = (uint16_t)game_x;
			crafted_lparam |= (uint16_t(game_y) << 16);
			//send_event(uMsg, wParam, crafted_lparam);
		}
	}
	return CallWindowProc(original_canvas_wndproc, hwnd, uMsg, wParam, lParam);
}

WNDPROC original_canvas_wndproc_intermediate;
LRESULT CALLBACK canvasWindowProcIntermediate(HWND hwnd, UINT uMsg,
	WPARAM wParam, LPARAM lParam) {

	if (WM_PARENTNOTIFY == uMsg) {
		// Hook child's WNDPROC and unhook self
		POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		HWND child = RealChildWindowFromPoint(hwnd, p);
		original_canvas_wndproc = (WNDPROC)GetWindowLongW(child, GWL_WNDPROC);
		SetWindowLongW(child, GWL_WNDPROC, (LONG)canvasWindowProc);

		SetWindowLongW(hwnd, GWL_WNDPROC, (LONG)original_canvas_wndproc_intermediate);
	}
	return CallWindowProc(original_canvas_wndproc_intermediate,
		hwnd, uMsg, wParam, lParam);
}

void extMain(HINSTANCE hinstDLL) {
	AllocConsole();
	freopen("CONOUT$", "w", stdout);

	char doom_path[MAX_PATH] = {0};
	const char* REL_DOOM_PATH = "DOOM\\chocolate-doom.exe";
	SetConsoleTitle("MSPAINT");
	GetFullPathNameA(REL_DOOM_PATH, sizeof doom_path, doom_path, nullptr);
	if (!std::filesystem::exists(doom_path)) {
		MessageBoxA(NULL, "Cannot find DOOM\\chocolate-doom.exe. "
			"Make sure to place the game in the right directory.",
			"Fatal Error", MB_ICONEXCLAMATION | MB_OK);
		FreeConsole();
		FreeLibraryAndExitThread(hinstDLL, EXIT_FAILURE);
		return;
	}

	char sdlcap_path[MAX_PATH] = {0};
	GetFullPathNameA("sdlcap.dll", sizeof sdlcap_path, sdlcap_path, nullptr);
	if (!std::filesystem::exists(sdlcap_path)) {
		MessageBoxA(NULL, "Cannot find sdlcap.dll. ",
			"Fatal Error", MB_ICONEXCLAMATION | MB_OK);
		FreeConsole();
		FreeLibraryAndExitThread(hinstDLL, EXIT_FAILURE);
		return;
	}

	// The child process inherits the CWD
	// chocolate-doom.exe searches for game data in the CWD
	std::filesystem::current_path(std::filesystem::path(doom_path).parent_path());
	PROCESS_INFORMATION doom_process;
	spawnAndLoadExtension(doom_path, "", sdlcap_path, doom_process);
	CloseHandle(doom_process.hThread);
	CloseHandle(doom_process.hProcess);

	for (int i = 0; i < 10; i++) {
		input_pipe = CreateFile(
			INPUT_PIPE_NAME,
			GENERIC_WRITE,
			0,
			nullptr,
			OPEN_EXISTING,
			0,
			nullptr
		);
		if (INVALID_HANDLE_VALUE != input_pipe)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		std::cout << "Retrying CreateFile\n";
	}
	if (INVALID_HANDLE_VALUE == input_pipe) {
		printf("Pipe error: %d\n", GetLastError());
	}
	else {
		printf("Opened input pipe: %p\n", input_pipe);
	}

	HANDLE frame_pipe;
	for (int i = 0; i < 10; i++) {
		frame_pipe = CreateFile(
			FRAME_PIPE_NAME,
			GENERIC_READ,
			0,
			nullptr,
			OPEN_EXISTING,
			0,
			nullptr
		);
		if (INVALID_HANDLE_VALUE != frame_pipe)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		std::cout << "Retrying CreateFile\n";
	}
	if (INVALID_HANDLE_VALUE == frame_pipe) {
		printf("Pipe error: %d\n", GetLastError());
	}
	else {
		printf("Opened frame pipe: %p\n", frame_pipe);
	}

	DWORD pipe_mode = PIPE_READMODE_BYTE;
	SetNamedPipeHandleState(frame_pipe, &pipe_mode, nullptr, nullptr);

	const uint32_t PBAPP_SHOW_ABOUT = 0x3514;
	const uint32_t PBAPP_NEW_FILE = 0x352C;
	const uint32_t COLORPICKER_GETPIXEL = 0x1d90;

	paint_base = (uint32_t)GetModuleHandleA(NULL);

	DWORD old_perm;
	VirtualProtect((void*)(paint_base + PBAPP_SHOW_ABOUT), 4, PAGE_EXECUTE_READWRITE,
		&old_perm);
	*(uint32_t*)(paint_base + PBAPP_SHOW_ABOUT) = uint32_t(&customShowAbout);

	const uint32_t get_pixel = (uint32_t)GetProcAddress(
		GetModuleHandleW(L"gdi32.dll"), "GetPixel");
	original_get_pixel_impl = get_pixel + 2;

	VirtualProtect((void*)(get_pixel - 5), 7, PAGE_EXECUTE_READWRITE, &old_perm);
	// dst - (src + len_of_instr) -> get_pixel - 5 + 5
	const uint32_t getpixel_hook_rel = uint32_t(&hookGetPixel) - get_pixel;
	*(uint8_t*)(get_pixel - 5) = 0xE9; // far jmp
	*(uint32_t*)(get_pixel - 4) = getpixel_hook_rel;
	*(uint16_t*)get_pixel = 0xF9EB; // jmp short $-5

	// Wait until the user uses the color picker tool 
	// (i.e. we get access to the canvas)
	while (!canvas)
		std::this_thread::sleep_for(std::chrono::milliseconds(250));

	HWND canvas_parent = GetDlgItem(window, 59648);
	original_canvas_wndproc_intermediate = \
		(WNDPROC)GetWindowLongW(canvas_parent, GWL_WNDPROC);
	SetWindowLongW(canvas_parent,
		GWL_WNDPROC, (LONG)canvasWindowProcIntermediate);

	HDC frame_dc = CreateCompatibleDC(canvas);
	HBITMAP frame_bmp = NULL;
	uint8_t* frame_buf;

	BITMAPINFO bmp_info;
	auto& hdr = bmp_info.bmiHeader;
	hdr.biSize = sizeof(BITMAPINFOHEADER);
	hdr.biWidth = FRAME_WIDTH;
	hdr.biHeight = -FRAME_HEIGHT; // Top-down
	hdr.biPlanes = 1;
	hdr.biBitCount = 24;
	hdr.biCompression = BI_RGB;
	hdr.biSizeImage = 0;
	hdr.biXPelsPerMeter = hdr.biYPelsPerMeter = 2835;
	hdr.biClrUsed = hdr.biClrImportant = 0;

	frame_bmp = CreateDIBSection(frame_dc, &bmp_info, DIB_RGB_COLORS,
		(void**)&frame_buf, NULL, 0);
	SelectObject(frame_dc, frame_bmp);

	constexpr int FPS = 35;
	constexpr auto FRAME_DELTA = std::chrono::microseconds(1000000) / FPS;
	auto tm_next_frame = std::chrono::steady_clock::now();

	while (true) {
		DWORD n_read;
		BOOL success = ReadFile(frame_pipe,
			frame_buf, FRAME_WIDTH * FRAME_HEIGHT * 3,
			&n_read, nullptr);
		if (!success) {
			const DWORD err = GetLastError();
			// ERROR_MORE_DATA?
			//printf("ReadFile failed: %d\n", err);
			continue;
		}
		// printf("Received frame %dx%dx%d (%d bytes). Issuing a repaint.\n",
		// 	FRAME_WIDTH, FRAME_HEIGHT, 3, n_read);
		BitBlt(canvas,
			viewport_x, viewport_y, FRAME_WIDTH, FRAME_HEIGHT,
			frame_dc, 0, 0, SRCCOPY);
		RedrawWindow(window, nullptr, nullptr,
			RDW_UPDATENOW | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);

		// tm_next_frame += FRAME_DELTA;
		// std::this_thread::sleep_until(tm_next_frame);
	}
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	DisableThreadLibraryCalls(hinstDLL);
	if (DLL_PROCESS_ATTACH == fdwReason)
		std::thread(extMain, hinstDLL).detach();
	return TRUE;
}
