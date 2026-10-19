#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "prx/libc/include/General.hpp"
#include "H264Fixture.hpp"
#include "HevcFixture.hpp"

struct ComputeMemoryInfo {
    std::uint64_t thisSize;
    std::uint64_t cpuGpuMemorySize;
    void* cpuGpuMemory;
};

struct ComputeConfigInfo {
    std::uint64_t thisSize;
    std::uint16_t computePipeId;
    std::uint16_t computeQueueId;
    bool checkMemoryType;
};

struct DecoderConfigInfo {
    std::uint64_t thisSize;
    std::uint32_t resourceType;
    std::uint32_t codecType;
    std::uint32_t profile;
    std::uint32_t maxLevel;
    std::int32_t maxFrameWidth;
    std::int32_t maxFrameHeight;
    std::int32_t maxDpbFrameCount;
    std::uint32_t decodePipelineDepth;
    std::uint64_t computeQueue;
    std::uint64_t cpuAffinityMask;
    std::int32_t cpuThreadPriority;
    bool optimizeProgressiveVideo;
    std::uint8_t reserved[19];
};

struct DecoderMemoryInfo {
    std::uint64_t thisSize;
    std::uint64_t cpuMemorySize;
    void* cpuMemory;
    std::uint64_t gpuMemorySize;
    void* gpuMemory;
    std::uint64_t cpuGpuMemorySize;
    void* cpuGpuMemory;
    std::uint64_t maxFrameBufferSize;
    std::uint32_t frameBufferAlignment;
};

struct InputData {
    std::uint64_t thisSize;
    const std::uint8_t* auData;
    std::uint64_t auSize;
    std::uint64_t ptsData;
    std::uint64_t dtsData;
    std::uint64_t attachedData;
};

struct InputResult {
    std::uint64_t thisSize;
    const void* decodedAu;
    std::uint32_t outputFrameCount;
    std::uint32_t reserved;
};

struct FrameBuffer {
    std::uint64_t thisSize;
    void* frameBuffer;
    std::uint64_t frameBufferSize;
};

struct OutputInfo {
    std::uint64_t thisSize;
    bool isValid;
    bool isLastFrame;
    bool isErrorFrame;
    std::uint8_t pictureCount;
    std::uint32_t codecType;
    std::uint32_t frameWidth;
    std::uint32_t framePitch;
    std::uint32_t frameHeight;
    bool isDiscardedFrame;
    void* frameBuffer;
    std::uint64_t frameBufferSize;
    std::uint32_t frameFormat;
    std::uint32_t framePitchInBytes;
};

struct AvcPictureInfo {
    std::uint64_t thisSize;
    bool isValid;
    std::uint64_t ptsData;
    std::uint64_t dtsData;
    std::uint64_t attachedData;
    std::uint8_t idrPictureFlag;
    std::uint8_t profileIdc;
    std::uint8_t levelIdc;
    std::uint32_t picWidthInMbsMinus1;
    std::uint32_t picHeightInMapUnitsMinus1;
    std::uint8_t frameMbsOnlyFlag;
    std::uint8_t frameCroppingFlag;
    std::uint32_t frameCropLeftOffset;
    std::uint32_t frameCropRightOffset;
    std::uint32_t frameCropTopOffset;
    std::uint32_t frameCropBottomOffset;
    std::uint8_t vui[44];
};

struct HevcPictureInfo {
    std::uint64_t thisSize;
    bool isValid;
    std::uint64_t ptsData;
    std::uint64_t dtsData;
    std::uint64_t attachedData;
    std::uint32_t picWidthInLumaSamples;
    std::uint32_t picHeightInLumaSamples;
    std::uint8_t bitDepthLumaMinus8;
    std::uint8_t bitDepthChromaMinus8;
    std::uint8_t timingInfoPresentFlag;
    std::uint32_t numUnitsInTick;
    std::uint32_t timeScale;
    std::uint32_t aspectRatioInfoPresentFlag;
    std::uint8_t aspectRatioIdc;
    std::uint16_t sarWidth;
    std::uint16_t sarHeight;
    std::uint8_t videoSignal[8];
    std::uint32_t picStruct;
    std::uint32_t sourceScanType;
    std::uint32_t duplicateFlag;
    std::uint32_t conformanceWindowFlag;
    std::uint32_t confWinLeftOffset;
    std::uint32_t confWinRightOffset;
    std::uint32_t confWinTopOffset;
    std::uint32_t confWinBottomOffset;
    std::uint32_t defaultDisplayWindow[5];
    std::uint8_t chromaLocation[4];
    std::uint8_t presentFlags[11];
    std::uint8_t idrPictureFlag;
    std::uint8_t irapPictureFlag;
    std::uint8_t generalProfileSpace;
    std::uint8_t generalTierFlag;
    std::uint8_t generalProfileIdc;
    std::uint8_t generalSourceFlags[3];
    std::uint8_t generalLevelIdc;
    std::uint8_t subLayer[9];
    std::uint8_t frameCroppingFlag;
    std::uint32_t frameCropLeftOffset;
    std::uint32_t frameCropRightOffset;
    std::uint32_t frameCropTopOffset;
    std::uint32_t frameCropBottomOffset;
};

