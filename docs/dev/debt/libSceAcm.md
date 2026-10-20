# libSceAcm technical debt

### Silent stubs

- [libSceAcm](../../../core/libs/prx/libSceAcm/Export.cpp) batches execute as silent no-ops, so `sceAcm_ConvReverb_SharedInput`, `sceAcm_FFT` and `sceAcm_Panner` return success without encoding audio commands.

### Unknown function info

- Context validation error codes (0x81940002 for bad context and 0x81940006 for bad pointer) for [libSceAcm](../../../core/libs/prx/libSceAcm/Export.cpp) follow prosper's `hle_audio_spatial.cpp`. KytyPS5 (`acm.cpp`) and prosper do not track individual batch identifiers, so `sceAcmBatchWait` returns success for valid contexts without inspecting batch IDs. Hardware ACM DSP coprocessor scheduling and console error verification remain unmeasured.
- PPSA02530 imports declared without parameters, signatures unknown, added without an implementation because the title fails to load without them: [sceAcmBatchJobPriority](../../../core/libs/prx/libSceAcm/Export.cpp) (libSceAcm) and [scePadVrControllerGetTriggerEffectStates](../../../core/libs/prx/libScePad/Export.cpp) (libScePad).
