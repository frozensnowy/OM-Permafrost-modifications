/*
OmniMIDI Audio Bus
Shared memory interface for Permafrost integration

This creates a shared memory region that Permafrost can read to get:
- Real-time audio levels (per-channel and master)
- Voice counts per channel
- Panic/reset flags (proper)
- Future: raw audio data for VST processing

The design's meant to be minimal on OmniMIDI's side, I just write
data to shared memory, and Permafrost reads it whenever it wants.
No complex synchronization needed for the metadata stuff.
*/
#pragma once

#include <windows.h>
#include <stdio.h>
#include <math.h>

// Shared memory names - Permafrost will connect to these
#define AUDIOBUS_SHARED_MEM_NAME L"OmniMIDI_AudioBus"
#define AUDIOBUS_MUTEX_NAME L"OmniMIDI_AudioBusMutex"
#define AUDIOBUS_VERSION 2 // Bumped for timestamp additions

// Size constants
#define AUDIOBUS_NUM_CHANNELS 16
#define AUDIOBUS_BUFFER_SAMPLES 256

// Flags for the shared memory header
#define AUDIOBUS_FLAG_ACTIVE 0x0001        // OmniMIDI is running and streaming
#define AUDIOBUS_FLAG_PANIC_REQUEST 0x0002 // Permafrost wants us to panic (all notes off)
#define AUDIOBUS_FLAG_PANIC_ACK 0x0004     // We acknowledged the panic request
#define AUDIOBUS_FLAG_AUDIO_MODE 0x0008    // Future: audio streaming is enabled

// Peak level decay rate (makes the meters smooth)
// Lower = faster decay, higher = slower decay
#define AUDIOBUS_LEVEL_DECAY 0.92f

#pragma pack(push, 1)

// Per-channel metadata
typedef struct AudioBusChannelInfo
{
    float PeakLevelL;  // Left channel peak level (0.0 - 1.0)
    float PeakLevelR;  // Right channel peak level (0.0 - 1.0)
    DWORD VoiceCount;  // Active voices on this channel
    DWORD Reserved[2]; // Future use (maybe RMS levels, etc)
} AudioBusChannelInfo;

// Main shared memory header
// This is what Permafrost reads to get real-time data
typedef struct AudioBusHeader
{
    // Magic bytes to verify we're reading the right thing
    char Magic[4]; // "OMAB" = OmniMIDI Audio Bus
    DWORD Version; // Protocol version (bump this if structure changes)

    // Audio format info
    DWORD SampleRate;  // Current sample rate (e.g. 48000)
    DWORD BufferSize;  // Samples per buffer (e.g. 256)
    DWORD NumChannels; // Always 16 for MIDI

    // Sync and status
    DWORD Flags;            // AUDIOBUS_FLAG_* values
    ULONGLONG WriteCounter; // Incremented every time we update levels
    ULONGLONG Timestamp;    // GetTickCount64 when last updated

    // Master levels (post-mix, what you actually hear)
    float MasterPeakL; // Master left peak (0.0 - 1.0)
    float MasterPeakR; // Master right peak (0.0 - 1.0)
    DWORD TotalVoices; // Total active voices across all channels
    float CPUUsage;    // Rendering CPU percentage

    // ============================================================
    // LATENCY TIMESTAMPS (QPC = QueryPerformanceCounter, microseconds)
    // These let Permafrost measure exact latency at each stage
    // ============================================================

    // QPC frequency (ticks per second) - needed to convert timestamps to time
    ULONGLONG QPCFrequency;

    // Timestamp when last MIDI event was received (QPC ticks)
    ULONGLONG LastMidiEventTime;

    // Timestamp when synthesis buffer was filled (QPC ticks)
    ULONGLONG LastSynthCompleteTime;

    // Timestamp when audio was written to output device (QPC ticks)
    ULONGLONG LastAudioOutputTime;

    // Pre-calculated latency values in microseconds (for convenience)
    DWORD OutputBufferLatencyUs; // Output buffer latency in microseconds
    DWORD AsioInputLatencyUs;    // ASIO input latency (0 for non-ASIO)

    // Current audio engine type (0=WAV, 1=DS, 2=ASIO, 3=WASAPI, 4=XAudio)
    DWORD CurrentEngine;

    // Reserved for future (SharedMem roundtrip timestamps, VST latency, etc)
    BYTE Reserved[16];

    // Per-channel info (16 channels)
    AudioBusChannelInfo Channels[AUDIOBUS_NUM_CHANNELS];

} AudioBusHeader;

