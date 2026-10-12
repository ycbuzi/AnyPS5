#include <cstddef>
#include <cstdint>

#include "prx/libc/include/General.hpp"
#include "prx/libSceNgs2.native/include/Ngs2Types.hpp"

#pragma GCC visibility push(default)

extern "C" {

int APS5_VABI sceNgs2GeomApply(const Ngs2GeomListenerWork* listener, const Ngs2GeomSourceParam* source, Ngs2GeomAttribute* out_attrib, uint32_t flags) {
    (void)listener;
    (void)source;
    (void)out_attrib;
    (void)flags;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNgs2GeomCalcListener(const Ngs2GeomListenerParam* param, Ngs2GeomListenerWork* out_work, uint32_t flags) {
    (void)param;
    (void)out_work;
    (void)flags;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNgs2GeomResetListenerParam(Ngs2GeomListenerParam* outListenerParam) {
    if (outListenerParam == nullptr) throw std::invalid_argument("sceNgs2GeomResetListenerParam: null output");
    *outListenerParam = {};
    outListenerParam->orient_front.z = 1.0f;
    outListenerParam->orient_up.y = 1.0f;
    outListenerParam->sound_speed = 343.0f;
    return SCE_NGS2_OK;
}

int APS5_VABI sceNgs2GeomResetSourceParam(Ngs2GeomSourceParam* outSourceParam) {
    if (outSourceParam == nullptr) throw std::invalid_argument("sceNgs2GeomResetSourceParam: null output");
    *outSourceParam = {};
    outSourceParam->direction.z = 1.0f;
    outSourceParam->cone = {1.0f, 360.0f, 1.0f, 360.0f};
    outSourceParam->rolloff = {0, 1000000.0f, 1.0f, 1.0f};
    outSourceParam->doppler_factor = 1.0f;
    outSourceParam->fbw_level = 1.0f;
    outSourceParam->lfe_level = 1.0f;
    outSourceParam->max_level = 1.0f;
    outSourceParam->num_speakers = 2;
    outSourceParam->matrix_format = 2;
    return SCE_NGS2_OK;
}

int APS5_VABI sceNgs2ReportRegisterHandler(uint32_t report_type, void* handler, uintptr_t user_data, Ngs2Handle* out_handle) {
    (void)report_type;
    (void)handler;
    (void)user_data;
    (void)out_handle;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNgs2ReportUnregisterHandler(Ngs2Handle report_handle) {
    (void)report_handle;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}

#pragma GCC visibility pop
