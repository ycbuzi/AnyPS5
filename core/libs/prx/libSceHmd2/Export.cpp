#include "Hmd2.hpp"
#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libSceHmd/Hmd.hpp"
#include "prx/libSceUserService/UserService.hpp"
#include "SceTypes.hpp"

#include <cstring>
#include <mutex>
#include <stdexcept>

namespace {

constexpr std::int32_t DeviceHandle = 0x0e010000;
constexpr Hmd2::FieldOfView DefaultFieldOfView{1.84177089f, 0.947190642f, 1.32898104f, 1.32898104f};

std::mutex stateMutex;
bool initialized = false;
std::int32_t openHandle = 0;

std::mutex reprojectionMutex;
Hmd2::ReprojectionState* reprojectionState = nullptr;
std::int32_t allowPositionalReprojection = 0;

std::uint32_t CompiledSdkVersion() {
    static const auto* processParameters = static_cast<const std::byte*>(ApplicationProcessParameters_nid_no_patch());
    std::uint32_t version;
    std::memcpy(&version, processParameters + 0x10, sizeof(version));
    return version;
}

std::int32_t GetDeviceInformation(Hmd2::DeviceInformation* info) {
    if (!info) return Hmd2::ParameterNull;
    *info = {};
    if (!initialized) return Hmd2::NotInitialized;
    info->status = Hmd2::DeviceStatus::NotDetected;
    if (CompiledSdkVersion() < Hmd2::UserIdSdkVersion) info->userId = -1;
    info->panelWidth = 4000;
    info->panelHeight = 2040;
    info->latency90Hz = 5555;
    info->latency120Hz = 4160;
    info->virtualImageDistance = 2.0f;
    return 0;
}

}