#pragma pack(pop)

// Calculated shared memory size
#define AUDIOBUS_HEADER_SIZE sizeof(AudioBusHeader)
#define AUDIOBUS_SHARED_MEM_SIZE AUDIOBUS_HEADER_SIZE

// Global state for the audio bus
static HANDLE g_AudioBusMapping = NULL;
static AudioBusHeader *g_AudioBusPtr = NULL;
static HANDLE g_AudioBusMutex = NULL;
static BOOL g_AudioBusInitialized = FALSE;

// Temporary storage for peak level smoothing
static float g_ChannelPeaksL[AUDIOBUS_NUM_CHANNELS] = {0};
static float g_ChannelPeaksR[AUDIOBUS_NUM_CHANNELS] = {0};
static float g_MasterPeakL = 0.0f;
static float g_MasterPeakR = 0.0f;

// QPC frequency cache (set once at init)
static ULONGLONG g_QPCFrequency = 0;

// Forward declarations
static BOOL AudioBus_Create();
static void AudioBus_Destroy();
static void AudioBus_UpdateLevels(float masterL, float masterR, DWORD totalVoices, float cpuUsage);
static void AudioBus_UpdateChannelVoices(int channel, DWORD voiceCount);
static void AudioBus_UpdateChannelLevels(int channel, float peakL, float peakR);
static BOOL AudioBus_CheckPanicRequest();
static void AudioBus_AcknowledgePanic();
static BOOL AudioBus_IsConnected();

// Timestamp functions for latency measurement
static void AudioBus_RecordMidiEvent();
static void AudioBus_RecordSynthComplete();
static void AudioBus_RecordAudioOutput();
static void AudioBus_UpdateLatencyInfo(DWORD outputLatencyUs, DWORD asioInputLatencyUs, DWORD engine);

