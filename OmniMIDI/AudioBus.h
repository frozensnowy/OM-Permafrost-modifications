/*
OmniMIDI Audio Bus V2 - Shared memory for Permafrost

TL;DR:

Permafrost can read/write:
- Audio levels (per-channel + master)
- Voice counts
- Panic flags
- 16ch audio OUT (we send to Permafrost)
- Stereo IN (Permafrost sends back after VST processing)

Audio goes: MIDI -> synthesis -> 16ch out -> Permafrost does VST stuff -> stereo back -> speakers
If Permafrost isn't there, we just output directly. No drama.
*/
#pragma once

#include <windows.h>
#include <stdio.h>
#include <math.h>

// Config

// Shared memory names - Permafrost connects to these
#define AUDIOBUS_SHARED_MEM_NAME L"OmniMIDI_AudioBus"
#define AUDIOBUS_MUTEX_NAME L"OmniMIDI_AudioBusMutex"

// Event names for audio sync (appended with PID for multi-instance)
#define AUDIOBUS_EVENT_AUDIO_READY L"OmniMIDI_AudioReady"
#define AUDIOBUS_EVENT_PROCESSED_READY L"OmniMIDI_ProcessedReady"

// Protocol version - bump this when structure changes
#define AUDIOBUS_VERSION 2

// Audio buffer config
#define AUDIOBUS_NUM_CHANNELS 16
#define AUDIOBUS_BUFFER_SAMPLES 2048 // Samples per buffer (increased for larger BASS buffers)
#define AUDIOBUS_SAMPLE_SIZE 4       // sizeof(float)
#define AUDIOBUS_STEREO 2            // L + R
#define AUDIOBUS_RING_SIZE 8         // Keep small for now
#define AUDIOBUS_RING_PREFILL 4      // Keep small for now

// Timeout values
#define AUDIOBUS_TAKEOVER_TIMEOUT_MS 100  // How long to wait for Permafrost to respond
#define AUDIOBUS_HEARTBEAT_TIMEOUT_MS 500 // How long before we assume Permafrost crashed
#define AUDIOBUS_FRAME_TIMEOUT_MS 50      // How long to wait for processed audio

// Peak level decay rate (makes the meters smooth)
#define AUDIOBUS_LEVEL_DECAY 0.92f

// Flags and enums

// Flags for the shared memory header
#define AUDIOBUS_FLAG_ACTIVE 0x0001        // OmniMIDI is running and streaming
#define AUDIOBUS_FLAG_PANIC_REQUEST 0x0002 // Permafrost wants us to panic (all notes off)
#define AUDIOBUS_FLAG_PANIC_ACK 0x0004     // We acknowledged the panic request
#define AUDIOBUS_FLAG_AUDIO_ENABLED 0x0008 // Audio streaming is enabled
#define AUDIOBUS_FLAG_VST_ACTIVE 0x0010    // Permafrost has VST plugins loaded

// Takeover state - controls audio routing
typedef enum AudioBusTakeoverState
{
    TAKEOVER_DIRECT = 0,   // Normal mode - OmniMIDI outputs directly to speakers
    TAKEOVER_PENDING = 1,  // Permafrost requested takeover, waiting for frame boundary
    TAKEOVER_ACTIVE = 2,   // Roundtrip mode - audio goes through Permafrost
    TAKEOVER_RELEASING = 3 // Returning to direct mode, finishing current frame
} AudioBusTakeoverState;

// Data structures

#pragma pack(push, 1)

// Per-channel metadata
typedef struct AudioBusChannelInfo
{
    float PeakLevelL;  // Left channel peak level (0.0 - 1.0)
    float PeakLevelR;  // Right channel peak level (0.0 - 1.0)
    DWORD VoiceCount;  // Active voices on this channel
    DWORD Reserved[2]; // Future use (RMS, etc)
} AudioBusChannelInfo;