extern "C" {

int APS5_VABI sceHmd2ReprojectionGetStatus() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionQuerySeeThroughBufferSizeAlign() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

std::int32_t APS5_VABI sceHmd2Initialize(const void* param) {
    (void)param;
    return Hmd::UnsupportedFeature;
}

std::int32_t APS5_VABI sceHmd2GetFieldOfView(std::int32_t handle, Hmd2::FieldOfView* fieldOfView) {
    std::lock_guard lock(stateMutex);
    if (!fieldOfView) return Hmd2::ParameterNull;
    if (handle == 0 || handle != openHandle) return Hmd2::InvalidHandle;
    if (!initialized) return Hmd2::NotInitialized;
    *fieldOfView = DefaultFieldOfView;
    return 0;
}

std::int32_t APS5_VABI sceHmd2GetDeviceInformationByHandle(std::int32_t handle, Hmd2::DeviceInformation* info) {
    std::lock_guard lock(stateMutex);
    if (handle != openHandle) return Hmd2::InvalidHandle;
    return GetDeviceInformation(info);
}

std::int32_t APS5_VABI sceHmd2Terminate() {
    std::lock_guard lock(stateMutex);
    if (!initialized) return Hmd2::NotInitialized;
    initialized = false;
    return 0;
}

std::int32_t APS5_VABI sceHmd2Close(std::int32_t handle) {
    std::lock_guard lock(stateMutex);
    if (!initialized) return Hmd2::NotInitialized;
    if (handle != openHandle) return Hmd2::InvalidHandle;
    openHandle = 0;
    return 0;
}

int APS5_VABI sceHmd2GazeGetResult() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeGetResultForFoveatedRendering() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

std::int32_t APS5_VABI sceHmd2GetDeviceInformation(Hmd2::DeviceInformation* info) {
    std::lock_guard lock(stateMutex);
    return GetDeviceInformation(info);
}

std::int32_t APS5_VABI sceHmd2GetFieldOfViewWithoutHandle(Hmd2::FieldOfView* fieldOfView) {
    std::lock_guard lock(stateMutex);
    if (!fieldOfView) return Hmd2::ParameterNull;
    if (!initialized) return Hmd2::NotInitialized;
    *fieldOfView = DefaultFieldOfView;
    return 0;
}

std::int32_t APS5_VABI sceHmd2Open(std::int32_t userId, std::int32_t type, std::int32_t index, const void* param) {
    std::lock_guard lock(stateMutex);
    if (!initialized) return Hmd2::NotInitialized;
    if (type != 0 || index != 0 || param) return Hmd2::ParameterInvalid;
    if (userId != USER_SERVICE_INITIAL_USER_ID) {
        if (CompiledSdkVersion() >= Hmd2::UserIdSdkVersion) return Hmd2::ParameterInvalid;
        throw std::runtime_error("sceHmd2Open: user is not the initial user");
    }
    if (openHandle != 0) return Hmd2::AlreadyOpened;
    openHandle = DeviceHandle;
    return DeviceHandle;
}

int APS5_VABI sceHmd2ReprojectionBeginFrame() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionDisableMirroring() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionDisableVrMode() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionEnableMirroring() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionEnableVrMode(std::uint64_t outputMode) {
    std::lock_guard lock(reprojectionMutex);
    if (reprojectionState == nullptr) return Hmd2::ReprojectionNotInitialized;
    if (outputMode != Hmd2::OutputMode89_91Hz && outputMode != Hmd2::OutputMode119_88Hz) {
        return Hmd2::VideoOutInvalidValue;
    }
    return Hmd2::VideoOutNoDevice;
}

int APS5_VABI sceHmd2ReprojectionGetMirroringDisplayBufferSizeAlign() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionGetMirroringWorkMemorySizeAlign() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionGetPredictedDisplayTime() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

std::int32_t APS5_VABI sceHmd2ReprojectionInitialize(const Hmd2::ReprojectionInitializeParam* param) {
    std::lock_guard lock(reprojectionMutex);
    if (reprojectionState != nullptr) return Hmd2::ReprojectionAlreadyInitialized;
    if (param == nullptr || param->reprojectionBuffer == nullptr || param->displayBuffer == nullptr) {
        return Hmd2::ParameterNull;
    }
    if (param->seeThroughBuffer != nullptr) {
        throw std::runtime_error("sceHmd2ReprojectionInitialize: see-through buffer not supported");
    }
    auto* state = static_cast<Hmd2::ReprojectionState*>(param->reprojectionBuffer);
    state->outputMode = 0;
    state->timingUs = param->reprojectionTiming != 0
                          ? static_cast<std::uint32_t>(param->reprojectionTiming)
                          : Hmd2::ReprojectionTimingDefault;
    reprojectionState = state;
    return 0;
}

Hmd2::SizeAlign APS5_VABI sceHmd2ReprojectionQueryBufferSizeAlign() {
    std::lock_guard lock(reprojectionMutex);
    if (allowPositionalReprojection == 0) {
        return Hmd2::SizeAlign{Hmd2::ReprojectionBufferSize, Hmd2::ReprojectionBufferAlign};
    }
    return Hmd2::SizeAlign{Hmd2::ReprojectionPositionalBufferSize
                               + static_cast<std::uint64_t>(allowPositionalReprojection)
                                     * Hmd2::ReprojectionPositionalBufferSizeStep,
                           Hmd2::ReprojectionPositionalBufferAlign};
}

int APS5_VABI sceHmd2ReprojectionQueryDisplayBufferSizeAlign() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

std::int32_t APS5_VABI sceHmd2ReprojectionSetAllowPositionalReprojection(std::int32_t mode, std::uint64_t param, std::int32_t flags) {
    (void)param;
    (void)flags;
    std::lock_guard lock(reprojectionMutex);
    if (mode < 0 || mode > Hmd2::AllowPositionalReprojectionMax) return Hmd2::ParameterInvalid;
    allowPositionalReprojection = mode;
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetMirroringOption() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetParam() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetParamWithBuffer() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetRenderConfig() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

std::int32_t APS5_VABI sceHmd2ReprojectionSetTiming(std::int32_t timing) {
    std::lock_guard lock(reprojectionMutex);
    if (reprojectionState == nullptr) return Hmd2::ReprojectionNotInitialized;
    if (timing < Hmd2::ReprojectionTimingMin || timing > Hmd2::ReprojectionTimingMax) {
        throw std::runtime_error("sceHmd2ReprojectionSetTiming: timing out of range");
    }
    reprojectionState->timingUs = static_cast<std::uint32_t>((timing + 99) / 100 * 100);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionTerminate() {
    std::lock_guard lock(reprojectionMutex);
    reprojectionState = nullptr;
    return 0;
}

std::int32_t APS5_VABI sceHmd2SetVibration(std::int32_t handle, std::uint32_t level) {
    (void)level;
    std::lock_guard lock(stateMutex);
    if (handle != openHandle) return Hmd2::InvalidHandle;
    return 0;
}

int APS5_VABI sceHmd2GazeGetCalibrationData() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeGetCalibrationDataMaxSize() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionClearUserEventEnd() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionClearUserEventStart() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionGetCurrentOutput() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetUserEventEnd() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetUserEventStart() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeActivateGazeTracking() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeGetDeviceStatus() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeGetUserCalibrationStatus() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeInsertRawDataWithHandle() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeSetEnabledEye() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeSetUserCalibrationData() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeSetUserCalibrationPoint() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeStartUserCalibration() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeStopUserCalibration() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSeeThroughDraw() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSeeThroughIC() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionVideoOutEq() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionWaitShader() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2SeeThroughCallbackGetPoseDataFunc() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeObjectSelectionGetSelectedObject() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeObjectSelectionInitialize() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeObjectSelectionQueryMemory() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2GazeObjectSelectionTerminate() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionClear() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSet() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetEventGpuEnd() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetEventGpuStart() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2RpeorjectionSetParam() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2SetFieldOfView() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceHmd2SetIpdOffset() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}
