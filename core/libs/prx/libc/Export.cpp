#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/HeapDiagnostics.hpp"

uint32_t Need_sceLibc = 1;

extern "C" {

    int APS5_VABI _ZSt13_Execute_onceRSt9once_flagPFiPvS1_PS1_ES1__nid_postfix(int*, int (APS5_VABI *)(void*, void*, void**), void*);

    int APS5_VABI std_execute_once_nid_postfix(int* flag, int (APS5_VABI *func)(void*, void*, void**), void* arg) {
        return _ZSt13_Execute_onceRSt9once_flagPFiPvS1_PS1_ES1__nid_postfix(flag, func, arg);
    }

    void APS5_VABI LibcHeapGetTraceInfo_nid_postfix(LibcHeapInfo* info) {
        LibcHeapTraceInfo_nid_no_patch(info);
    }

// Imports of unknown names: the Windows loader resolves imports strictly,
// so the NIDs must be exported even while unresolved.
APS5_EXPORT("Ye20uNnlglA", libcCyberUnknown02);
std::uint64_t APS5_VABI libcCyberUnknown02(void) {
    NotImplemented_nid_no_patch("Ye20uNnlglA");
    return 0;
}

APS5_EXPORT("H+8UBOwfScI", libcCyberUnknown08);
std::uint64_t APS5_VABI libcCyberUnknown08(void) {
    NotImplemented_nid_no_patch("H+8UBOwfScI");
    return 0;
}

// Dinkumware's wide classification entry. Its descriptor encoding is not
// known here (the guest's wctype() returns rune masks, see RuneLocale.cpp),
// so the host's iswctype cannot be handed the value.
std::uint64_t APS5_VABI _Iswctype_nid_postfix() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}
}