// Main shared memory header
typedef struct AudioBusHeader
{
    // ---- IDENTIFICATION ----
    char Magic[4];   // "OMAB" = OmniMIDI Audio Bus
    DWORD Version;   // Protocol version (currently 3)
    DWORD ProcessID; // OmniMIDI's process ID (for multi-instance)

    // ---- AUDIO FORMAT ----
    DWORD SampleRate;  // Current sample rate (e.g. 48000)
    DWORD BufferSize;  // Samples per buffer (e.g. 256)
    DWORD NumChannels; // Always 16 for MIDI

    // ---- STATUS FLAGS ----
    DWORD Flags;                         // AUDIOBUS_FLAG_* values
    AudioBusTakeoverState TakeoverState; // Current audio routing state
    ULONGLONG HeartbeatCounter;          // Incremented every audio frame
    ULONGLONG Timestamp;                 // GetTickCount64 when last updated

    // ---- MASTER LEVELS (post-mix) ----
    float MasterPeakL; // Master left peak (0.0 - 1.0)
    float MasterPeakR; // Master right peak (0.0 - 1.0)
    DWORD TotalVoices; // Total active voices across all channels
    float CPUUsage;    // Rendering CPU percentage

    // ---- LATENCY TIMESTAMPS (QPC ticks) ----
    ULONGLONG QPCFrequency;           // Ticks per second for conversion
    ULONGLONG LastMidiEventTime;      // When MIDI event was received
    ULONGLONG LastSynthCompleteTime;  // When synthesis buffer was filled
    ULONGLONG LastAudioOutputTime;    // When audio was sent to device
    ULONGLONG LastSharedMemWriteTime; // When we wrote to SharedMem OUT
    ULONGLONG LastSharedMemReadTime;  // When we read from SharedMem IN

    // ---- LATENCY INFO (microseconds) ----
    DWORD OutputBufferLatencyUs; // Output buffer latency
    DWORD AsioInputLatencyUs;    // ASIO input latency (0 for non-ASIO)
    DWORD PermafrostLatencyUs;   // VST processing latency (from Permafrost)
    DWORD CurrentEngine;         // 0=WAV, 1=DS, 2=ASIO, 3=WASAPI, 4=XAudio

    // ---- DOUBLE BUFFER INDICES ----
    // Each side writes to one buffer while the other reads from the other
    // Values are 0 or 1, indicating which buffer to use
    volatile LONG OutWriteIndex; // OmniMIDI writes 16ch here (0 or 1)
    volatile LONG OutReadIndex;  // Permafrost reads from here
    volatile LONG InWriteIndex;  // Permafrost writes stereo here
    volatile LONG InReadIndex;   // OmniMIDI reads from here

    // ---- FRAME COUNTERS ----
    // These help sync and detect dropped frames
    volatile ULONGLONG OutFrameCounter; // Frames written to OUT
    volatile ULONGLONG InFrameCounter;  // Frames written to IN

    // ---- CURRENT FRAME INFO ----
    // Per-frame info that changes with each audio callback
    volatile DWORD CurrentFrameSamples; // Actual stereo samples in current frame (varies per callback)
    DWORD Reserved2[3];                 // Padding for alignment

    // ---- PER-CHANNEL INFO ----
    AudioBusChannelInfo Channels[AUDIOBUS_NUM_CHANNELS];

    // ---- RESERVED ----
    BYTE Reserved[64]; // Future expansion without version bump

} AudioBusHeader;

// Audio buffer regions
// Each buffer holds AUDIOBUS_BUFFER_SAMPLES of stereo float data
// Double-buffered: A and B for lock-free operation

// Size of one channel's stereo buffer
#define AUDIOBUS_CHANNEL_BUFFER_SIZE (AUDIOBUS_BUFFER_SAMPLES * AUDIOBUS_STEREO * AUDIOBUS_SAMPLE_SIZE)

// 16-channel OUT region (OmniMIDI -> Permafrost)
// Layout: [Ch0 BufferA][Ch0 BufferB][Ch1 BufferA][Ch1 BufferB]...
#define AUDIOBUS_OUT_BUFFER_SIZE (AUDIOBUS_NUM_CHANNELS * 2 * AUDIOBUS_CHANNEL_BUFFER_SIZE)

// Stereo IN region (Permafrost -> OmniMIDI)
// Layout: [Stereo BufferA][Stereo BufferB]
#define AUDIOBUS_IN_BUFFER_SIZE (2 * AUDIOBUS_CHANNEL_BUFFER_SIZE)

// Total shared memory size
#define AUDIOBUS_HEADER_SIZE sizeof(AudioBusHeader)
#define AUDIOBUS_TOTAL_SIZE (AUDIOBUS_HEADER_SIZE + AUDIOBUS_OUT_BUFFER_SIZE + AUDIOBUS_IN_BUFFER_SIZE)

// Offsets into shared memory
#define AUDIOBUS_OUT_OFFSET AUDIOBUS_HEADER_SIZE
#define AUDIOBUS_IN_OFFSET (AUDIOBUS_HEADER_SIZE + AUDIOBUS_OUT_BUFFER_SIZE)

#pragma pack(pop)

// Global state

static HANDLE g_AudioBusMapping = NULL;
static BYTE *g_AudioBusBasePtr = NULL;       // Base pointer to mapped memory
static AudioBusHeader *g_AudioBusPtr = NULL; // Header pointer
static float *g_AudioBusOutPtr = NULL;       // 16-channel OUT region
static float *g_AudioBusInPtr = NULL;        // Stereo IN region
static HANDLE g_AudioBusMutex = NULL;
static BOOL g_AudioBusInitialized = FALSE;

// Event handles for audio sync
static HANDLE g_AudioReadyEvent = NULL;     // Signals: 16ch buffer is ready
static HANDLE g_ProcessedReadyEvent = NULL; // Signals: stereo buffer is ready

// DSP handles for per-channel capture
static HDSP g_ChannelDSPHandles[AUDIOBUS_NUM_CHANNELS] = {0};
static HSTREAM g_ChannelStreams[AUDIOBUS_NUM_CHANNELS] = {0};
static BOOL g_ChannelDSPActive = FALSE;

