#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.storage.h>
#include <cstdio>
#include <cstring>
#include <string>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
namespace AF = ABI::Windows::Foundation;
namespace AFC = ABI::Windows::Foundation::Collections;
namespace AS = ABI::Windows::Storage;

namespace {

std::wstring g_folder;
std::string g_json = "{";
std::string g_names;

std::string Utf8(const wchar_t* w, UINT32 n) {
    if (!n) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), s.data(), len, nullptr, nullptr);
    return s;
}

void Save() {
    if (FILE* f = _wfopen((g_folder + L"\\storage.json").c_str(), L"wb")) {
        const std::string closed = g_json + "\"end\":1}";
        std::fwrite(closed.data(), 1, closed.size(), f);
        std::fclose(f);
    }
    if (FILE* f = _wfopen((g_folder + L"\\storage-names.txt").c_str(), L"wb")) {
        std::fwrite(g_names.data(), 1, g_names.size(), f);
        std::fclose(f);
    }
}

std::string Hr(HRESULT hr) {
    char b[24];
    std::snprintf(b, sizeof b, "\"hr 0x%08lx\"", static_cast<unsigned long>(hr));
    return b;
}

void Put(const std::string& key, const std::string& value) {
    std::string k;
    for (char c : key) k += (c == '\\' || c == '"') ? '_' : c;
    g_json += "\"" + k + "\":" + value + ",";
    Save();
}

template <typename Op>
HRESULT Wait(Op* op, DWORD ms) {
    ComPtr<AF::IAsyncInfo> info;
    HRESULT hr = op->QueryInterface(IID_PPV_ARGS(&info));
    if (FAILED(hr)) return hr;
    const ULONGLONG end = GetTickCount64() + ms;
    AsyncStatus status = AsyncStatus::Started;
    while (SUCCEEDED(info->get_Status(&status)) && status == AsyncStatus::Started) {
        if (GetTickCount64() > end) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        Sleep(20);
    }
    if (status != AsyncStatus::Completed) {
        HRESULT error = E_FAIL;
        info->get_ErrorCode(&error);
        return error;
    }
    return S_OK;
}

std::string ListFolder(const std::string& label, AS::IStorageFolder* folder) {
    ComPtr<AF::IAsyncOperation<AFC::IVectorView<AS::IStorageItem*>*>> op;
    HRESULT hr = folder->GetItemsAsyncOverloadDefaultStartAndCount(&op);
    if (SUCCEEDED(hr)) hr = Wait(op.Get(), 10000);
    if (FAILED(hr)) return "{\"open\":true,\"list\":" + Hr(hr) + "}";
    ComPtr<AFC::IVectorView<AS::IStorageItem*>> items;
    op->GetResults(&items);
    unsigned n = 0;
    if (items) items->get_Size(&n);
    g_names += "== " + label + "\n";
    for (unsigned i = 0; i < n && i < 200; ++i) {
        ComPtr<AS::IStorageItem> item;
        if (FAILED(items->GetAt(i, &item))) continue;
        HSTRING name = nullptr;
        item->get_Name(&name);
        UINT32 len = 0;
        const wchar_t* raw = WindowsGetStringRawBuffer(name, &len);
        g_names += Utf8(raw, len) + "\n";
        WindowsDeleteString(name);
    }
    return "{\"open\":true,\"items\":" + std::to_string(n) + "}";
}

void ByPath(AS::IStorageFolderStatics* statics, const wchar_t* path) {
    char label[256];
    std::snprintf(label, sizeof label, "%ls", path);
    ComPtr<AF::IAsyncOperation<AS::StorageFolder*>> op;
    HRESULT hr = statics->GetFolderFromPathAsync(HStringReference(path).Get(), &op);
    if (SUCCEEDED(hr)) hr = Wait(op.Get(), 10000);
    if (FAILED(hr)) return Put(label, Hr(hr));
    ComPtr<AS::IStorageFolder> folder;
    op->GetResults(&folder);
    Put(label, folder ? ListFolder(label, folder.Get()) : "\"null\"");
}

void Known(AS::IKnownFoldersStatics* known, const char* label, HRESULT (STDMETHODCALLTYPE AS::IKnownFoldersStatics::*getter)(AS::IStorageFolder**)) {
    ComPtr<AS::IStorageFolder> folder;
    const HRESULT hr = (known->*getter)(&folder);
    if (FAILED(hr) || !folder) return Put(std::string("known_") + label, Hr(hr));
    Put(std::string("known_") + label, ListFolder(label, folder.Get()));
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    g_folder = folder;
    RoInitialize(RO_INIT_MULTITHREADED);
    Put("started", "1");
    ComPtr<AS::IStorageFolderStatics> statics;
    HRESULT hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_StorageFolder).Get(), IID_PPV_ARGS(&statics));
    if (FAILED(hr)) {
        Put("statics", Hr(hr));
    } else {
        for (const wchar_t* path : {L"C:\\", L"C:\\Windows", L"C:\\Windows\\System32", L"D:\\", L"D:\\Drives", L"E:\\", L"F:\\", L"G:\\", L"J:\\", L"K:\\", L"M:\\", L"N:\\", L"O:\\", L"P:\\", L"Q:\\", L"S:\\", L"T:\\", L"U:\\", L"U:\\Users", L"V:\\", L"X:\\", L"Y:\\"}) {
            ByPath(statics.Get(), path);
        }
    }
    ComPtr<AS::IKnownFoldersStatics> known;
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_KnownFolders).Get(), IID_PPV_ARGS(&known));
    if (FAILED(hr)) {
        Put("known", Hr(hr));
    } else {
        Known(known.Get(), "documents", &AS::IKnownFoldersStatics::get_DocumentsLibrary);
        Known(known.Get(), "pictures", &AS::IKnownFoldersStatics::get_PicturesLibrary);
        Known(known.Get(), "videos", &AS::IKnownFoldersStatics::get_VideosLibrary);
        Known(known.Get(), "music", &AS::IKnownFoldersStatics::get_MusicLibrary);
        Known(known.Get(), "removable", &AS::IKnownFoldersStatics::get_RemovableDevices);
    }
    const std::string done = "{\"storage\":\"storage.json\"}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