static_assert(sizeof(HevcPictureInfo) == 0xb8 && offsetof(HevcPictureInfo, idrPictureFlag) == 0x93 && offsetof(HevcPictureInfo, frameCroppingFlag) == 0xa5);
static_assert(sizeof(DecoderConfigInfo) == 0x50 && sizeof(OutputInfo) == 0x38 && offsetof(OutputInfo, isLastFrame) == 9 && offsetof(OutputInfo, pictureCount) == 0xb && sizeof(AvcPictureInfo) == 0x78);

extern "C" {
int APS5_VABI sceVdecswQueryComputeMemoryInfo(ComputeMemoryInfo*);
int APS5_VABI sceVdecswAllocateComputeQueue(const ComputeConfigInfo*, const ComputeMemoryInfo*, std::uint64_t*);
int APS5_VABI sceVdecswReleaseComputeQueue(std::uint64_t);
int APS5_VABI sceVdecswQueryDecoderMemoryInfo(const DecoderConfigInfo*, DecoderMemoryInfo*);
int APS5_VABI sceVdecswCreateDecoder(const DecoderConfigInfo*, const DecoderMemoryInfo*, std::uint64_t*);
int APS5_VABI sceVdecswDeleteDecoder(std::uint64_t);
int APS5_VABI sceVdecswSetDecodeInput(std::uint64_t, const InputData*);
int APS5_VABI sceVdecswTrySyncDecodeInput(std::uint64_t, InputResult*);
int APS5_VABI sceVdecswSyncDecodeInput(std::uint64_t, InputResult*);
int APS5_VABI sceVdecswSetDecodeOutput(std::uint64_t, const FrameBuffer*);
int APS5_VABI sceVdecswTrySyncDecodeOutput(std::uint64_t, OutputInfo*);
int APS5_VABI sceVdecswSyncDecodeOutput(std::uint64_t, OutputInfo*);
int APS5_VABI sceVdecswGetAvcPictureInfo(const OutputInfo*, AvcPictureInfo*, AvcPictureInfo*);
int APS5_VABI sceVdecswGetHevcPictureInfo(const OutputInfo*, HevcPictureInfo*);
int APS5_VABI sceVdecswFinalizeDecodeSequence(std::uint64_t);
int APS5_VABI sceVdecswResetDecoder(std::uint64_t);
}