// Local peak level smoothing
static float g_ChannelPeaksL[AUDIOBUS_NUM_CHANNELS] = {0};
static float g_ChannelPeaksR[AUDIOBUS_NUM_CHANNELS] = {0};
static float g_MasterPeakL = 0.0f;
static float g_MasterPeakR = 0.0f;

// QPC frequency cache
static ULONGLONG g_QPCFrequency = 0;

// Temporary buffer for accumulating channel audio before write
static float g_ChannelAccumBuffer[AUDIOBUS_NUM_CHANNELS][AUDIOBUS_BUFFER_SAMPLES * AUDIOBUS_STEREO] = {0};
static DWORD g_ChannelAccumSamples[AUDIOBUS_NUM_CHANNELS] = {0};

// Helper functions

// Get current QPC timestamp
static inline ULONGLONG AudioBus_GetQPCTicks()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

// Convert QPC ticks to microseconds
static inline ULONGLONG AudioBus_TicksToMicroseconds(ULONGLONG ticks)
{
    if (g_QPCFrequency == 0)
        return 0;
    return (ticks * 1000000ULL) / g_QPCFrequency;
}

// Get pointer to specific channel's buffer (A or B)
static inline float *AudioBus_GetOutChannelBuffer(int channel, int bufferIndex)
{
    if (!g_AudioBusOutPtr || channel < 0 || channel >= AUDIOBUS_NUM_CHANNELS)
        return NULL;

    // Each channel has 2 buffers (A and B), each with AUDIOBUS_BUFFER_SAMPLES * 2 floats
    int channelOffset = channel * 2 * (AUDIOBUS_BUFFER_SAMPLES * AUDIOBUS_STEREO);
    int bufferOffset = bufferIndex * (AUDIOBUS_BUFFER_SAMPLES * AUDIOBUS_STEREO);

    return g_AudioBusOutPtr + channelOffset + bufferOffset;
}

// Get pointer to stereo IN buffer (A or B)
static inline float *AudioBus_GetInBuffer(int bufferIndex)
{
    if (!g_AudioBusInPtr)
        return NULL;

    int bufferOffset = bufferIndex * (AUDIOBUS_BUFFER_SAMPLES * AUDIOBUS_STEREO);
    return g_AudioBusInPtr + bufferOffset;
}

// Forward declarations

// Core lifecycle
static BOOL AudioBus_Create();
static void AudioBus_Destroy();
static BOOL AudioBus_IsConnected();

// Level updates (existing, still used)
static void AudioBus_UpdateLevels(float masterL, float masterR, DWORD totalVoices, float cpuUsage);
static void AudioBus_UpdateChannelVoices(int channel, DWORD voiceCount);
static void AudioBus_UpdateChannelLevels(int channel, float peakL, float peakR);
static void AudioBus_UpdateAllChannelVoices(DWORD *voiceCounts);

// Panic handling
static BOOL AudioBus_CheckPanicRequest();
static void AudioBus_AcknowledgePanic();
static void AudioBus_RequestPanic();
static void AudioBus_ClearPanicAck();

// Config
static void AudioBus_SetSampleRate(DWORD sampleRate);
static DWORD AudioBus_GetFlags();

// Timestamps
static void AudioBus_RecordMidiEvent();
static void AudioBus_RecordSynthComplete();
static void AudioBus_RecordAudioOutput();
static void AudioBus_UpdateLatencyInfo(DWORD outputLatencyUs, DWORD asioInputLatencyUs, DWORD engine);

// Sprint 1: Audio streaming
static BOOL AudioBus_SetupChannelDSPs(HSTREAM midiStream);
static void AudioBus_RemoveChannelDSPs();
static BOOL AudioBus_IsTakeoverActive();
static AudioBusTakeoverState AudioBus_GetTakeoverState();
static void AudioBus_RequestTakeover();
static void AudioBus_ReleaseTakeover();
static void AudioBus_WriteChannelAudio(int channel, float *buffer, DWORD sampleCount);
static BOOL AudioBus_ReadProcessedAudio(float *buffer, DWORD sampleCount);
static void AudioBus_SignalAudioReady();
static BOOL AudioBus_WaitForProcessedAudio(DWORD timeoutMs);
static void AudioBus_IncrementHeartbeat();
static BOOL AudioBus_CheckPermafrostAlive();

// Core lifecycle