/*
Creates the shared memory region.
Called during OmniMIDI initialization.
Returns TRUE if successful, FALSE otherwise.
*/
static BOOL AudioBus_Create()
{
    if (g_AudioBusInitialized)
    {
        PrintMessageToDebugLog("AudioBus", "Already initialized, skipping.");
        return TRUE;
    }

    PrintMessageToDebugLog("AudioBus", "Creating shared memory for Permafrost integration...");

    // Create or open the mutex for synchronization
    g_AudioBusMutex = CreateMutexW(NULL, FALSE, AUDIOBUS_MUTEX_NAME);
    if (g_AudioBusMutex == NULL)
    {
        DWORD err = GetLastError();
        char buf[128];
        sprintf(buf, "Failed to create mutex, error: %lu", err);
        PrintMessageToDebugLog("AudioBus", buf);
        return FALSE;
    }

    // Create the shared memory mapping
    g_AudioBusMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,     // Use page file
        NULL,                     // Default security
        PAGE_READWRITE,           // Read/write access
        0,                        // High-order DWORD of size
        AUDIOBUS_SHARED_MEM_SIZE, // Low-order DWORD of size
        AUDIOBUS_SHARED_MEM_NAME  // Name of the mapping
    );

    if (g_AudioBusMapping == NULL)
    {
        DWORD err = GetLastError();
        char buf[128];
        sprintf(buf, "Failed to create file mapping, error: %lu", err);
        PrintMessageToDebugLog("AudioBus", buf);
        CloseHandle(g_AudioBusMutex);
        g_AudioBusMutex = NULL;
        return FALSE;
    }

    // Map the shared memory into our address space
    g_AudioBusPtr = (AudioBusHeader *)MapViewOfFile(
        g_AudioBusMapping,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        AUDIOBUS_SHARED_MEM_SIZE);

    if (g_AudioBusPtr == NULL)
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

    // Get QPC frequency for timestamp conversion
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_QPCFrequency = freq.QuadPart;

    // Initialize the header
    WaitForSingleObject(g_AudioBusMutex, INFINITE);

    memset(g_AudioBusPtr, 0, AUDIOBUS_SHARED_MEM_SIZE);

    // Set magic bytes
    g_AudioBusPtr->Magic[0] = 'O';
    g_AudioBusPtr->Magic[1] = 'M';
    g_AudioBusPtr->Magic[2] = 'A';
    g_AudioBusPtr->Magic[3] = 'B';

    g_AudioBusPtr->Version = AUDIOBUS_VERSION;
    g_AudioBusPtr->SampleRate = ManagedSettings.AudioFrequency;
    g_AudioBusPtr->BufferSize = AUDIOBUS_BUFFER_SAMPLES;
    g_AudioBusPtr->NumChannels = AUDIOBUS_NUM_CHANNELS;
    g_AudioBusPtr->Flags = AUDIOBUS_FLAG_ACTIVE;
    g_AudioBusPtr->WriteCounter = 0;
    g_AudioBusPtr->Timestamp = GetTickCount64();

    // Initialize timestamp fields
    g_AudioBusPtr->QPCFrequency = g_QPCFrequency;
    g_AudioBusPtr->LastMidiEventTime = 0;
    g_AudioBusPtr->LastSynthCompleteTime = 0;
    g_AudioBusPtr->LastAudioOutputTime = 0;
    g_AudioBusPtr->OutputBufferLatencyUs = 0;
    g_AudioBusPtr->AsioInputLatencyUs = 0;
    g_AudioBusPtr->CurrentEngine = ManagedSettings.CurrentEngine;

    // Initialize channel info
    for (int i = 0; i < AUDIOBUS_NUM_CHANNELS; i++)
    {
        g_AudioBusPtr->Channels[i].PeakLevelL = 0.0f;
        g_AudioBusPtr->Channels[i].PeakLevelR = 0.0f;
        g_AudioBusPtr->Channels[i].VoiceCount = 0;
    }

    ReleaseMutex(g_AudioBusMutex);

    g_AudioBusInitialized = TRUE;
    PrintMessageToDebugLog("AudioBus", "Shared memory created successfully. Permafrost can now connect.");

    return TRUE;
}

/*
Destroys the shared memory region.
Called during OmniMIDI shutdown.
*/
static void AudioBus_Destroy()
{
    if (!g_AudioBusInitialized)
    {
        return;
    }

    PrintMessageToDebugLog("AudioBus", "Destroying shared memory...");

    // Mark as inactive before cleanup
    if (g_AudioBusPtr && g_AudioBusMutex)
    {
        WaitForSingleObject(g_AudioBusMutex, 100);
        g_AudioBusPtr->Flags = 0; // Clear active flag
        ReleaseMutex(g_AudioBusMutex);
    }

    // Unmap and close handles
    if (g_AudioBusPtr)
    {
        UnmapViewOfFile(g_AudioBusPtr);
        g_AudioBusPtr = NULL;
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
    PrintMessageToDebugLog("AudioBus", "Shared memory destroyed.");
}

/*
Updates the master audio levels and overall stats.
This should be called from the audio thread periodically.

masterL/masterR: Peak levels from BASS_ChannelGetLevelEx (0.0 - 1.0)
totalVoices: Total active voices across all channels
cpuUsage: CPU percentage from BASS_ChannelGetAttribute(BASS_ATTRIB_CPU)
*/
static void AudioBus_UpdateLevels(float masterL, float masterR, DWORD totalVoices, float cpuUsage)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    // Apply smoothing to the peak levels (fast attack, slow decay)
    // This prevents the meters from being too jumpy
    if (masterL > g_MasterPeakL)
    {
        g_MasterPeakL = masterL; // Instant attack
    }
    else
    {
        g_MasterPeakL *= AUDIOBUS_LEVEL_DECAY; // Slow decay
    }

    if (masterR > g_MasterPeakR)
    {
        g_MasterPeakR = masterR;
    }
    else
    {
        g_MasterPeakR *= AUDIOBUS_LEVEL_DECAY;
    }

    // Write to shared memory (quick operation, minimal lock time)
    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->MasterPeakL = g_MasterPeakL;
        g_AudioBusPtr->MasterPeakR = g_MasterPeakR;
        g_AudioBusPtr->TotalVoices = totalVoices;
        g_AudioBusPtr->CPUUsage = cpuUsage;
        g_AudioBusPtr->WriteCounter++;
        g_AudioBusPtr->Timestamp = GetTickCount64();

        ReleaseMutex(g_AudioBusMutex);
    }
    // If we can't get the mutex immediately, skip this update
    // (don't block the audio thread)
}

