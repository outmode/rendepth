#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <string>

namespace WindowsCalibration {
using Microsoft::WRL::ComPtr;

inline std::wstring name(IShellItem* item) {
	PWSTR text = nullptr;
	if (FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &text))) return {};
	std::wstring result(text);
	CoTaskMemFree(text);
	return result;
}

inline ComPtr<IShellItem> child(IShellItem* folder, const wchar_t* wanted) {
	ComPtr<IEnumShellItems> items;
	if (FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) return {};
	ComPtr<IShellItem> item;
	while (items->Next(1, item.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
		if (_wcsicmp(name(item.Get()).c_str(), wanted) == 0) return item;
	}
	return {};
}

inline bool read(IShellItem* item, std::string& text) {
	ComPtr<IBindCtx> binding;
	if (FAILED(CreateBindCtx(0, &binding))) return false;
	BIND_OPTS options{};
	options.cbStruct = sizeof(options);
	options.grfMode = STGM_READ;
	if (FAILED(binding->SetBindOptions(&options))) return false;
	ComPtr<IStream> stream;
	if (FAILED(item->BindToHandler(binding.Get(), BHID_Stream, IID_PPV_ARGS(&stream)))) return false;
	std::string result;
	char buffer[4096];
	for (;;) {
		ULONG count = 0;
		const HRESULT status = stream->Read(buffer, sizeof(buffer), &count);
		if (FAILED(status) || result.size() + count > 64 * 1024) return false;
		result.append(buffer, count);
		if (count == 0 || status == S_FALSE) break;
	}
	if (result.empty()) return false;
	text = std::move(result);
	return true;
}

// Portable devices appear in Explorer's shell namespace, not std::filesystem.
// Only inspect Looking Glass devices and their calibration folder, never media.
inline bool load(std::string& text) {
	const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
	struct Apartment {
		bool owned;
		~Apartment() { if (owned) CoUninitialize(); }
	} apartment{SUCCEEDED(initialized)};
	ComPtr<IShellItem> computer;
	if (FAILED(SHGetKnownFolderItem(FOLDERID_ComputerFolder, KF_FLAG_DEFAULT,
		nullptr, IID_PPV_ARGS(&computer)))) return false;
	ComPtr<IEnumShellItems> devices;
	if (FAILED(computer->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&devices)))) return false;
	ComPtr<IShellItem> device;
	while (devices->Next(1, device.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
		auto deviceName = name(device.Get());
		std::transform(deviceName.begin(), deviceName.end(), deviceName.begin(),
			[](wchar_t c) { return std::towlower(c); });
		if (deviceName.find(L"looking glass") == std::wstring::npos &&
			deviceName.find(L"lkg") == std::wstring::npos) continue;
		SFGAOF attributes = 0;
		if (FAILED(device->GetAttributes(SFGAO_FILESYSTEM, &attributes)) ||
			(attributes & SFGAO_FILESYSTEM)) continue;
		ComPtr<IEnumShellItems> storages;
		if (FAILED(device->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&storages)))) continue;
		ComPtr<IShellItem> storage;
		while (storages->Next(1, storage.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
			auto folder = child(storage.Get(), L"LKG_calibration");
			if (!folder) continue;
			auto file = child(folder.Get(), L"visual.json");
			if (file && read(file.Get(), text)) return true;
		}
	}
	return false;
}
}
#endif