// Set up shared memory + events. Call at init time.
static BOOL AudioBus_Create()
{
    if (g_AudioBusInitialized)
    {
        PrintMessageToDebugLog("AudioBus", "Already initialized, skipping.");
        return TRUE;
    }

    PrintMessageToDebugLog("AudioBus", "Creating shared memory for Permafrost integration...");

    // Get QPC frequency
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_QPCFrequency = freq.QuadPart;

    // Create mutex for synchronization
    g_AudioBusMutex = CreateMutexW(NULL, FALSE, AUDIOBUS_MUTEX_NAME);
    if (g_AudioBusMutex == NULL)
    {
        DWORD err = GetLastError();
        char buf[128];
        sprintf(buf, "Failed to create mutex, error: %lu", err);
        PrintMessageToDebugLog("AudioBus", buf);
        return FALSE;
    }

    // Create the shared memory mapping (larger now with audio buffers)
    g_AudioBusMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        AUDIOBUS_TOTAL_SIZE,
        AUDIOBUS_SHARED_MEM_NAME);

    if (g_AudioBusMapping == NULL)
    {
        DWORD err = GetLastError();
        char buf[128];
        sprintf(buf, "Failed to create file mapping (%lu bytes), error: %lu", AUDIOBUS_TOTAL_SIZE, err);
        PrintMessageToDebugLog("AudioBus", buf);
        CloseHandle(g_AudioBusMutex);
        g_AudioBusMutex = NULL;
        return FALSE;
    }

    // Map into our address space
    g_AudioBusBasePtr = (BYTE *)MapViewOfFile(
        g_AudioBusMapping,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        AUDIOBUS_TOTAL_SIZE);

    if (g_AudioBusBasePtr == NULL)
    {
        DWORD err = GetLastError();
        char buf[128];
        sprintf(buf, "Failed to map view of file, error: %lu", err);
        PrintMessageToDebugLog("AudioBus", buf);
        CloseHandle(g_AudioBusMapping);
        CloseHandle(g_AudioBusMutex);
        g_AudioBusMapping = NULL;
        g_AudioBusMutex = NULL;
        return FALSE;
    }

    // Set up region pointers
    g_AudioBusPtr = (AudioBusHeader *)g_AudioBusBasePtr;
    g_AudioBusOutPtr = (float *)(g_AudioBusBasePtr + AUDIOBUS_OUT_OFFSET);
    g_AudioBusInPtr = (float *)(g_AudioBusBasePtr + AUDIOBUS_IN_OFFSET);

    // Create sync events (auto-reset for clean signaling)
    wchar_t eventName[128];
    DWORD pid = GetCurrentProcessId();

    swprintf(eventName, 128, L"%s_%lu", AUDIOBUS_EVENT_AUDIO_READY, pid);
    g_AudioReadyEvent = CreateEventW(NULL, FALSE, FALSE, eventName);

    swprintf(eventName, 128, L"%s_%lu", AUDIOBUS_EVENT_PROCESSED_READY, pid);
    g_ProcessedReadyEvent = CreateEventW(NULL, FALSE, FALSE, eventName);

    if (!g_AudioReadyEvent || !g_ProcessedReadyEvent)
    {
        PrintMessageToDebugLog("AudioBus", "Failed to create sync events");
        // Clean up partial init
        if (g_AudioReadyEvent)
            CloseHandle(g_AudioReadyEvent);
        if (g_ProcessedReadyEvent)
            CloseHandle(g_ProcessedReadyEvent);
        UnmapViewOfFile(g_AudioBusBasePtr);
        CloseHandle(g_AudioBusMapping);
        CloseHandle(g_AudioBusMutex);
        g_AudioBusBasePtr = NULL;
        g_AudioBusPtr = NULL;
        g_AudioBusOutPtr = NULL;
        g_AudioBusInPtr = NULL;
        g_AudioBusMapping = NULL;
        g_AudioBusMutex = NULL;
        return FALSE;
    }

    // Initialize header
    WaitForSingleObject(g_AudioBusMutex, INFINITE);

    memset(g_AudioBusBasePtr, 0, AUDIOBUS_TOTAL_SIZE);

    // Magic bytes
    g_AudioBusPtr->Magic[0] = 'O';
    g_AudioBusPtr->Magic[1] = 'M';
    g_AudioBusPtr->Magic[2] = 'A';
    g_AudioBusPtr->Magic[3] = 'B';

    g_AudioBusPtr->Version = AUDIOBUS_VERSION;
    g_AudioBusPtr->ProcessID = pid;
    g_AudioBusPtr->SampleRate = ManagedSettings.AudioFrequency;
    g_AudioBusPtr->BufferSize = AUDIOBUS_BUFFER_SAMPLES;
    g_AudioBusPtr->NumChannels = AUDIOBUS_NUM_CHANNELS;
    g_AudioBusPtr->Flags = AUDIOBUS_FLAG_ACTIVE;
    g_AudioBusPtr->TakeoverState = TAKEOVER_DIRECT;
    g_AudioBusPtr->HeartbeatCounter = 0;
    g_AudioBusPtr->Timestamp = GetTickCount64();

    // QPC stuff
    g_AudioBusPtr->QPCFrequency = g_QPCFrequency;
    g_AudioBusPtr->CurrentEngine = ManagedSettings.CurrentEngine;

    // Buffer indices start at 0
    g_AudioBusPtr->OutWriteIndex = 0;
    g_AudioBusPtr->OutReadIndex = 0;
    g_AudioBusPtr->InWriteIndex = 0;
    g_AudioBusPtr->InReadIndex = 0;
    g_AudioBusPtr->OutFrameCounter = 0;
    g_AudioBusPtr->InFrameCounter = 0;

    // Init channel info
    for (int i = 0; i < AUDIOBUS_NUM_CHANNELS; i++)
    {
        g_AudioBusPtr->Channels[i].PeakLevelL = 0.0f;
        g_AudioBusPtr->Channels[i].PeakLevelR = 0.0f;
        g_AudioBusPtr->Channels[i].VoiceCount = 0;
    }

    ReleaseMutex(g_AudioBusMutex);

    g_AudioBusInitialized = TRUE;

    char buf[256];
    sprintf(buf, "Shared memory created successfully. Total size: %lu bytes (Header: %lu, Out: %lu, In: %lu)",
            AUDIOBUS_TOTAL_SIZE, AUDIOBUS_HEADER_SIZE, AUDIOBUS_OUT_BUFFER_SIZE, AUDIOBUS_IN_BUFFER_SIZE);
    PrintMessageToDebugLog("AudioBus", buf);

    return TRUE;
}

