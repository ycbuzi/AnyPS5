#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/File/include/SharedMemory.hpp"
#include "nid/Sha1.hpp"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <barrier>

extern "C" {
int APS5_VABI shm_open_nid_postfix(const char*, int, int);
int APS5_VABI shm_unlink_nid_postfix(const char*);
int APS5_VABI close_nid_postfix(int);
int APS5_VABI dup_nid_postfix(int);
int APS5_VABI dup2_nid_postfix(int, int);
std::int64_t APS5_VABI lseek_nid_postfix(int, std::int64_t, int);
int APS5_VABI ftruncate_nid_postfix(int, std::int64_t);
int APS5_VABI fchmod_nid_postfix(int, int);
std::int64_t APS5_VABI pread_nid_postfix(int, void*, std::size_t, std::int64_t);
std::int64_t APS5_VABI pwrite_nid_disambig1_nid_postfix(int, const void*, std::size_t, std::int64_t);
int* APS5_VABI __error_nid_postfix();
}

namespace {
constexpr int ReadOnly = 0;
constexpr int ReadWrite = 2;
constexpr int Create = 0x200;
constexpr int Exclusive = 0x800;
constexpr std::int64_t RaceSize = 4096;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string(message) + " (guest errno=" + std::to_string(*__error_nid_postfix()) + ")");
}

void RequireError(int descriptor, int expected, const char* message) {
    Require(descriptor == -1 && *__error_nid_postfix() == expected, message);
}

void RequireNoUnlinkedFiles(const std::string& name) {
    const auto digest = Nid::Sha1(std::vector<std::uint8_t>(name.begin(), name.end()));
    std::string prefix;
    for (const auto byte : digest) {
        prefix += "0123456789abcdef"[byte >> 4];
        prefix += "0123456789abcdef"[byte & 15];
    }
    const auto directory = std::filesystem::temp_directory_path() / L"AnyPS5" / L"shm";
    if (!std::filesystem::exists(directory)) return;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto filename = entry.path().filename().string();
        Require(!filename.starts_with(prefix + "-unlinked-"),
                "closing the last descriptor did not reap its unlinked host object");
    }
}

