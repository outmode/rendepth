// Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT
// Firefox and Chrome start executables on Windows. Launch the shared Python
// native host with inherited native-messaging pipes and wait for it to finish.
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>
#include <vector>

static std::wstring utf8(const std::string& text) {
	const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (size <= 0) return {};
	std::wstring result(size, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		text.data(), static_cast<int>(text.size()), result.data(), size);
	return result;
}

int wmain(int argc, wchar_t** argv) {
	#ifdef RENDEPTH_CHROME_NATIVE_HOST
	wchar_t executable[MAX_PATH];
	const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
	if (!length || length == MAX_PATH) {
		std::fputs("Could not locate Chrome native host executable.\n", stderr); return 1;
	}
	const auto configuration = std::filesystem::path(executable).parent_path() /
		"rendepth-chrome-host.conf";
	#else
	if (argc < 2) { std::fputs("Firefox manifest argument is missing.\n", stderr); return 1; }
	const auto configuration = std::filesystem::path(argv[1]).parent_path() /
		"rendepth-firefox-host.conf";
	#endif
	std::ifstream input(configuration, std::ios::binary);
	std::string python, host, rendepth;
	if (!std::getline(input, python) || !std::getline(input, host) ||
		!std::getline(input, rendepth)) {
		std::fputs("Browser native host configuration is missing.\n", stderr);
		return 1;
	}
	for (auto* line : {&python, &host, &rendepth})
		if (!line->empty() && line->back() == '\r') line->pop_back();
	std::wstring pythonPath = utf8(python), hostPath = utf8(host), appPath = utf8(rendepth);
	if (pythonPath.empty() || hostPath.empty() || appPath.empty()) {
		std::fputs("Browser native host configuration is invalid UTF-8.\n", stderr);
		return 1;
	}
	std::wstring command = L"\"" + pythonPath + L"\" -u \"" + hostPath +
		L"\" --rendepth \"" + appPath + L"\"";
	HANDLE inherited[3]{};
	const DWORD handles[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
	for (int index = 0; index < 3; ++index) {
		const HANDLE original = GetStdHandle(handles[index]);
		if (original == nullptr || original == INVALID_HANDLE_VALUE ||
			!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(),
				&inherited[index], 0, TRUE, DUPLICATE_SAME_ACCESS)) {
			std::fputs("Could not inherit browser native-messaging pipes.\n", stderr);
			for (HANDLE handle : inherited) if (handle) CloseHandle(handle);
			return 1;
		}
	}
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = inherited[0];
	startup.hStdOutput = inherited[1];
	startup.hStdError = inherited[2];
	PROCESS_INFORMATION process{};
	const BOOL started = CreateProcessW(pythonPath.c_str(), command.data(), nullptr,
		nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
	for (HANDLE handle : inherited) CloseHandle(handle);
	if (!started) {
		std::fprintf(stderr, "Could not start browser native host (Windows error %lu).\n", GetLastError());
		return 1;
	}
	WaitForSingleObject(process.hProcess, INFINITE);
	DWORD result = 1;
	GetExitCodeProcess(process.hProcess, &result);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return static_cast<int>(result);
}