// Tear down shared memory
static void AudioBus_Destroy()
{
    if (!g_AudioBusInitialized)
        return;

    PrintMessageToDebugLog("AudioBus", "Destroying shared memory...");

    // Remove any active DSPs first
    AudioBus_RemoveChannelDSPs();

    // Mark as inactive
    if (g_AudioBusPtr && g_AudioBusMutex)
    {
        WaitForSingleObject(g_AudioBusMutex, 100);
        g_AudioBusPtr->Flags = 0;
        g_AudioBusPtr->TakeoverState = TAKEOVER_DIRECT;
        ReleaseMutex(g_AudioBusMutex);
    }

    // Close events
    if (g_AudioReadyEvent)
    {
        CloseHandle(g_AudioReadyEvent);
        g_AudioReadyEvent = NULL;
    }
    if (g_ProcessedReadyEvent)
    {
        CloseHandle(g_ProcessedReadyEvent);
        g_ProcessedReadyEvent = NULL;
    }

    // Unmap and close handles
    if (g_AudioBusBasePtr)
    {
        UnmapViewOfFile(g_AudioBusBasePtr);
        g_AudioBusBasePtr = NULL;
        g_AudioBusPtr = NULL;
        g_AudioBusOutPtr = NULL;
        g_AudioBusInPtr = NULL;
    }

    if (g_AudioBusMapping)
    {
        CloseHandle(g_AudioBusMapping);
        g_AudioBusMapping = NULL;
    }

    if (g_AudioBusMutex)
    {
        CloseHandle(g_AudioBusMutex);
        g_AudioBusMutex = NULL;
    }

    g_AudioBusInitialized = FALSE;
    g_ChannelDSPActive = FALSE;

    PrintMessageToDebugLog("AudioBus", "Shared memory destroyed.");
}

static BOOL AudioBus_IsConnected()
{
    return g_AudioBusInitialized && g_AudioBusPtr != NULL;
}

// ============================================================
// IMPLEMENTATION: LEVEL UPDATES
// ============================================================

static void AudioBus_UpdateLevels(float masterL, float masterR, DWORD totalVoices, float cpuUsage)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    // Smoothing
    if (masterL > g_MasterPeakL)
        g_MasterPeakL = masterL;
    else
        g_MasterPeakL *= AUDIOBUS_LEVEL_DECAY;

    if (masterR > g_MasterPeakR)
        g_MasterPeakR = masterR;
    else
        g_MasterPeakR *= AUDIOBUS_LEVEL_DECAY;

    // Quick non-blocking write
    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->MasterPeakL = g_MasterPeakL;
        g_AudioBusPtr->MasterPeakR = g_MasterPeakR;
        g_AudioBusPtr->TotalVoices = totalVoices;
        g_AudioBusPtr->CPUUsage = cpuUsage;
        g_AudioBusPtr->Timestamp = GetTickCount64();
        ReleaseMutex(g_AudioBusMutex);
    }
}

static void AudioBus_UpdateChannelVoices(int channel, DWORD voiceCount)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;
    if (channel < 0 || channel >= AUDIOBUS_NUM_CHANNELS)
        return;

    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->Channels[channel].VoiceCount = voiceCount;
        ReleaseMutex(g_AudioBusMutex);
    }
}

static void AudioBus_UpdateChannelLevels(int channel, float peakL, float peakR)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;
    if (channel < 0 || channel >= AUDIOBUS_NUM_CHANNELS)
        return;

    // Smoothing
    if (peakL > g_ChannelPeaksL[channel])
        g_ChannelPeaksL[channel] = peakL;
    else
        g_ChannelPeaksL[channel] *= AUDIOBUS_LEVEL_DECAY;

    if (peakR > g_ChannelPeaksR[channel])
        g_ChannelPeaksR[channel] = peakR;
    else
        g_ChannelPeaksR[channel] *= AUDIOBUS_LEVEL_DECAY;

    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->Channels[channel].PeakLevelL = g_ChannelPeaksL[channel];
        g_AudioBusPtr->Channels[channel].PeakLevelR = g_ChannelPeaksR[channel];
        ReleaseMutex(g_AudioBusMutex);
    }
}

