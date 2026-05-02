#include <stdint.h>
#include <stdlib.h>

uint32_t FAudioCreate(void** a, uint32_t b, uint32_t c) {
    *a = malloc(64);
    return 0;
}
uint32_t FAudio_CreateMasteringVoice(void* a, void** b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, void* g) {
    *b = malloc(64);
    return 0;
}
uint32_t FAudio_StartEngine(void* a) {
    return 0;
}
uint32_t FAudio_GetDeviceCount(void* a, uint32_t* b) {
    *b = 1;
    return 0;
}
uint32_t FAudio_GetDeviceDetails(void* a, uint32_t b, void* c) {
    return 0;
}
uint32_t FAudio_AddRef(void* a) {
    return 1;
}
uint32_t FAudio_Release(void* a) {
    return 0;
}
uint32_t FAudio_Initialize(void* a, uint32_t b, uint32_t c) {
    return 0;
}
uint32_t FAudio_CommitChanges(void* a, uint32_t b) {
    return 0;
}
uint32_t FAudioLinkedVersion(void) {
    return 0;
}
void FAudioVoice_DestroyVoice(void* a) {
}
uint32_t FAudioVoice_SetVolume(void* a, float b, uint32_t c) {
    return 0;
}
uint32_t FAudioVoice_SetChannelVolumes(void* a, uint32_t b, float* c, uint32_t d) {
    return 0;
}
uint32_t FAudioVoice_SetOutputMatrix(void* a, void* b, uint32_t c, uint32_t d, float* e, uint32_t f) {
    return 0;
}
uint32_t FAudioVoice_SetOutputVoices(void* a, void* b) {
    return 0;
}
uint32_t FAudioVoice_SetEffectChain(void* a, void* b) {
    return 0;
}
uint32_t FAudioVoice_EnableEffect(void* a, uint32_t b, uint32_t c) {
    return 0;
}
uint32_t FAudioVoice_DisableEffect(void* a, uint32_t b, uint32_t c) {
    return 0;
}
void FAudioVoice_GetEffectState(void* a, uint32_t b, int* c) {
    *c = 0;
}
uint32_t FAudioVoice_GetEffectParameters(void* a, uint32_t b, void* c, uint32_t d) {
    return 0;
}
uint32_t FAudioVoice_SetEffectParameters(void* a, uint32_t b, void* c, uint32_t d, uint32_t e) {
    return 0;
}
uint32_t FAudioVoice_SetFilterParameters(void* a, void* b, uint32_t c) {
    return 0;
}
uint32_t FAudioVoice_SetOutputFilterParameters(void* a, void* b, void* c, uint32_t d) {
    return 0;
}
void FAudioVoice_GetVoiceDetails(void* a, void* b) {
}
void FAudioVoice_GetVolume(void* a, float* b) {
    *b = 1.0f;
}
void FAudioVoice_GetChannelVolumes(void* a, uint32_t b, float* c) {
}
void FAudioVoice_GetFilterParameters(void* a, void* b) {
}
void FAudioVoice_GetOutputFilterParameters(void* a, void* b, void* c) {
}
void FAudioVoice_GetOutputMatrix(void* a, void* b, uint32_t c, uint32_t d, float* e) {
}
uint32_t FAudioVoice_DestroyVoiceSafeEXT(void* a) {
    return 0;
}
uint32_t FAudio_CreateSourceVoice(void* a, void** b, void* c, uint32_t d, float e, void* f, void* g, void* h) {
    *b = malloc(64);
    return 0;
}
uint32_t FAudio_CreateSubmixVoice(void* a, void** b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, void* g, void* h) {
    *b = malloc(64);
    return 0;
}
void FAudio_StopEngine(void* a) {
}
void FAudio_GetPerformanceData(void* a, void* b) {
}
void FAudio_SetDebugConfiguration(void* a, void* b, void* c) {
}
void FAudio_GetProcessingQuantum(void* a, uint32_t* b, uint32_t* c) {
    *b = 1;
    *c = 48000;
}
void FAudio_UnregisterForCallbacks(void* a, void* b) {
}
uint32_t FAudio_RegisterForCallbacks(void* a, void* b) {
    return 0;
}
void FAudio_Destroy(void* a) {
    free(a);
}
uint32_t FAudioSourceVoice_Start(void* a, uint32_t b, uint32_t c) {
    return 0;
}
uint32_t FAudioSourceVoice_Stop(void* a, uint32_t b, uint32_t c) {
    return 0;
}
uint32_t FAudioSourceVoice_SubmitSourceBuffer(void* a, void* b, void* c) {
    return 0;
}
uint32_t FAudioSourceVoice_FlushSourceBuffers(void* a) {
    return 0;
}
uint32_t FAudioSourceVoice_ExitLoop(void* a, uint32_t b) {
    return 0;
}
uint32_t FAudioSourceVoice_SetFrequencyRatio(void* a, float b, uint32_t c) {
    return 0;
}
uint32_t FAudioSourceVoice_SetSourceSampleRate(void* a, uint32_t b) {
    return 0;
}
uint32_t FAudioSourceVoice_Discontinuity(void* a) {
    return 0;
}
void FAudioSourceVoice_GetState(void* a, void* b, uint32_t c) {
}
void FAudioSourceVoice_GetFrequencyRatio(void* a, float* b) {
    *b = 1.0f;
}
void F3DAudioInitialize(uint32_t a, float b, void* c) {
}
uint32_t F3DAudioInitialize8(uint32_t a, float b, void* c) {
    return 0;
}
void F3DAudioCalculate(void* a, void* b, void* c, uint32_t d, void* e) {
}
uint32_t FACTCreateEngine(uint32_t a, void** b) {
    *b = malloc(64);
    return 0;
}
uint32_t FACTCreateEngineWithCustomAllocatorEXT(uint32_t a, void** b, void* c, void* d, void* e) {
    *b = malloc(64);
    return 0;
}
uint32_t FACTAudioEngine_Initialize(void* a, void* b) {
    return 0;
}
void FACTAudioEngine_DoWork(void* a) {
}
uint32_t FACTAudioEngine_GetFinalMixFormat(void* a, void* b) {
    return 0;
}
float FACTAudioEngine_GetGlobalVariable(void* a, uint16_t b) {
    return 0.0f;
}
void FACTAudioEngine_SetGlobalVariable(void* a, uint16_t b, float c) {
}
uint32_t FACTAudioEngine_GetRendererCount(void* a, uint16_t* b) {
    *b = 1;
    return 0;
}
uint32_t FACTAudioEngine_GetRendererDetails(void* a, uint16_t b, void* c) {
    return 0;
}
uint16_t FACTAudioEngine_GetCategory(void* a, const char* b) {
    return 0;
}
uint16_t FACTAudioEngine_GetGlobalVariableIndex(void* a, const char* b) {
    return 0;
}
void FACTAudioEngine_Pause(void* a, uint16_t b, int c) {
}
void FACTAudioEngine_SetVolume(void* a, uint16_t b, float c) {
}
uint32_t FACTAudioEngine_ShutDown(void* a) {
    return 0;
}
void FACTAudioEngine_Stop(void* a, uint16_t b, uint32_t c) {
}
void* FACTAudioEngine_CreateSoundBank(void* a, void* b, uint32_t c, uint32_t d, uint32_t e, void** f) {
    *f = malloc(64);
    return 0;
}
void* FACTAudioEngine_CreateInMemoryWaveBank(void* a, void* b, uint32_t c, uint32_t d, void** e) {
    *e = malloc(64);
    return 0;
}
void* FACTAudioEngine_CreateStreamingWaveBank(void* a, void* b, void** c) {
    *c = malloc(64);
    return 0;
}
void* FACTAudioEngine_PrepareWave(void* a, uint16_t b, void* c, uint32_t d, uint32_t e, uint32_t f, void** g) {
    *g = malloc(64);
    return 0;
}
void* FACTAudioEngine_PrepareInMemoryWave(void* a, uint16_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, void** g) {
    *g = malloc(64);
    return 0;
}
void* FACTAudioEngine_PrepareStreamingWave(void* a, uint16_t b, void* c, uint32_t d, uint32_t e, uint32_t f, void** g) {
    *g = malloc(64);
    return 0;
}
uint32_t FACTAudioEngine_RegisterNotification(void* a, void* b) {
    return 0;
}
uint32_t FACTAudioEngine_UnRegisterNotification(void* a, void* b) {
    return 0;
}
uint32_t FACTAudioEngine_Release(void* a) {
    return 0;
}
uint32_t FACTAudioEngine_AddRef(void* a) {
    return 1;
}
uint32_t FACT3DInitialize(void* a) {
    return 0;
}
uint32_t FACT3DCalculate(void* a, void* b, void* c) {
    return 0;
}
uint32_t FACT3DApply(void* a, void* b) {
    return 0;
}
uint16_t FACTSoundBank_GetCueIndex(void* a, const char* b) {
    return 0;
}
uint32_t FACTSoundBank_GetNumCues(void* a, uint16_t* b) {
    *b = 0;
    return 0;
}
uint32_t FACTSoundBank_GetCueProperties(void* a, uint16_t b, void* c) {
    return 0;
}
uint32_t FACTSoundBank_GetState(void* a, uint32_t* b) {
    *b = 4;
    return 0;
}
uint16_t FACTSoundBank_Play(void* a, uint16_t b, uint32_t c, uint32_t d, void** e) {
    *e = malloc(64);
    return 0;
}
uint32_t FACTSoundBank_Play3D(void* a, uint16_t b, uint32_t c, uint32_t d, void* e, void* f, void** g) {
    *g = malloc(64);
    return 0;
}
void* FACTSoundBank_Prepare(void* a, uint16_t b, uint32_t c, uint32_t d, void** e) {
    *e = malloc(64);
    return 0;
}
uint32_t FACTSoundBank_Stop(void* a, uint16_t b, uint32_t c) {
    return 0;
}
uint32_t FACTSoundBank_Destroy(void* a) {
    return 0;
}
uint16_t FACTCue_GetVariableIndex(void* a, const char* b) {
    return 0;
}
uint32_t FACTCue_GetState(void* a, uint32_t* b) {
    *b = 0;
    return 0;
}
uint32_t FACTCue_GetProperties(void* a, void* b) {
    return 0;
}
float FACTCue_GetVariable(void* a, uint16_t b) {
    return 0.0f;
}
uint32_t FACTCue_SetVariable(void* a, uint16_t b, float c) {
    return 0;
}
uint32_t FACTCue_SetOutputVoices(void* a, void* b) {
    return 0;
}
uint32_t FACTCue_SetOutputVoiceMatrix(void* a, void* b, uint32_t c, uint32_t d, float* e) {
    return 0;
}
uint32_t FACTCue_SetMatrixCoefficients(void* a, uint32_t b, uint32_t c, float* d) {
    return 0;
}
uint32_t FACTCue_Play(void* a) {
    return 0;
}
uint32_t FACTCue_Pause(void* a, int b) {
    return 0;
}
uint32_t FACTCue_Stop(void* a, uint32_t b) {
    return 0;
}
uint32_t FACTCue_Destroy(void* a) {
    return 0;
}
uint16_t FACTWaveBank_GetWaveIndex(void* a, const char* b) {
    return 0;
}
uint32_t FACTWaveBank_GetNumWaves(void* a, uint16_t* b) {
    *b = 0;
    return 0;
}
uint32_t FACTWaveBank_GetWaveProperties(void* a, uint16_t b, void* c) {
    return 0;
}
uint32_t FACTWaveBank_GetState(void* a, uint32_t* b) {
    *b = 4;
    return 0;
}
uint16_t FACTWaveBank_Play(void* a, uint16_t b, uint32_t c, uint32_t d, void** e) {
    *e = malloc(64);
    return 0;
}
void* FACTWaveBank_Prepare(void* a, uint16_t b, uint32_t c, uint32_t d, void** e) {
    *e = malloc(64);
    return 0;
}
uint32_t FACTWaveBank_Stop(void* a, uint16_t b, uint32_t c) {
    return 0;
}
uint32_t FACTWaveBank_Destroy(void* a) {
    return 0;
}
uint32_t FACTWave_GetState(void* a, uint32_t* b) {
    *b = 4;
    return 0;
}
uint32_t FACTWave_GetProperties(void* a, void* b) {
    return 0;
}
uint32_t FACTWave_Play(void* a) {
    return 0;
}
uint32_t FACTWave_Pause(void* a, int b) {
    return 0;
}
uint32_t FACTWave_Stop(void* a, uint32_t b) {
    return 0;
}
uint32_t FACTWave_SetVolume(void* a, float b) {
    return 0;
}
uint32_t FACTWave_SetPitch(void* a, int16_t b) {
    return 0;
}
uint32_t FACTWave_SetMatrixCoefficients(void* a, uint32_t b, uint32_t c, float* d) {
    return 0;
}
uint32_t FACTWave_Destroy(void* a) {
    return 0;
}
uint32_t FAudioCreateReverb(void** a) {
    *a = malloc(64);
    return 0;
}
uint32_t FAudioCreateReverb9(void** a) {
    *a = malloc(64);
    return 0;
}
uint32_t FAudioCreateCollectorEXT(void** a, uint32_t b, uint32_t c) {
    *a = malloc(64);
    return 0;
}
uint32_t FAudioCreateCollectorWithCustomAllocatorEXT(void** a, uint32_t b, uint32_t c, void* d, void* e, void* f) {
    *a = malloc(64);
    return 0;
}
void* FAudio_fopen(const char* a) {
    return NULL;
}
void* FAudio_memopen(void* a, int b) {
    return NULL;
}
void* FAudio_memptr(void* a, int b) {
    return NULL;
}
void FAudio_close(void* a) {
}
