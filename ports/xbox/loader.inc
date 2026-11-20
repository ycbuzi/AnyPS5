using PayloadMain = int(__cdecl*)(const wchar_t*);

HMODULE ResolveModule(const char* name) {
    wchar_t wide[260]{};
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 259);
    if (HMODULE module = GetModuleHandleW(wide)) return module;
    if (HMODULE module = LoadPackagedLibrary(wide, 0)) return module;
    using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
    static auto loadLibrary = reinterpret_cast<LoadLibraryExWFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "LoadLibraryExW"));
    return loadLibrary ? loadLibrary(wide, nullptr, 0) : nullptr;
}

std::string RunPayload(const std::wstring& folder) {
    const std::wstring path = folder + L"\\payload.dll";
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return "\"no payload\"";
    std::string image;
    char chunk[65536];
    for (size_t got; (got = std::fread(chunk, 1, sizeof chunk, file)) != 0;) image.append(chunk, got);
    std::fclose(file);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    if (image.size() < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE) return "\"bad dos header\"";
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return "\"bad nt header\"";
    const SIZE_T size = nt->OptionalHeader.SizeOfImage;
    auto* base = static_cast<unsigned char*>(VirtualAllocFromApp(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!base) return "\"alloc failed " + std::to_string(GetLastError()) + "\"";
    std::memcpy(base, image.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (section->SizeOfRawData) std::memcpy(base + section->VirtualAddress, image.data() + section->PointerToRawData, section->SizeOfRawData);
    }
    const auto& directories = nt->OptionalHeader.DataDirectory;
    const auto delta = reinterpret_cast<std::uint64_t>(base) - nt->OptionalHeader.ImageBase;
    const auto& relocs = directories[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta && relocs.Size) {
        auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(base + relocs.VirtualAddress);
        auto* end = reinterpret_cast<unsigned char*>(block) + relocs.Size;
        while (reinterpret_cast<unsigned char*>(block) < end && block->SizeOfBlock) {
            const auto* entries = reinterpret_cast<const WORD*>(block + 1);
            const size_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            for (size_t i = 0; i < count; ++i) {
                if ((entries[i] >> 12) == IMAGE_REL_BASED_DIR64) *reinterpret_cast<std::uint64_t*>(base + block->VirtualAddress + (entries[i] & 0xFFF)) += delta;
            }
            block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<unsigned char*>(block) + block->SizeOfBlock);
        }
    }
    std::string missing;
    const auto& imports = directories[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.Size) {
        for (auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); descriptor->Name; ++descriptor) {
            const char* dll = reinterpret_cast<const char*>(base + descriptor->Name);
            HMODULE module = ResolveModule(dll);
            auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
            const auto* lookup = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk : descriptor->FirstThunk));
            for (; lookup->u1.AddressOfData; ++lookup, ++thunk) {
                FARPROC proc = nullptr;
                std::string what;
                if (IMAGE_SNAP_BY_ORDINAL64(lookup->u1.Ordinal)) {
                    what = "#" + std::to_string(IMAGE_ORDINAL64(lookup->u1.Ordinal));
                    if (module) proc = GetProcAddress(module, reinterpret_cast<LPCSTR>(IMAGE_ORDINAL64(lookup->u1.Ordinal)));
                } else {
                    what = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + lookup->u1.AddressOfData)->Name;
                    if (module) proc = GetProcAddress(module, what.c_str());
                }
                if (!proc) missing += (missing.empty() ? "" : ",") + std::string(dll) + "!" + what;
                thunk->u1.Function = reinterpret_cast<std::uint64_t>(proc);
            }
        }
    }
    if (!missing.empty()) return "{\"missing_imports\":\"" + missing + "\"}";
    section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const DWORD c = section->Characteristics;
        ULONG protect = PAGE_READONLY;
        if (c & IMAGE_SCN_MEM_EXECUTE) protect = (c & IMAGE_SCN_MEM_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
        else if (c & IMAGE_SCN_MEM_WRITE) protect = PAGE_READWRITE;
        ULONG old = 0;
        const SIZE_T length = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
        if (length && !VirtualProtectFromApp(base + section->VirtualAddress, length, protect, &old)) return "\"protect failed " + std::to_string(GetLastError()) + "\"";
    }
    const auto& exceptions = directories[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exceptions.Size) {
        RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(base + exceptions.VirtualAddress), exceptions.Size / sizeof(RUNTIME_FUNCTION), reinterpret_cast<DWORD64>(base));
    }
    using DllMainFn = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
    if (nt->OptionalHeader.AddressOfEntryPoint) {
        auto entry = reinterpret_cast<DllMainFn>(base + nt->OptionalHeader.AddressOfEntryPoint);
        if (!entry(reinterpret_cast<HINSTANCE>(base), DLL_PROCESS_ATTACH, nullptr)) return "\"dllmain failed\"";
    }
    const auto& exports = directories[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exports.Size) return "\"no exports\"";
    const auto* table = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exports.VirtualAddress);
    const auto* names = reinterpret_cast<const DWORD*>(base + table->AddressOfNames);
    const auto* ordinals = reinterpret_cast<const WORD*>(base + table->AddressOfNameOrdinals);
    const auto* functions = reinterpret_cast<const DWORD*>(base + table->AddressOfFunctions);
    for (DWORD i = 0; i < table->NumberOfNames; ++i) {
        if (std::strcmp(reinterpret_cast<const char*>(base + names[i]), "PayloadMain") == 0) {
            auto main = reinterpret_cast<PayloadMain>(base + functions[ordinals[i]]);
            return "{\"result\":" + std::to_string(main(folder.c_str())) + "}";
        }
    }
    return "\"no PayloadMain\"";
}

void WritePayloadReport() {
    const std::wstring folder = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path());
    std::string result;
    try {
        result = RunPayload(folder);
    } catch (...) {
        result = "\"payload threw\"";
    }
    const std::wstring path = folder + L"\\loader.json";
    if (FILE* f = _wfopen(path.c_str(), L"wb")) {
        std::fwrite(result.data(), 1, result.size(), f);
        std::fclose(f);
    }
}