static void AudioBus_UpdateAllChannelVoices(DWORD *voiceCounts)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        DWORD total = 0;
        for (int i = 0; i < AUDIOBUS_NUM_CHANNELS; i++)
        {
            g_AudioBusPtr->Channels[i].VoiceCount = voiceCounts[i];
            total += voiceCounts[i];
        }
        g_AudioBusPtr->TotalVoices = total;
        ReleaseMutex(g_AudioBusMutex);
    }
}

// ============================================================
// IMPLEMENTATION: PANIC HANDLING
// ============================================================

static BOOL AudioBus_CheckPanicRequest()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return FALSE;

    BOOL panicRequested = FALSE;
    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        panicRequested = (g_AudioBusPtr->Flags & AUDIOBUS_FLAG_PANIC_REQUEST) != 0;
        ReleaseMutex(g_AudioBusMutex);
    }
    return panicRequested;
}

static void AudioBus_AcknowledgePanic()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    if (WaitForSingleObject(g_AudioBusMutex, INFINITE) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->Flags &= ~AUDIOBUS_FLAG_PANIC_REQUEST;
        g_AudioBusPtr->Flags |= AUDIOBUS_FLAG_PANIC_ACK;
        ReleaseMutex(g_AudioBusMutex);
    }
    PrintMessageToDebugLog("AudioBus", "Panic acknowledged.");
}

static void AudioBus_RequestPanic()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    if (WaitForSingleObject(g_AudioBusMutex, INFINITE) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->Flags |= AUDIOBUS_FLAG_PANIC_REQUEST;
        g_AudioBusPtr->Flags &= ~AUDIOBUS_FLAG_PANIC_ACK;
        ReleaseMutex(g_AudioBusMutex);
    }
}

static void AudioBus_ClearPanicAck()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->Flags &= ~AUDIOBUS_FLAG_PANIC_ACK;
        ReleaseMutex(g_AudioBusMutex);
    }
}

// ============================================================
// IMPLEMENTATION: CONFIG
// ============================================================

static void AudioBus_SetSampleRate(DWORD sampleRate)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    if (WaitForSingleObject(g_AudioBusMutex, INFINITE) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->SampleRate = sampleRate;
        ReleaseMutex(g_AudioBusMutex);
    }

    char buf[64];
    sprintf(buf, "Sample rate updated to %lu Hz", sampleRate);
    PrintMessageToDebugLog("AudioBus", buf);
}

static DWORD AudioBus_GetFlags()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return 0;

    DWORD flags = 0;
    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        flags = g_AudioBusPtr->Flags;
        ReleaseMutex(g_AudioBusMutex);
    }
    return flags;
}

// ============================================================
// IMPLEMENTATION: TIMESTAMPS
// ============================================================

static void AudioBus_RecordMidiEvent()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;
    // No mutex - atomic write
    g_AudioBusPtr->LastMidiEventTime = AudioBus_GetQPCTicks();
}

static void AudioBus_RecordSynthComplete()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;
    g_AudioBusPtr->LastSynthCompleteTime = AudioBus_GetQPCTicks();
}

static void AudioBus_RecordAudioOutput()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;
    g_AudioBusPtr->LastAudioOutputTime = AudioBus_GetQPCTicks();
}

static void AudioBus_UpdateLatencyInfo(DWORD outputLatencyUs, DWORD asioInputLatencyUs, DWORD engine)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->OutputBufferLatencyUs = outputLatencyUs;
        g_AudioBusPtr->AsioInputLatencyUs = asioInputLatencyUs;
        g_AudioBusPtr->CurrentEngine = engine;
        ReleaseMutex(g_AudioBusMutex);
    }
}

// ============================================================
// IMPLEMENTATION: SPRINT 1 - AUDIO STREAMING
// ============================================================

// DSP callback - BASS calls this for each channel's audio (stereo floats)
void CALLBACK AudioBus_ChannelDSPCallback(HDSP handle, DWORD channel, void *buffer, DWORD length, void *user)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    // Only capture when takeover is active
    if (g_AudioBusPtr->TakeoverState != TAKEOVER_ACTIVE)
        return;

    int chIndex = (int)(intptr_t)user;
    if (chIndex < 0 || chIndex >= AUDIOBUS_NUM_CHANNELS)
        return;

    float *samples = (float *)buffer;
    DWORD sampleCount = length / sizeof(float); // Total floats (stereo pairs)

    // Also update peak levels while we're here
    float peakL = 0.0f, peakR = 0.0f;
    for (DWORD i = 0; i < sampleCount; i += 2)
    {
        float l = fabsf(samples[i]);
        float r = fabsf(samples[i + 1]);
        if (l > peakL)
            peakL = l;
        if (r > peakR)
            peakR = r;
    }
    AudioBus_UpdateChannelLevels(chIndex, peakL, peakR);

    // Copy to the current OUT buffer
    int writeIndex = g_AudioBusPtr->OutWriteIndex & 1; // 0 or 1
    float *destBuffer = AudioBus_GetOutChannelBuffer(chIndex, writeIndex);

    if (destBuffer)
    {
        // Copy the samples (might be less than full buffer)
        DWORD copyCount = min(sampleCount, AUDIOBUS_BUFFER_SAMPLES * AUDIOBUS_STEREO);
        memcpy(destBuffer, samples, copyCount * sizeof(float));
    }
}

