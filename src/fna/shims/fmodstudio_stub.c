#include <string.h>
#include <stdlib.h>

long FMOD_Studio_Bank_GetBusCount() { return 0; }
long FMOD_Studio_Bank_GetBusList() { return 0; }
long FMOD_Studio_Bank_GetEventCount() { return 0; }
long FMOD_Studio_Bank_GetEventList() { return 0; }
long FMOD_Studio_Bank_GetID() { return 0; }
long FMOD_Studio_Bank_GetLoadingState() { return 0; }
int FMOD_Studio_Bank_GetPath(void *bank, char *path, int size, int *len) { if (path && size > 0) path[0] = 0; if (len) *len = 1; return 0; }
long FMOD_Studio_Bank_GetSampleLoadingState() { return 0; }
long FMOD_Studio_Bank_GetStringCount() { return 0; }
long FMOD_Studio_Bank_GetStringInfo() { return 0; }
long FMOD_Studio_Bank_GetUserData() { return 0; }
long FMOD_Studio_Bank_GetVCACount() { return 0; }
long FMOD_Studio_Bank_GetVCAList() { return 0; }
long FMOD_Studio_Bank_IsValid() { return 0; }
long FMOD_Studio_Bank_LoadSampleData() { return 0; }
long FMOD_Studio_Bank_SetUserData() { return 0; }
long FMOD_Studio_Bank_Unload() { return 0; }
long FMOD_Studio_Bank_UnloadSampleData() { return 0; }
long FMOD_Studio_Bus_GetChannelGroup() { return 0; }
long FMOD_Studio_Bus_GetID() { return 0; }
long FMOD_Studio_Bus_GetMute() { return 0; }
int FMOD_Studio_Bus_GetPath(void *bus, char *path, int size, int *len) { if (path && size > 0) path[0] = 0; if (len) *len = 1; return 0; }
long FMOD_Studio_Bus_GetPaused() { return 0; }
long FMOD_Studio_Bus_GetVolume() { return 0; }
long FMOD_Studio_Bus_IsValid() { return 0; }
long FMOD_Studio_Bus_LockChannelGroup() { return 0; }
long FMOD_Studio_Bus_SetMute() { return 0; }
long FMOD_Studio_Bus_SetPaused() { return 0; }
long FMOD_Studio_Bus_SetVolume() { return 0; }
long FMOD_Studio_Bus_StopAllEvents() { return 0; }
long FMOD_Studio_Bus_UnlockChannelGroup() { return 0; }
long FMOD_Studio_CommandReplay_GetCommandAtTime() { return 0; }
long FMOD_Studio_CommandReplay_GetCommandCount() { return 0; }
long FMOD_Studio_CommandReplay_GetCommandInfo() { return 0; }
long FMOD_Studio_CommandReplay_GetCommandString() { return 0; }
long FMOD_Studio_CommandReplay_GetCurrentCommand() { return 0; }
long FMOD_Studio_CommandReplay_GetLength() { return 0; }
long FMOD_Studio_CommandReplay_GetPaused() { return 0; }
long FMOD_Studio_CommandReplay_GetPlaybackState() { return 0; }
long FMOD_Studio_CommandReplay_GetSystem() { return 0; }
long FMOD_Studio_CommandReplay_GetUserData() { return 0; }
long FMOD_Studio_CommandReplay_IsValid() { return 0; }
long FMOD_Studio_CommandReplay_Release() { return 0; }
long FMOD_Studio_CommandReplay_SeekToCommand() { return 0; }
long FMOD_Studio_CommandReplay_SeekToTime() { return 0; }
long FMOD_Studio_CommandReplay_SetBankPath() { return 0; }
long FMOD_Studio_CommandReplay_SetCreateInstanceCallback() { return 0; }
long FMOD_Studio_CommandReplay_SetFrameCallback() { return 0; }
long FMOD_Studio_CommandReplay_SetLoadBankCallback() { return 0; }
long FMOD_Studio_CommandReplay_SetPaused() { return 0; }
long FMOD_Studio_CommandReplay_SetUserData() { return 0; }
long FMOD_Studio_CommandReplay_Start() { return 0; }
long FMOD_Studio_CommandReplay_Stop() { return 0; }
int FMOD_Studio_EventDescription_CreateInstance(void *desc, void **instance) { if (instance) *instance = malloc(1); return 0; }
long FMOD_Studio_EventDescription_GetID() { return 0; }
long FMOD_Studio_EventDescription_GetInstanceCount() { return 0; }
long FMOD_Studio_EventDescription_GetInstanceList() { return 0; }
int FMOD_Studio_EventDescription_GetLength(void *desc, int *length) { if (length) *length = 0; return 0; }
long FMOD_Studio_EventDescription_GetMaximumDistance() { return 0; }
long FMOD_Studio_EventDescription_GetMinimumDistance() { return 0; }
long FMOD_Studio_EventDescription_GetParameter() { return 0; }
long FMOD_Studio_EventDescription_GetParameterByIndex() { return 0; }
long FMOD_Studio_EventDescription_GetParameterCount() { return 0; }
int FMOD_Studio_EventDescription_GetPath(void *desc, char *path, int size, int *len) { if (path && size > 0) path[0] = 0; if (len) *len = 1; return 0; }
long FMOD_Studio_EventDescription_GetSampleLoadingState() { return 0; }
long FMOD_Studio_EventDescription_GetSoundSize() { return 0; }
long FMOD_Studio_EventDescription_GetUserData() { return 0; }
long FMOD_Studio_EventDescription_GetUserProperty() { return 0; }
long FMOD_Studio_EventDescription_GetUserPropertyByIndex() { return 0; }
long FMOD_Studio_EventDescription_GetUserPropertyCount() { return 0; }
long FMOD_Studio_EventDescription_HasCue() { return 0; }
int FMOD_Studio_EventDescription_Is3D(void *desc, int *is3d) { if (is3d) *is3d = 0; return 0; }
int FMOD_Studio_EventDescription_IsOneshot(void *desc, int *oneshot) { if (oneshot) *oneshot = 0; return 0; }
long FMOD_Studio_EventDescription_IsSnapshot() { return 0; }
long FMOD_Studio_EventDescription_IsStream() { return 0; }
long FMOD_Studio_EventDescription_IsValid() { return 0; }
long FMOD_Studio_EventDescription_LoadSampleData() { return 0; }
long FMOD_Studio_EventDescription_ReleaseAllInstances() { return 0; }
long FMOD_Studio_EventDescription_SetCallback() { return 0; }
long FMOD_Studio_EventDescription_SetUserData() { return 0; }
long FMOD_Studio_EventDescription_UnloadSampleData() { return 0; }
long FMOD_Studio_EventInstance_Get3DAttributes() { return 0; }
long FMOD_Studio_EventInstance_GetChannelGroup() { return 0; }
long FMOD_Studio_EventInstance_GetDescription() { return 0; }
long FMOD_Studio_EventInstance_GetListenerMask() { return 0; }
long FMOD_Studio_EventInstance_GetParameter() { return 0; }
long FMOD_Studio_EventInstance_GetParameterByIndex() { return 0; }
long FMOD_Studio_EventInstance_GetParameterCount() { return 0; }
long FMOD_Studio_EventInstance_GetParameterValue() { return 0; }
long FMOD_Studio_EventInstance_GetParameterValueByIndex() { return 0; }
long FMOD_Studio_EventInstance_GetPaused() { return 0; }
int FMOD_Studio_EventInstance_GetPitch(void *inst, float *pitch, float *finalPitch) { if (pitch) *pitch = 1.0f; if (finalPitch) *finalPitch = 1.0f; return 0; }
long FMOD_Studio_EventInstance_GetPlaybackState() { return 0; }
long FMOD_Studio_EventInstance_GetProperty() { return 0; }
long FMOD_Studio_EventInstance_GetReverbLevel() { return 0; }
long FMOD_Studio_EventInstance_GetTimelinePosition() { return 0; }
long FMOD_Studio_EventInstance_GetUserData() { return 0; }
int FMOD_Studio_EventInstance_GetVolume(void *inst, float *vol, float *finalVol) { if (vol) *vol = 1.0f; if (finalVol) *finalVol = 1.0f; return 0; }
long FMOD_Studio_EventInstance_IsValid() { return 0; }
long FMOD_Studio_EventInstance_IsVirtual() { return 0; }
int FMOD_Studio_EventInstance_Release(void *inst) { return 0; }
long FMOD_Studio_EventInstance_Set3DAttributes() { return 0; }
long FMOD_Studio_EventInstance_SetCallback() { return 0; }
long FMOD_Studio_EventInstance_SetListenerMask() { return 0; }
int FMOD_Studio_EventInstance_SetParameterValue(void *inst, const char *name, float value) { return 0; }
long FMOD_Studio_EventInstance_SetParameterValueByIndex() { return 0; }
long FMOD_Studio_EventInstance_SetParameterValuesByIndices() { return 0; }
long FMOD_Studio_EventInstance_SetPaused() { return 0; }
int FMOD_Studio_EventInstance_SetPitch(void *inst, float pitch) { return 0; }
long FMOD_Studio_EventInstance_SetProperty() { return 0; }
long FMOD_Studio_EventInstance_SetReverbLevel() { return 0; }
long FMOD_Studio_EventInstance_SetTimelinePosition() { return 0; }
long FMOD_Studio_EventInstance_SetUserData() { return 0; }
int FMOD_Studio_EventInstance_SetVolume(void *inst, float vol) { return 0; }
int FMOD_Studio_EventInstance_Start(void *inst) { return 0; }
int FMOD_Studio_EventInstance_Stop(void *inst, int mode) { return 0; }
long FMOD_Studio_EventInstance_TriggerCue() { return 0; }
long FMOD_Studio_ParameterInstance_GetDescription() { return 0; }
long FMOD_Studio_ParameterInstance_GetValue() { return 0; }
long FMOD_Studio_ParameterInstance_IsValid() { return 0; }
long FMOD_Studio_ParameterInstance_SetValue() { return 0; }
long FMOD_Studio_ParseID() { return 0; }
int FMOD_Studio_System_Create(void **system, unsigned int version) { if (system) *system = malloc(1); return 0; }
long FMOD_Studio_System_FlushCommands() { return 0; }
long FMOD_Studio_System_FlushSampleLoading() { return 0; }
long FMOD_Studio_System_GetAdvancedSettings() { return 0; }
long FMOD_Studio_System_GetBank() { return 0; }
long FMOD_Studio_System_GetBankByID() { return 0; }
int FMOD_Studio_System_GetBankCount(void *system, int *count) { if (count) *count = 0; return 0; }
long FMOD_Studio_System_GetBankList() { return 0; }
long FMOD_Studio_System_GetBufferUsage() { return 0; }
int FMOD_Studio_System_GetBus(void *system, const char *path, void **bus) { if (bus) *bus = malloc(1); return 0; }
long FMOD_Studio_System_GetBusByID() { return 0; }
long FMOD_Studio_System_GetCPUUsage() { return 0; }
int FMOD_Studio_System_GetEvent(void *system, const char *path, void **eventDesc) { if (eventDesc) *eventDesc = malloc(1); return 0; }
long FMOD_Studio_System_GetEventByID() { return 0; }
long FMOD_Studio_System_GetListenerAttributes() { return 0; }
long FMOD_Studio_System_GetListenerWeight() { return 0; }
long FMOD_Studio_System_GetLowLevelSystem() { return 0; }
long FMOD_Studio_System_GetNumListeners() { return 0; }
long FMOD_Studio_System_GetSoundInfo() { return 0; }
long FMOD_Studio_System_GetUserData() { return 0; }
int FMOD_Studio_System_GetVCA(void *system, const char *path, void **vca) { if (vca) *vca = malloc(1); return 0; }
long FMOD_Studio_System_GetVCAByID() { return 0; }
int FMOD_Studio_System_Initialize(void *system, int maxChannels, int studioInitFlags, int initFlags, void *extraData) { return 0; }
long FMOD_Studio_System_IsValid() { return 0; }
long FMOD_Studio_System_LoadBankCustom() { return 0; }
int FMOD_Studio_System_LoadBankFile(void *system, const char *path, int flags, void **bank) { if (bank) *bank = malloc(1); return 0; }
long FMOD_Studio_System_LoadBankMemory() { return 0; }
long FMOD_Studio_System_LoadCommandReplay() { return 0; }
long FMOD_Studio_System_LookupID() { return 0; }
int FMOD_Studio_System_LookupPath(void *sys, void *id, char *path, int size, int *len) { if (path && size > 0) path[0] = 0; if (len) *len = 1; return 0; }
long FMOD_Studio_System_Release() { return 0; }
long FMOD_Studio_System_ResetBufferUsage() { return 0; }
long FMOD_Studio_System_SetAdvancedSettings() { return 0; }
long FMOD_Studio_System_SetCallback() { return 0; }
long FMOD_Studio_System_SetListenerAttributes() { return 0; }
long FMOD_Studio_System_SetListenerWeight() { return 0; }
long FMOD_Studio_System_SetNumListeners() { return 0; }
long FMOD_Studio_System_SetUserData() { return 0; }
long FMOD_Studio_System_StartCommandCapture() { return 0; }
long FMOD_Studio_System_StopCommandCapture() { return 0; }
long FMOD_Studio_System_UnloadAll() { return 0; }
int FMOD_Studio_System_Update(void *system) { return 0; }
long FMOD_Studio_VCA_GetID() { return 0; }
int FMOD_Studio_VCA_GetPath(void *vca, char *path, int size, int *len) { if (path && size > 0) path[0] = 0; if (len) *len = 1; return 0; }
long FMOD_Studio_VCA_GetVolume() { return 0; }
long FMOD_Studio_VCA_IsValid() { return 0; }
long FMOD_Studio_VCA_SetVolume() { return 0; }