/*
Updates the voice count for a specific channel.
Called when BASSMIDI reports voice count changes.
*/
static void AudioBus_UpdateChannelVoices(int channel, DWORD voiceCount)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    if (channel < 0 || channel >= AUDIOBUS_NUM_CHANNELS)
    {
        return;
    }

    // Direct write - voice counts don't need smoothing
    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->Channels[channel].VoiceCount = voiceCount;
        ReleaseMutex(g_AudioBusMutex);
    }
}

/*
Updates the peak levels for a specific channel.
This would be called from DSP callbacks if we implement per-channel audio routing.
For now, we can estimate from voice activity.
*/
static void AudioBus_UpdateChannelLevels(int channel, float peakL, float peakR)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    if (channel < 0 || channel >= AUDIOBUS_NUM_CHANNELS)
    {
        return;
    }

    // Apply smoothing
    if (peakL > g_ChannelPeaksL[channel])
    {
        g_ChannelPeaksL[channel] = peakL;
    }
    else
    {
        g_ChannelPeaksL[channel] *= AUDIOBUS_LEVEL_DECAY;
    }

    if (peakR > g_ChannelPeaksR[channel])
    {
        g_ChannelPeaksR[channel] = peakR;
    }
    else
    {
        g_ChannelPeaksR[channel] *= AUDIOBUS_LEVEL_DECAY;
    }

    // Write to shared memory
    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->Channels[channel].PeakLevelL = g_ChannelPeaksL[channel];
        g_AudioBusPtr->Channels[channel].PeakLevelR = g_ChannelPeaksR[channel];
        ReleaseMutex(g_AudioBusMutex);
    }
}

/*
Updates all channel voice counts at once.
Called from ParseDebugData.
*/
static void AudioBus_UpdateAllChannelVoices(DWORD *voiceCounts)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

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

/*
Checks if Permafrost has set the panic request flag.
Returns TRUE if we should send all-notes-off.
*/
static BOOL AudioBus_CheckPanicRequest()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return FALSE;
    }

    BOOL panicRequested = FALSE;

    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        panicRequested = (g_AudioBusPtr->Flags & AUDIOBUS_FLAG_PANIC_REQUEST) != 0;
        ReleaseMutex(g_AudioBusMutex);
    }

    return panicRequested;
}

/*
Acknowledges the panic request after we've handled it.
Clears the request flag and sets the ack flag.
*/
static void AudioBus_AcknowledgePanic()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    if (WaitForSingleObject(g_AudioBusMutex, INFINITE) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->Flags &= ~AUDIOBUS_FLAG_PANIC_REQUEST;
        g_AudioBusPtr->Flags |= AUDIOBUS_FLAG_PANIC_ACK;
        ReleaseMutex(g_AudioBusMutex);
    }

    PrintMessageToDebugLog("AudioBus", "Panic acknowledged.");
}

/*
Clears the panic ack flag.
Permafrost calls this after it sees we handled the panic.
*/
static void AudioBus_ClearPanicAck()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->Flags &= ~AUDIOBUS_FLAG_PANIC_ACK;
        ReleaseMutex(g_AudioBusMutex);
    }
}

/*
Checks if the audio bus is initialized and connected.
*/
static BOOL AudioBus_IsConnected()
{
    return g_AudioBusInitialized && g_AudioBusPtr != NULL;
}