void VerifyUntrackedDescriptor(int descriptor) {
    Require(File::IsSharedMemoryDescriptor_nid_no_patch(descriptor), "named descriptor was not tracked");
    std::vector<wchar_t> path(32768);
    const DWORD length = ::GetFinalPathNameByHandleW(reinterpret_cast<HANDLE>(::_get_osfhandle(descriptor)),
        path.data(), static_cast<DWORD>(path.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    Require(length > 0 && length < path.size(), "locating the tracked file failed");
    const HANDLE handle = ::CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(handle != INVALID_HANDLE_VALUE, "opening an untracked handle to the named file failed");
    const int untracked = ::_open_osfhandle(reinterpret_cast<std::intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (untracked < 0) ::CloseHandle(handle);
    Require(untracked >= 0, "creating an untracked descriptor failed");
    const bool classified = File::IsSharedMemoryDescriptor_nid_no_patch(untracked);
    Require(::_close(untracked) == 0, "closing the untracked descriptor failed");
    Require(!classified, "an untracked descriptor entered the named shared-memory path");
    Require(!File::IsSharedMemoryDescriptor_nid_no_patch(untracked), "a closed untracked descriptor was classified as shared memory");
}

void VerifyConcurrentDescriptorReuse(const std::string& name) {
    std::barrier start(3);
    std::vector<int> okay(2, 1);
    std::vector<std::thread> workers;
    for (int index = 0; index < 2; ++index) {
        workers.emplace_back([&, index] {
            start.arrive_and_wait();
            for (int iteration = 0; iteration < 64; ++iteration) {
                const int descriptor = shm_open_nid_postfix(name.c_str(), ReadWrite, 0);
                if (descriptor < 0) { okay[index] = 0; break; }
                if (!File::IsSharedMemoryDescriptor_nid_no_patch(descriptor) ||
                    pwrite_nid_disambig1_nid_postfix(descriptor, "x", 1, 64) != 0) okay[index] = 0;
                if (close_nid_postfix(descriptor) != 0) okay[index] = 0;
            }
        });
    }
    start.arrive_and_wait();
    for (auto& worker : workers) worker.join();
    Require(okay[0] && okay[1], "concurrent close/open lost named-descriptor tracking or bounded writes");
}

bool RunResizeWriteRace(const std::string& name) {
    const int descriptor = shm_open_nid_postfix(name.c_str(), ReadWrite, 0);
    if (descriptor < 0) return false;
    bool okay = true;
    for (int iteration = 0; iteration < 300; ++iteration) {
        const auto size = (iteration & 1) ? 0 : RaceSize;
        if (ftruncate_nid_postfix(descriptor, size) != 0) { okay = false; break; }
        if (pwrite_nid_disambig1_nid_postfix(descriptor, "x", 1, RaceSize / 2) < 0) { okay = false; break; }
        struct _stat64 status{};
        if (::_fstat64(descriptor, &status) != 0 || (status.st_size != 0 && status.st_size != RaceSize)) {
            okay = false;
            break;
        }
    }
    close_nid_postfix(descriptor);
    return okay;
}

void VerifyCrossProcessResizeWrite(const std::string& name, int descriptor) {
    wchar_t executable[32768]{};
    const DWORD length = ::GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    Require(length > 0 && length < std::size(executable), "locating the Windows shared-memory test executable failed");
    std::vector<PROCESS_INFORMATION> workers;
    for (int index = 0; index < 2; ++index) {
        std::wstring command = L"\"" + std::wstring(executable, length) + L"\" --race " + std::wstring(name.begin(), name.end());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        Require(::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                 nullptr, nullptr, &startup, &process) != FALSE,
                "starting a cross-process shared-memory race worker failed");
        workers.push_back(process);
    }
    const bool localSucceeded = RunResizeWriteRace(name);
    bool childrenSucceeded = true;
    for (const auto& process : workers) {
        childrenSucceeded = childrenSucceeded && ::WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0;
        DWORD exitCode = 1;
        childrenSucceeded = childrenSucceeded && ::GetExitCodeProcess(process.hProcess, &exitCode) && exitCode == 0;
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
    }
    Require(localSucceeded && childrenSucceeded,
            "cross-process truncate/write race produced an invalid shared-object size");
    struct _stat64 status{};
    Require(::_fstat64(descriptor, &status) == 0 && (status.st_size == 0 || status.st_size == RaceSize),
            "cross-process truncate/write race left an invalid final size");
    Require(shm_unlink_nid_postfix(name.c_str()) == 0, "cross-process race cleanup failed");
}

void VerifyResizeReadPosition(const std::string& name) {
    constexpr std::size_t payloadSize = 2u << 20;
    const int descriptor = shm_open_nid_postfix(name.c_str(), ReadWrite | Create | Exclusive, 0600);
    Require(descriptor >= 0 && ftruncate_nid_postfix(descriptor, payloadSize) == 0,
            "creating the resize/read object failed");
    const int resizer = dup_nid_postfix(descriptor);
    Require(resizer >= 0, "duplicating the resize/read descriptor failed");
    std::vector<char> payload(payloadSize, 'r');
    Require(pwrite_nid_disambig1_nid_postfix(descriptor, payload.data(), payload.size(), 0) == payloadSize &&
            lseek_nid_postfix(descriptor, 137, 0) == 137, "preparing the resize/read object failed");
    std::barrier start(2);
    bool resized = true;
    std::thread resizing([&] {
        start.arrive_and_wait();
        for (int iteration = 0; iteration < 256; ++iteration)
            if (ftruncate_nid_postfix(resizer, payloadSize + ((iteration & 1) ? 4096 : 0)) != 0) resized = false;
    });
    start.arrive_and_wait();
    std::vector<char> output(payloadSize);
    bool read = true;
    for (int iteration = 0; iteration < 32; ++iteration)
        if (pread_nid_postfix(descriptor, output.data(), output.size(), 0) != payloadSize || output != payload) read = false;
    resizing.join();
    Require(resized && read && lseek_nid_postfix(descriptor, 0, 1) == 137,
            "concurrent resize/read changed the shared file position or contents");
    Require(shm_unlink_nid_postfix(name.c_str()) == 0 && close_nid_postfix(descriptor) == 0 && close_nid_postfix(resizer) == 0,
            "resize/read cleanup failed");
    RequireNoUnlinkedFiles(name);
}

void Run() {
    const std::string base = "/anyps5-windows-named-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(::GetTickCount64());
    const std::string name = base + "-lifecycle";
    Require(shm_unlink_nid_postfix(name.c_str()) == -1 && *__error_nid_postfix() == 2,
            "new test name unexpectedly existed");

    *__error_nid_postfix() = 73;
    const int original = shm_open_nid_postfix(name.c_str(), ReadWrite | Create | Exclusive, 0600);
    Require(original >= 0 && *__error_nid_postfix() == 73, "exclusive creation failed or changed guest errno");
    VerifyUntrackedDescriptor(original);
    Require(ftruncate_nid_postfix(original, 64) == 0, "sizing the named object failed");
    VerifyConcurrentDescriptorReuse(name);
    const char originalBytes[] = "original";
    Require(pwrite_nid_disambig1_nid_postfix(original, originalBytes, sizeof(originalBytes), 0) == sizeof(originalBytes),
            "write to named object failed");

    *__error_nid_postfix() = 0;
    RequireError(shm_open_nid_postfix(name.c_str(), ReadWrite | Create | Exclusive, 0600), 17,
                 "exclusive creation replaced an existing named object");
    const int reader = shm_open_nid_postfix(name.c_str(), ReadOnly, 0);
    char contents[sizeof(originalBytes)]{};
    Require(reader >= 0 && pread_nid_postfix(reader, contents, sizeof(contents), 0) == sizeof(contents) &&
            std::memcmp(contents, originalBytes, sizeof(contents)) == 0, "reopen did not share the original contents");
    *__error_nid_postfix() = 0;
    Require(pwrite_nid_disambig1_nid_postfix(reader, "x", 1, 0) == -1 && *__error_nid_postfix() == 9,
            "read-only descriptor accepted a write");
    *__error_nid_postfix() = 0;
    Require(pwrite_nid_disambig1_nid_postfix(reader, "x", 1, 64) == -1 && *__error_nid_postfix() == 9,
            "read-only descriptor accepted a write at end of file");
    *__error_nid_postfix() = 0;
    Require(pwrite_nid_disambig1_nid_postfix(reader, "", 0, 64) == -1 && *__error_nid_postfix() == 9,
            "read-only descriptor accepted a zero-length write");

    Require(shm_unlink_nid_postfix(name.c_str()) == 0, "unlink of named object failed");
    *__error_nid_postfix() = 0;
    RequireError(shm_open_nid_postfix(name.c_str(), ReadOnly, 0), 2, "unlinked name remained openable");
    std::memset(contents, 0, sizeof(contents));
    Require(pread_nid_postfix(original, contents, sizeof(contents), 0) == sizeof(contents) &&
            std::memcmp(contents, originalBytes, sizeof(contents)) == 0, "open descriptor stopped working after unlink");
    const char updatedByte = 'X';
    Require(pwrite_nid_disambig1_nid_postfix(original, &updatedByte, 1, 0) == 1,
            "open descriptor stopped accepting writes after unlink");

    const int replacement = shm_open_nid_postfix(name.c_str(), ReadWrite | Create | Exclusive, 0600);
    Require(replacement >= 0, "name could not be recreated while old descriptors remained open");
    Require(ftruncate_nid_postfix(replacement, 64) == 0, "sizing the replacement object failed");
    Require(fchmod_nid_postfix(original, 0400) == 0,
            "fchmod failed on the old open object after unlink and name recreation");
    Require(fchmod_nid_postfix(original, 0600) == 0,
            "restoring mode on the old open object after name recreation failed");
    std::memset(contents, 0x7f, sizeof(contents));
    Require(pread_nid_postfix(replacement, contents, sizeof(contents), 0) == sizeof(contents) &&
            std::memcmp(contents, "\0\0\0\0\0\0\0\0", sizeof(contents)) == 0,
            "recreated name still referred to the unlinked object");
    Require(pwrite_nid_disambig1_nid_postfix(replacement, "y", 1, 0) == 1,
            "fchmod on the old descriptor changed the replacement object's permissions");
    const char replacementBytes[] = "replacement";
    Require(pwrite_nid_disambig1_nid_postfix(replacement, replacementBytes, sizeof(replacementBytes), 0) == sizeof(replacementBytes),
            "write to replacement object failed");
    std::memset(contents, 0, sizeof(contents));
    Require(pread_nid_postfix(original, contents, sizeof(contents), 0) == sizeof(contents) &&
            contents[0] == updatedByte && std::memcmp(contents + 1, originalBytes + 1, sizeof(originalBytes) - 2) == 0,
            "replacement write changed the old open object");
    Require(shm_unlink_nid_postfix(name.c_str()) == 0, "replacement unlink failed");
    Require(close_nid_postfix(reader) == 0 && close_nid_postfix(original) == 0 && close_nid_postfix(replacement) == 0,
            "closing lifecycle descriptors failed");
    RequireNoUnlinkedFiles(name);

    const std::string duplicateName = base + "-duplicate";
    const int duplicateOriginal = shm_open_nid_postfix(duplicateName.c_str(), ReadWrite | Create | Exclusive, 0600);
    Require(duplicateOriginal >= 0 && ftruncate_nid_postfix(duplicateOriginal, 64) == 0,
            "creating the duplicate test object failed");
    const int duplicate = dup_nid_postfix(duplicateOriginal);
    Require(duplicate >= 0 && pwrite_nid_disambig1_nid_postfix(duplicate, "d", 1, 0) == 1,
            "dup lost shared-memory write access");
    const int duplicateReader = shm_open_nid_postfix(duplicateName.c_str(), ReadOnly, 0);
    Require(duplicateReader >= 0 && shm_unlink_nid_postfix(duplicateName.c_str()) == 0,
            "unlinking the duplicate test object failed");
    Require(close_nid_postfix(duplicateOriginal) == 0, "closing the duplicated original failed");
    Require(dup2_nid_postfix(-1, duplicate) == -1,
            "dup2 accepted an invalid source");
    Require(pwrite_nid_disambig1_nid_postfix(duplicate, "e", 1, 0) == 1,
            "failed dup2 destroyed its shared-memory target");
    Require(dup2_nid_postfix(duplicateReader, duplicate) == duplicate,
            "dup2 could not replace a shared-memory target");
    Require(pwrite_nid_disambig1_nid_postfix(duplicate, "f", 1, 0) == -1 && *__error_nid_postfix() == 9,
            "dup2 lost shared-memory read-only access");
    Require(close_nid_postfix(duplicateReader) == 0, "closing the duplicate reader failed");
    const std::string replacementName = base + "-duplicate-replacement";
    const int duplicateReplacement = shm_open_nid_postfix(replacementName.c_str(), ReadWrite | Create | Exclusive, 0600);
    Require(duplicateReplacement >= 0 && dup2_nid_postfix(duplicateReplacement, duplicate) == duplicate,
            "dup2 could not replace the last unlinked descriptor");
    RequireNoUnlinkedFiles(duplicateName);
    Require(shm_unlink_nid_postfix(replacementName.c_str()) == 0 &&
            close_nid_postfix(duplicateReplacement) == 0 && close_nid_postfix(duplicate) == 0,
            "cleaning up dup2 replacement descriptors failed");
    RequireNoUnlinkedFiles(replacementName);
    VerifyResizeReadPosition(base + "-resize-read");

    const std::string concurrentName = base + "-exclusive-race";
    constexpr int contenderCount = 12;
    std::vector<int> descriptors(contenderCount, -2);
    std::vector<int> errors(contenderCount, 0);
    std::vector<std::thread> contenders;
    contenders.reserve(contenderCount);
    for (int index = 0; index < contenderCount; ++index) {
        contenders.emplace_back([&, index] {
            descriptors[index] = shm_open_nid_postfix(concurrentName.c_str(), ReadWrite | Create | Exclusive, 0600);
            errors[index] = *__error_nid_postfix();
        });
    }
    for (auto& contender : contenders) contender.join();
    int created = 0;
    for (int index = 0; index < contenderCount; ++index) {
        if (descriptors[index] >= 0) {
            ++created;
            Require(close_nid_postfix(descriptors[index]) == 0, "closing exclusive-race descriptor failed");
        } else {
            Require(errors[index] == 17, "exclusive-race loser did not receive EEXIST");
        }
    }
    Require(created == 1, "exclusive create race produced more or fewer than one winner");
    Require(shm_unlink_nid_postfix(concurrentName.c_str()) == 0, "exclusive-race cleanup failed");

    const std::string raceName = base + "-cross-process-race";
    const int raceDescriptor = shm_open_nid_postfix(raceName.c_str(), ReadWrite | Create | Exclusive, 0600);
    Require(raceDescriptor >= 0 && ftruncate_nid_postfix(raceDescriptor, RaceSize) == 0,
            "cross-process race object creation failed");
    VerifyCrossProcessResizeWrite(raceName, raceDescriptor);
    Require(close_nid_postfix(raceDescriptor) == 0, "closing cross-process race descriptor failed");

    const std::string permissionsName = base + "-permissions";
    const int restricted = shm_open_nid_postfix(permissionsName.c_str(), ReadWrite | Create | Exclusive, 0400);
    Require(restricted >= 0 && close_nid_postfix(restricted) == 0, "read-only mode creation failed");
    *__error_nid_postfix() = 0;
    RequireError(shm_open_nid_postfix(permissionsName.c_str(), ReadWrite, 0), 13,
                 "mode 0400 allowed a later read-write open");
    const int restrictedReader = shm_open_nid_postfix(permissionsName.c_str(), ReadOnly, 0);
    Require(restrictedReader >= 0, "mode 0400 rejected a read-only open");
    Require(fchmod_nid_postfix(restrictedReader, 0600) == 0,
            "fchmod could not restore write permission on a named object");
    const int restoredWriter = shm_open_nid_postfix(permissionsName.c_str(), ReadWrite, 0);
    Require(restoredWriter >= 0, "mode 0600 after fchmod rejected read-write open");
    Require(fchmod_nid_postfix(restoredWriter, 0400) == 0,
            "fchmod could not remove write permission on a named object");
    *__error_nid_postfix() = 0;
    Require(shm_unlink_nid_postfix(permissionsName.c_str()) == -1 && *__error_nid_postfix() == 13,
            "unlink of a read-only named object did not return EACCES");
    Require(fchmod_nid_postfix(restoredWriter, 0600) == 0,
            "fchmod could not restore permission before unlink");
    Require(shm_unlink_nid_postfix(permissionsName.c_str()) == 0,
            "owner could not unlink a writable named object");
    Require(close_nid_postfix(restoredWriter) == 0, "closing restored writer failed");
    Require(close_nid_postfix(restrictedReader) == 0, "closing restricted reader failed");

    const std::string noPermissionsName = base + "-mode-zero";
    const int noPermissions = shm_open_nid_postfix(noPermissionsName.c_str(), ReadWrite | Create | Exclusive, 0000);
    Require(noPermissions >= 0 && fchmod_nid_postfix(noPermissions, 0600) == 0,
            "fchmod could not restore access to a mode 0000 named object");
    Require(shm_unlink_nid_postfix(noPermissionsName.c_str()) == 0 && close_nid_postfix(noPermissions) == 0,
            "mode 0000 object cleanup failed");

    *__error_nid_postfix() = 0;
    RequireError(shm_open_nid_postfix(nullptr, ReadWrite, 0), 14, "null name did not return EFAULT");
    *__error_nid_postfix() = 0;
    RequireError(shm_open_nid_postfix("relative", ReadWrite, 0), 22, "relative name did not return EINVAL");
    std::puts("Windows named shared-memory lifecycle checks passed");
}
}

int main(int argumentCount, char** arguments) {
    try {
        if (argumentCount == 3 && std::string_view(arguments[1]) == "--race")
            return RunResizeWriteRace(arguments[2]) ? 0 : 1;
        Run();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