// Hook DSPs on all 16 channels - call after MIDI stream is up
static BOOL AudioBus_SetupChannelDSPs(HSTREAM midiStream)
{
    if (!g_AudioBusInitialized || !midiStream)
        return FALSE;

    if (g_ChannelDSPActive)
    {
        PrintMessageToDebugLog("AudioBus", "Channel DSPs already active");
        return TRUE;
    }

    PrintMessageToDebugLog("AudioBus", "Setting up per-channel DSP callbacks...");

    int setupCount = 0;
    for (int ch = 0; ch < AUDIOBUS_NUM_CHANNELS; ch++)
    {
        // Get the channel stream handle from BASSMIDI
        HSTREAM chanStream = BASS_MIDI_StreamGetChannel(midiStream, ch);
        if (!chanStream)
        {
            // Channel might not be active yet, that's okay
            g_ChannelStreams[ch] = 0;
            g_ChannelDSPHandles[ch] = 0;
            continue;
        }

        g_ChannelStreams[ch] = chanStream;

        // Set up DSP callback with channel index as user data
        HDSP dsp = BASS_ChannelSetDSP(chanStream, AudioBus_ChannelDSPCallback, (void *)(intptr_t)ch, 0);
        if (dsp)
        {
            g_ChannelDSPHandles[ch] = dsp;
            setupCount++;
        }
        else
        {
            g_ChannelDSPHandles[ch] = 0;
            char buf[128];
            sprintf(buf, "Failed to set DSP on channel %d, error: %d", ch, BASS_ErrorGetCode());
            PrintMessageToDebugLog("AudioBus", buf);
        }
    }

    g_ChannelDSPActive = TRUE;

    char buf[128];
    sprintf(buf, "Set up %d channel DSPs", setupCount);
    PrintMessageToDebugLog("AudioBus", buf);

    return setupCount > 0;
}

// Unhook all channel DSPs
static void AudioBus_RemoveChannelDSPs()
{
    if (!g_ChannelDSPActive)
        return;

    PrintMessageToDebugLog("AudioBus", "Removing channel DSP callbacks...");

    for (int ch = 0; ch < AUDIOBUS_NUM_CHANNELS; ch++)
    {
        if (g_ChannelDSPHandles[ch] && g_ChannelStreams[ch])
        {
            BASS_ChannelRemoveDSP(g_ChannelStreams[ch], g_ChannelDSPHandles[ch]);
        }
        g_ChannelDSPHandles[ch] = 0;
        g_ChannelStreams[ch] = 0;
    }

    g_ChannelDSPActive = FALSE;
}

// Is Permafrost currently handling our audio?
static BOOL AudioBus_IsTakeoverActive()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return FALSE;

    return g_AudioBusPtr->TakeoverState == TAKEOVER_ACTIVE;
}

static AudioBusTakeoverState AudioBus_GetTakeoverState()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return TAKEOVER_DIRECT;

    return g_AudioBusPtr->TakeoverState;
}

// Permafrost wants to take over - go DIRECT -> PENDING
static void AudioBus_RequestTakeover()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    if (WaitForSingleObject(g_AudioBusMutex, AUDIOBUS_TAKEOVER_TIMEOUT_MS) == WAIT_OBJECT_0)
    {
        if (g_AudioBusPtr->TakeoverState == TAKEOVER_DIRECT)
        {
            g_AudioBusPtr->TakeoverState = TAKEOVER_PENDING;
            g_AudioBusPtr->Flags |= AUDIOBUS_FLAG_AUDIO_ENABLED;
            PrintMessageToDebugLog("AudioBus", "Takeover requested - state: PENDING");
        }
        ReleaseMutex(g_AudioBusMutex);
    }
}

// Give audio back to direct output
static void AudioBus_ReleaseTakeover()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    if (WaitForSingleObject(g_AudioBusMutex, AUDIOBUS_TAKEOVER_TIMEOUT_MS) == WAIT_OBJECT_0)
    {
        if (g_AudioBusPtr->TakeoverState == TAKEOVER_ACTIVE ||
            g_AudioBusPtr->TakeoverState == TAKEOVER_PENDING)
        {
            g_AudioBusPtr->TakeoverState = TAKEOVER_RELEASING;
            PrintMessageToDebugLog("AudioBus", "Takeover release requested - state: RELEASING");
        }
        ReleaseMutex(g_AudioBusMutex);
    }
}