/*
Gets the current flags from shared memory.
Useful for checking status without modifying anything.
*/
static DWORD AudioBus_GetFlags()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return 0;
    }

    DWORD flags = 0;
    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        flags = g_AudioBusPtr->Flags;
        ReleaseMutex(g_AudioBusMutex);
    }
    return flags;
}

/*
Sets the panic request flag.
This is called by Permafrost (via the shared memory) to request a panic.
OmniMIDI will check this flag periodically and send all-notes-off.

NOTE: This function is here for completeness, but normally Permafrost
would write directly to the shared memory from its side.
*/
static void AudioBus_RequestPanic()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    if (WaitForSingleObject(g_AudioBusMutex, INFINITE) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->Flags |= AUDIOBUS_FLAG_PANIC_REQUEST;
        g_AudioBusPtr->Flags &= ~AUDIOBUS_FLAG_PANIC_ACK;
        ReleaseMutex(g_AudioBusMutex);
    }

    PrintMessageToDebugLog("AudioBus", "Panic requested.");
}

/*
Updates the sample rate in the shared memory header.
Call this when the audio engine is reconfigured.
*/
static void AudioBus_SetSampleRate(DWORD sampleRate)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    if (WaitForSingleObject(g_AudioBusMutex, INFINITE) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->SampleRate = sampleRate;
        ReleaseMutex(g_AudioBusMutex);
    }

    char buf[64];
    sprintf(buf, "Sample rate updated to %lu Hz", sampleRate);
    PrintMessageToDebugLog("AudioBus", buf);
}

// ============================================================
// TIMESTAMP FUNCTIONS FOR LATENCY MEASUREMENT
// ============================================================

/*
Gets current QPC timestamp in ticks.
Helper function for internal use.
*/
static inline ULONGLONG AudioBus_GetQPCTicks()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

/*
Records timestamp when a MIDI event is received.
Call this from the MIDI input handler.
*/
static void AudioBus_RecordMidiEvent()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    // No mutex - single write, atomic enough for our purposes
    // We don't want to slow down MIDI event processing
    g_AudioBusPtr->LastMidiEventTime = AudioBus_GetQPCTicks();
}

/*
Records timestamp when synthesis buffer is complete.
Call this after BASSMIDI fills a buffer.
*/
static void AudioBus_RecordSynthComplete()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    g_AudioBusPtr->LastSynthCompleteTime = AudioBus_GetQPCTicks();
}

/*
Records timestamp when audio is sent to output device.
Call this from the audio output callback/thread.
*/
static void AudioBus_RecordAudioOutput()
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    g_AudioBusPtr->LastAudioOutputTime = AudioBus_GetQPCTicks();
}

/*
Updates latency info from OmniMIDI's internal tracking.
Call this periodically (e.g., once per second with the rate limiter).

outputLatencyUs: Output buffer latency in microseconds
asioInputLatencyUs: ASIO input latency in microseconds (0 for non-ASIO)
engine: Current engine type (0=WAV, 1=DS, 2=ASIO, 3=WASAPI, 4=XAudio)
*/
static void AudioBus_UpdateLatencyInfo(DWORD outputLatencyUs, DWORD asioInputLatencyUs, DWORD engine)
{
    if (!g_AudioBusInitialized || !g_AudioBusPtr)
    {
        return;
    }

    if (WaitForSingleObject(g_AudioBusMutex, 0) == WAIT_OBJECT_0)
    {
        g_AudioBusPtr->OutputBufferLatencyUs = outputLatencyUs;
        g_AudioBusPtr->AsioInputLatencyUs = asioInputLatencyUs;
        g_AudioBusPtr->CurrentEngine = engine;
        ReleaseMutex(g_AudioBusMutex);
    }
}

/*
Calculates time difference between two QPC timestamps in microseconds.
Useful for Permafrost to calculate latencies.
*/
static inline ULONGLONG AudioBus_QPCDiffToMicroseconds(ULONGLONG start, ULONGLONG end)
{
    if (g_QPCFrequency == 0)
        return 0;
    if (end < start)
        return 0; // Handle wrap-around or invalid

    // Convert to microseconds: (diff * 1000000) / frequency
    return ((end - start) * 1000000ULL) / g_QPCFrequency;
}
