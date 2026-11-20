#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#if defined(_WIN32)
#include <windows.h>
#endif

inline std::filesystem::path HostCanonical(const std::filesystem::path& path, std::error_code& error) {
#if defined(_WIN32)
    error.clear();
    auto result = std::filesystem::canonical(path, error);
    if (!error) return result;
    std::error_code absoluteError;
    const auto absolute = std::filesystem::absolute(path, absoluteError);
    if (absoluteError) return {};
    const HANDLE handle = CreateFileW(absolute.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    error.clear();
    if (length == 0 || length >= buffer.size()) return absolute.lexically_normal();
    buffer.resize(length);
    if (buffer.rfind(L"\\\\?\\UNC\\", 0) == 0) return std::filesystem::path(L"\\\\" + buffer.substr(8));
    if (buffer.rfind(L"\\\\?\\", 0) == 0) return std::filesystem::path(buffer.substr(4));
    return std::filesystem::path(buffer);
#else
    return std::filesystem::canonical(path, error);
#endif
}

inline std::filesystem::path HostCanonical(const std::filesystem::path& path) {
    std::error_code error;
    auto result = HostCanonical(path, error);
    if (error) throw std::filesystem::filesystem_error("canonical", path, error);
    return result;
}

inline std::filesystem::path HostWeaklyCanonical(const std::filesystem::path& path, std::error_code& error) {
#if defined(_WIN32)
    error.clear();
    auto result = std::filesystem::weakly_canonical(path, error);
    if (!error) return result;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) return {};
    auto head = absolute.lexically_normal();
    std::filesystem::path tail;
    while (!head.empty()) {
        std::error_code headError;
        const auto resolved = HostCanonical(head, headError);
        if (!headError) {
            error.clear();
            return tail.empty() ? resolved : (resolved / tail).lexically_normal();
        }
        if (head == head.root_path() || !head.has_relative_path()) break;
        tail = tail.empty() ? head.filename() : head.filename() / tail;
        head = head.parent_path();
    }
    error.clear();
    return absolute.lexically_normal();
#else
    return std::filesystem::weakly_canonical(path, error);
#endif
}

inline std::filesystem::path HostWeaklyCanonical(const std::filesystem::path& path) {
    std::error_code error;
    auto result = HostWeaklyCanonical(path, error);
    if (error) throw std::filesystem::filesystem_error("weakly_canonical", path, error);
    return result;
}