namespace {

using namespace H264Fixture;
using namespace HevcFixture;

constexpr int OutputPending = static_cast<int>(0x81510115u);
constexpr int InputQueueEmpty = static_cast<int>(0x81510116u);
constexpr std::uint32_t CodecAvc = 1;
constexpr std::uint32_t CodecHevc = 974921;
constexpr std::array<std::size_t, 6> HevcDisplayOrderUnits{0, 3, 2, 1, 5, 4};

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Session {
    std::uint64_t queue = 0;
    std::uint64_t decoder = 0;
    DecoderMemoryInfo memory{sizeof(DecoderMemoryInfo)};

    explicit Session(std::uint32_t codec = CodecAvc, std::uint32_t profile = 100, std::uint32_t level = 42) {
        ComputeMemoryInfo compute{sizeof(ComputeMemoryInfo)};
        check(sceVdecswQueryComputeMemoryInfo(&compute) == 0 && compute.cpuGpuMemorySize != 0, "compute memory query failed");
        const ComputeConfigInfo computeConfig{sizeof(ComputeConfigInfo), 3, 3, true};
        check(sceVdecswAllocateComputeQueue(&computeConfig, &compute, &queue) == 0, "compute queue allocation failed");
        DecoderConfigInfo config{sizeof(DecoderConfigInfo), 1, codec, profile, level, static_cast<std::int32_t>(Width), static_cast<std::int32_t>(Height), 4, 3, queue, 0x3f, 0, true, {}};
        check(sceVdecswQueryDecoderMemoryInfo(&config, &memory) == 0 && memory.maxFrameBufferSize != 0, "decoder memory query failed");
        check(sceVdecswCreateDecoder(&config, &memory, &decoder) == 0, "decoder creation failed");
    }

    ~Session() {
        sceVdecswDeleteDecoder(decoder);
        sceVdecswReleaseComputeQueue(queue);
    }
};

void testDecode() {
    Session session;
    const auto units = accessUnits(false);
    check(units.size() == PictureHashes.size(), "unexpected access unit count");
    std::array<std::vector<std::uint8_t>, 2> buffers{std::vector<std::uint8_t>(session.memory.maxFrameBufferSize), std::vector<std::uint8_t>(session.memory.maxFrameBufferSize)};
    InputResult result{sizeof(InputResult)};
    check(sceVdecswTrySyncDecodeInput(session.decoder, &result) == InputQueueEmpty && result.decodedAu == nullptr, "an idle decoder did not report an empty input queue");
    std::size_t pictures = 0;
    bool outputSet = false;
    bool ended = false;
    const auto drain = [&] {
        for (;;) {
            if (!outputSet) {
                const FrameBuffer frame{sizeof(FrameBuffer), buffers[pictures % 2].data(), buffers[pictures % 2].size()};
                check(sceVdecswSetDecodeOutput(session.decoder, &frame) == 0, "output buffer was rejected");
                outputSet = true;
            }
            OutputInfo output{sizeof(OutputInfo)};
            const auto status = sceVdecswTrySyncDecodeOutput(session.decoder, &output);
            if (status == OutputPending) return;
            check(status == 0, "output sync failed");
            outputSet = false;
            if (!output.isValid) {
                check(output.isLastFrame && output.pictureCount == 0, "an output without a picture is not the end of the sequence");
                ended = true;
                return;
            }
            check(pictures < PictureHashes.size(), "more pictures than access units");
            check(output.isLastFrame == (pictures + 1 == PictureHashes.size()), "only the last picture of the finalized sequence is marked last");
            check(!output.isErrorFrame && !output.isDiscardedFrame && output.pictureCount == 1 && output.frameWidth == Width && output.frameHeight == CodedHeight && output.frameBuffer == buffers[pictures % 2].data(), "unexpected picture geometry");
            AvcPictureInfo info{sizeof(AvcPictureInfo)};
            check(sceVdecswGetAvcPictureInfo(&output, &info, nullptr) == 0 && info.isValid, "picture info missing");
            const auto unit = DisplayOrderUnits[pictures];
            check(info.ptsData == 1000 + unit && info.dtsData == unit && info.attachedData == 0xa0 + unit && info.idrPictureFlag == (pictures == 0 ? 1 : 0) && info.profileIdc == 100, "pictures out of display order");
            check(info.picWidthInMbsMinus1 == Width / 16 - 1 && info.picHeightInMapUnitsMinus1 == CodedHeight / 16 - 1 && info.frameMbsOnlyFlag == 1 && info.frameCroppingFlag == 1 && info.frameCropLeftOffset == 0 && info.frameCropRightOffset == 0 && info.frameCropTopOffset == 0 && info.frameCropBottomOffset == (CodedHeight - Height) / 2, "picture geometry in the picture info is wrong");
            check(hashNv12(static_cast<const std::uint8_t*>(output.frameBuffer), output.framePitch, output.frameHeight) == PictureHashes[pictures], "picture " + std::to_string(pictures) + " differs from the reference");
            ++pictures;
        }
    };
    std::uint32_t produced = 0;
    for (std::size_t unit = 0; unit < units.size(); ++unit) {
        const InputData input{sizeof(InputData), units[unit].data(), units[unit].size(), 1000 + unit, unit, 0xa0 + unit};
        check(sceVdecswSetDecodeInput(session.decoder, &input) == 0, "decode input was rejected");
        InputResult consumed{sizeof(InputResult)};
        check(sceVdecswTrySyncDecodeInput(session.decoder, &consumed) == 0 && consumed.decodedAu == units[unit].data(), "an access unit was not consumed");
        produced += consumed.outputFrameCount;
        drain();
    }
    check(pictures < PictureHashes.size(), "the last picture was returned before the sequence was finalized");
    check(sceVdecswFinalizeDecodeSequence(session.decoder) == 0, "finalize failed");
    while (!ended) drain();
    check(pictures == PictureHashes.size(), "missing pictures after finalizing the sequence");
    check(produced <= PictureHashes.size(), "more pictures counted than decoded");
    check(sceVdecswTrySyncDecodeInput(session.decoder, &result) == InputQueueEmpty, "a finalized decoder did not report an empty input queue");
}

void testHevc() {
    Session session(CodecHevc, 1, 30);
    const auto units = hevcUnits(HevcStream.data(), HevcStream.size());
    check(units.size() == HevcHashes.size(), "unexpected HEVC access unit count");
    std::vector<std::uint8_t> buffer(session.memory.maxFrameBufferSize);
    std::size_t pictures = 0;
    bool ended = false;
    const auto drain = [&] {
        for (;;) {
            const FrameBuffer frame{sizeof(FrameBuffer), buffer.data(), buffer.size()};
            check(sceVdecswSetDecodeOutput(session.decoder, &frame) == 0, "HEVC output buffer was rejected");
            OutputInfo output{sizeof(OutputInfo)};
            const auto status = sceVdecswTrySyncDecodeOutput(session.decoder, &output);
            if (status == OutputPending) return;
            check(status == 0, "HEVC output sync failed");
            if (!output.isValid) {
                ended = true;
                return;
            }
            check(pictures < HevcHashes.size() && output.codecType == CodecHevc && output.pictureCount == 1 && output.frameWidth == Width && output.frameHeight == Height, "unexpected HEVC picture geometry");
            HevcPictureInfo info{sizeof(HevcPictureInfo)};
            check(sceVdecswGetHevcPictureInfo(&output, &info) == 0 && info.isValid, "HEVC picture info missing");
            const auto unit = HevcDisplayOrderUnits[pictures];
            check(info.ptsData == 1000 + unit && info.dtsData == unit && info.attachedData == 0xa0 + unit, "HEVC pictures out of display order");
            check(info.idrPictureFlag == (pictures == 0 ? 1 : 0) && info.irapPictureFlag == (pictures == 0 ? 1 : 0), "HEVC random access flags differ");
            check(info.generalProfileIdc == 1 && info.generalLevelIdc == 30 && info.bitDepthLumaMinus8 == 0 && info.bitDepthChromaMinus8 == 0, "HEVC profile, level or bit depth differs");
            check(info.picWidthInLumaSamples == Width && info.picHeightInLumaSamples == Height && info.conformanceWindowFlag == 0 && info.frameCroppingFlag == 0, "HEVC picture geometry in the picture info is wrong");
            AvcPictureInfo avc{sizeof(AvcPictureInfo)};
            bool avcThrows = false;
            try {
                sceVdecswGetAvcPictureInfo(&output, &avc, nullptr);
            } catch (const std::exception&) {
                avcThrows = true;
            }
            check(avcThrows, "AVC picture info was returned for an HEVC picture");
            check(hashNv12(buffer.data(), output.framePitch, output.frameHeight) == HevcHashes[pictures], "HEVC picture " + std::to_string(pictures) + " differs from the reference");
            ++pictures;
        }
    };
    for (std::size_t unit = 0; unit < units.size(); ++unit) {
        const InputData input{sizeof(InputData), units[unit].data(), units[unit].size(), 1000 + unit, unit, 0xa0 + unit};
        check(sceVdecswSetDecodeInput(session.decoder, &input) == 0, "HEVC decode input was rejected");
        InputResult consumed{sizeof(InputResult)};
        check(sceVdecswTrySyncDecodeInput(session.decoder, &consumed) == 0 && consumed.decodedAu == units[unit].data(), "an HEVC access unit was not consumed");
        drain();
    }
    check(sceVdecswFinalizeDecodeSequence(session.decoder) == 0, "HEVC finalize failed");
    while (!ended) drain();
    check(pictures == HevcHashes.size(), "missing HEVC pictures after finalizing the sequence");
}

void testHevcCrop() {
    Session session(CodecHevc, 1, 30);
    std::vector<std::uint8_t> buffer(session.memory.maxFrameBufferSize);
    const FrameBuffer frame{sizeof(FrameBuffer), buffer.data(), buffer.size()};
    check(sceVdecswSetDecodeOutput(session.decoder, &frame) == 0, "cropped HEVC output buffer was rejected");
    const InputData input{sizeof(InputData), HevcCroppedStream.data(), HevcCroppedStream.size(), 1000, 0, 0xa0};
    check(sceVdecswSetDecodeInput(session.decoder, &input) == 0, "cropped HEVC decode input was rejected");
    InputResult consumed{sizeof(InputResult)};
    check(sceVdecswSyncDecodeInput(session.decoder, &consumed) == 0, "the cropped HEVC access unit was not consumed");
    check(sceVdecswFinalizeDecodeSequence(session.decoder) == 0, "cropped HEVC finalize failed");
    OutputInfo output{sizeof(OutputInfo)};
    check(sceVdecswSyncDecodeOutput(session.decoder, &output) == 0 && output.isValid && output.frameWidth == 64 && output.frameHeight == 40, "unexpected cropped HEVC picture geometry");
    HevcPictureInfo info{sizeof(HevcPictureInfo)};
    check(sceVdecswGetHevcPictureInfo(&output, &info) == 0 && info.isValid, "cropped HEVC picture info missing");
    check(info.picWidthInLumaSamples == 64 && info.picHeightInLumaSamples == 40, "cropped HEVC coded size differs");
    check(info.conformanceWindowFlag == 1 && info.confWinLeftOffset == 0 && info.confWinRightOffset == 0 && info.confWinTopOffset == 0 && info.confWinBottomOffset == 2, "HEVC conformance window differs");
    check(info.frameCroppingFlag == 1 && info.frameCropLeftOffset == 0 && info.frameCropRightOffset == 0 && info.frameCropTopOffset == 0 && info.frameCropBottomOffset == 2, "HEVC frame cropping differs");
}

void testWithoutOutput() {
    Session session;
    const auto units = accessUnits(false);
    const InputData input{sizeof(InputData), units[0].data(), units[0].size(), 1000, 0, 0xa0};
    check(sceVdecswSetDecodeInput(session.decoder, &input) == 0, "decode input was rejected");
    InputResult result{sizeof(InputResult)};
    check(sceVdecswTrySyncDecodeInput(session.decoder, &result) == 0 && result.decodedAu == units[0].data(), "an input was not decoded before an output buffer was set");
    std::vector<std::uint8_t> buffer(session.memory.maxFrameBufferSize);
    const FrameBuffer frame{sizeof(FrameBuffer), buffer.data(), buffer.size()};
    check(sceVdecswSetDecodeOutput(session.decoder, &frame) == 0, "output buffer was rejected");
    OutputInfo output{sizeof(OutputInfo)};
    check(sceVdecswTrySyncDecodeOutput(session.decoder, &output) == OutputPending, "the only picture of an open sequence was returned before it was known to be the last");
    check(sceVdecswFinalizeDecodeSequence(session.decoder) == 0, "finalize failed");
    check(sceVdecswTrySyncDecodeOutput(session.decoder, &output) == 0 && output.isValid && output.isLastFrame, "the only picture of a finalized sequence is not marked last");
    check(sceVdecswResetDecoder(session.decoder) == 0, "reset failed");
    check(sceVdecswTrySyncDecodeInput(session.decoder, &result) == InputQueueEmpty, "a reset decoder kept its inputs");
}

void testSyncWrappers() {
    Session session;
    InputResult idle{sizeof(InputResult)};
    check(sceVdecswSyncDecodeInput(session.decoder, &idle) == InputQueueEmpty, "sync input on an idle decoder did not report an empty queue");
    const auto units = accessUnits(false);
    const InputData input{sizeof(InputData), units[0].data(), units[0].size(), 1000, 0, 0xa0};
    check(sceVdecswSetDecodeInput(session.decoder, &input) == 0, "decode input was rejected");
    InputResult result{sizeof(InputResult)};
    check(sceVdecswSyncDecodeInput(session.decoder, &result) == 0 && result.decodedAu == units[0].data(), "sync input did not consume the access unit");
    std::vector<std::uint8_t> buffer(session.memory.maxFrameBufferSize);
    const FrameBuffer frame{sizeof(FrameBuffer), buffer.data(), buffer.size()};
    check(sceVdecswSetDecodeOutput(session.decoder, &frame) == 0, "output buffer was rejected");
    check(sceVdecswFinalizeDecodeSequence(session.decoder) == 0, "finalize failed");
    OutputInfo output{sizeof(OutputInfo)};
    check(sceVdecswSyncDecodeOutput(session.decoder, &output) == 0 && output.isValid && output.isLastFrame, "sync output did not return the finalized picture");
    HevcPictureInfo hevc{sizeof(HevcPictureInfo)};
    bool hevcThrows = false;
    try {
        sceVdecswGetHevcPictureInfo(&output, &hevc);
    } catch (const std::exception&) {
        hevcThrows = true;
    }
    check(hevcThrows, "HEVC picture info was returned for an H.264 picture");
}

}

int main() {
    try {
        testDecode();
        testHevc();
        testHevcCrop();
        testWithoutOutput();
        testSyncWrappers();
        std::puts("Vdecsw tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