// Frame boundary - handle state changes and swap buffers
static void AudioBus_ProcessFrameBoundary()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return;

    // Handle state transitions at frame boundary
    AudioBusTakeoverState state = g_AudioBusPtr->TakeoverState;

    if (state == TAKEOVER_PENDING)
    {
        // Permafrost is ready, switch to active
        g_AudioBusPtr->TakeoverState = TAKEOVER_ACTIVE;
        PrintMessageToDebugLog("AudioBus", "Takeover active - state: ACTIVE");
    }
    else if (state == TAKEOVER_RELEASING)
    {
        // Finish current frame, return to direct
        g_AudioBusPtr->TakeoverState = TAKEOVER_DIRECT;
        g_AudioBusPtr->Flags &= ~AUDIOBUS_FLAG_AUDIO_ENABLED;
        PrintMessageToDebugLog("AudioBus", "Takeover released - state: DIRECT");
    }
}

// Flip to next OUT buffer
static void AudioBus_SwapOutBuffer()
{
    if (!g_AudioBusPtr)
        return;

    // Atomic swap: 0->1 or 1->0
    InterlockedExchange(&g_AudioBusPtr->OutWriteIndex,
                        (g_AudioBusPtr->OutWriteIndex + 1) & 1);
    InterlockedIncrement64((volatile LONG64 *)&g_AudioBusPtr->OutFrameCounter);

    g_AudioBusPtr->LastSharedMemWriteTime = AudioBus_GetQPCTicks();
}

// Tell Permafrost we've got a frame ready
static void AudioBus_SignalAudioReady()
{
    if (g_AudioReadyEvent)
    {
        SetEvent(g_AudioReadyEvent);
    }
}

// Wait for Permafrost to finish processing (FALSE = timeout)
static BOOL AudioBus_WaitForProcessedAudio(DWORD timeoutMs)
{
    if (!g_ProcessedReadyEvent)
        return FALSE;

    DWORD result = WaitForSingleObject(g_ProcessedReadyEvent, timeoutMs);
    return result == WAIT_OBJECT_0;
}

// Grab processed audio from Permafrost
// They write to InWriteIndex then swap, so we read the opposite buffer
static BOOL AudioBus_ReadProcessedAudio(float *buffer, DWORD sampleCount)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr || !g_AudioBusInPtr || !buffer)
        return FALSE;

    // Read from the buffer Permafrost just finished writing
    // After Permafrost swaps InWriteIndex, the completed buffer is the opposite
    int writeIndex = g_AudioBusPtr->InWriteIndex;
    int readFromBuffer = (writeIndex + 1) & 1;
    float *srcBuffer = AudioBus_GetInBuffer(readFromBuffer);

    if (!srcBuffer)
        return FALSE;

    // Copy the samples
    DWORD copyCount = min(sampleCount * AUDIOBUS_STEREO, AUDIOBUS_BUFFER_SAMPLES * AUDIOBUS_STEREO);
    memcpy(buffer, srcBuffer, copyCount * sizeof(float));

    g_AudioBusPtr->LastSharedMemReadTime = AudioBus_GetQPCTicks();

    return TRUE;
}

// Bump heartbeat (call each frame)
static void AudioBus_IncrementHeartbeat()
{
    if (!g_AudioBusPtr)
        return;
    InterlockedIncrement64((volatile LONG64 *)&g_AudioBusPtr->HeartbeatCounter);
}

// Did Permafrost crash? Check if frame counters are drifting
static BOOL AudioBus_CheckPermafrostAlive()
{
    if (!g_AudioBusPtr || g_AudioBusPtr->TakeoverState != TAKEOVER_ACTIVE)
        return TRUE; // Not in takeover mode, don't check

    // Compare OUT and IN frame counters
    // If we've written many more frames than we've received back,
    // Permafrost is probably not processing
    ULONGLONG outFrames = g_AudioBusPtr->OutFrameCounter;
    ULONGLONG inFrames = g_AudioBusPtr->InFrameCounter;

    // Allow up to 3 frames of drift (for buffering)
    if (outFrames > inFrames + 3)
    {
        PrintMessageToDebugLog("AudioBus", "Permafrost appears unresponsive, releasing takeover");
        AudioBus_ReleaseTakeover();
        return FALSE;
    }

    return TRUE;
}

// Main roundtrip handler - returns TRUE if audio went through Permafrost, FALSE for direct out
static BOOL AudioBus_ProcessAudioFrame()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
        return FALSE;

    // Handle frame boundary state transitions
    AudioBus_ProcessFrameBoundary();

    // Increment heartbeat
    AudioBus_IncrementHeartbeat();

    // Check if takeover is active
    if (!AudioBus_IsTakeoverActive())
        return FALSE;

    // Check if Permafrost is still alive
    if (!AudioBus_CheckPermafrostAlive())
        return FALSE;

    // At this point, DSP callbacks have filled the channel buffers
    // Swap to next buffer and signal Permafrost
    AudioBus_SwapOutBuffer();
    AudioBus_SignalAudioReady();

    // Wait for processed audio (with timeout for crash safety)
    if (!AudioBus_WaitForProcessedAudio(AUDIOBUS_FRAME_TIMEOUT_MS))
    {
        // Timeout! Permafrost is too slow or crashed
        PrintMessageToDebugLog("AudioBus", "Timeout waiting for processed audio");
        AudioBus_ReleaseTakeover();
        return FALSE;
    }

    return TRUE;
}

// Frozy was here.