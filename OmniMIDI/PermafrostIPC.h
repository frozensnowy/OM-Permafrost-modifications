/*
OmniMIDI Permafrost IPC
Named pipe client for communication with Permafrost service

This handles two-way communication with Permafrost:
1. Requesting SoundFont lists (original functionality)
2. Mixer control commands (panic, levels, etc.)
3. Audio bus coordination (future)

The protocol is simple text-based for easy debugging:
- Commands are pipe-delimited: "COMMAND|param1|param2"
- Responses are either data or "ERROR|message"
*/
#pragma once

#define PERMAFROST_PIPE_NAME L"\\\\.\\pipe\\OmniMIDI_Permafrost"
#define PERMAFROST_TIMEOUT_MS 1000
#define PERMAFROST_BUFFER_SIZE 65536

// Command prefixes for mixer operations
// These are sent FROM Permafrost TO OmniMIDI via the watchdog check
#define PERMAFROST_CMD_PANIC "PANIC"
#define PERMAFROST_CMD_RESET "RESET"
#define PERMAFROST_CMD_GET_LEVELS "GET_LEVELS"

// Forward declaration - ResetSynth is defined in settings.h
// We need this here because PermafrostIPC.h might be included before settings.h
extern void ResetSynth(BOOL SwitchingBufferMode, BOOL ModeReset);

static BOOL PermafrostAvailable = FALSE;
static BOOL PermafrostMixerEnabled = FALSE;

// Request SoundFont list from Permafrost service
// Returns TRUE if successful and outListData contains .omlist format data
// Returns FALSE if Permafrost is not running or request failed (fall back to file-based)
static BOOL RequestSoundFontListFromPermafrost(
    const wchar_t *appName,
    const wchar_t *appPath,
    DWORD pid,
    std::wstring &outListData)
{
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    BOOL success = FALSE;

    try
    {
        PrintMessageToDebugLog("PermafrostIPC", "Attempting to connect to Permafrost service...");

        // Try to connect to the named pipe
        hPipe = CreateFileW(
            PERMAFROST_PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);

        if (hPipe == INVALID_HANDLE_VALUE)
        {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND)
            {
                PrintMessageToDebugLog("PermafrostIPC", "Permafrost service not running (pipe not found).");
            }
            else
            {
                PrintMessageToDebugLog("PermafrostIPC", "Failed to connect to Permafrost pipe.");
            }
            PermafrostAvailable = FALSE;
            return FALSE;
        }

        PrintMessageToDebugLog("PermafrostIPC", "Connected to Permafrost service.");
        PermafrostAvailable = TRUE;

        // Set pipe mode to message mode
        DWORD mode = PIPE_READMODE_BYTE;
        SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);

        // Build request: "PERMAFROST|appName|appPath|pid"
        std::wstring request = L"PERMAFROST|";
        request += appName ? appName : L"";
        request += L"|";
        request += appPath ? appPath : L"";
        request += L"|";
        request += std::to_wstring(pid);

        PrintMessageWToDebugLog(L"PermafrostIPC", request.c_str());

        // Convert to UTF-8 for sending
        int utf8Size = WideCharToMultiByte(CP_UTF8, 0, request.c_str(), -1, NULL, 0, NULL, NULL);
        std::string utf8Request(utf8Size, 0);
        WideCharToMultiByte(CP_UTF8, 0, request.c_str(), -1, &utf8Request[0], utf8Size, NULL, NULL);

        // Write request
        DWORD bytesWritten = 0;
        if (!WriteFile(hPipe, utf8Request.c_str(), (DWORD)utf8Request.length(), &bytesWritten, NULL))
        {
            PrintMessageToDebugLog("PermafrostIPC", "Failed to write request to pipe.");
            CloseHandle(hPipe);
            return FALSE;
        }

        FlushFileBuffers(hPipe);

        // Read response with timeout
        char buffer[PERMAFROST_BUFFER_SIZE] = {0};
        DWORD bytesRead = 0;
        DWORD totalBytesRead = 0;
        std::string response;

        // Set a deadline for reading
        DWORD startTime = GetTickCount();

        while (true)
        {
            DWORD bytesAvailable = 0;
            if (!PeekNamedPipe(hPipe, NULL, 0, NULL, &bytesAvailable, NULL))
            {
                break;
            }

            if (bytesAvailable > 0)
            {
                if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
                {
                    buffer[bytesRead] = '\0';
                    response += buffer;
                    totalBytesRead += bytesRead;
                }
            }

            // Check for timeout
            if (GetTickCount() - startTime > PERMAFROST_TIMEOUT_MS)
            {
                PrintMessageToDebugLog("PermafrostIPC", "Timeout waiting for response.");
                break;
            }

            // If we got data and pipe is empty, we're done
            if (totalBytesRead > 0 && bytesAvailable == 0)
            {
                // Small delay to ensure all data is received
                Sleep(10);
                PeekNamedPipe(hPipe, NULL, 0, NULL, &bytesAvailable, NULL);
                if (bytesAvailable == 0)
                    break;
            }

            Sleep(10);
        }

        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;

        if (response.empty())
        {
            PrintMessageToDebugLog("PermafrostIPC", "Empty response from Permafrost.");
            return FALSE;
        }

        // Check for error response
        if (response.substr(0, 6) == "ERROR|")
        {
            PrintMessageToDebugLog("PermafrostIPC", "Error response from Permafrost.");
            PrintMessageToDebugLog("PermafrostIPC", response.c_str());
            return FALSE;
        }

        // Convert UTF-8 response to wide string
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, response.c_str(), -1, NULL, 0);
        if (wideSize > 0)
        {
            outListData.resize(wideSize);
            MultiByteToWideChar(CP_UTF8, 0, response.c_str(), -1, &outListData[0], wideSize);
            // Remove null terminator from string
            if (!outListData.empty() && outListData.back() == L'\0')
            {
                outListData.pop_back();
            }
        }

        // Debug: Log first 500 chars of raw response
        char debugBuf[512] = {0};
        strncpy(debugBuf, response.c_str(), 500);
        PrintMessageToDebugLog("PermafrostIPC", "Raw response (first 500 chars):");
        PrintMessageToDebugLog("PermafrostIPC", debugBuf);

        // Debug: Log response length and check for CR characters
        char lenBuf[128];
        sprintf(lenBuf, "Response length: %zu bytes, contains CR: %s",
                response.length(),
                (response.find('\r') != std::string::npos) ? "YES" : "NO");
        PrintMessageToDebugLog("PermafrostIPC", lenBuf);

        PrintMessageToDebugLog("PermafrostIPC", "Successfully received SoundFont list from Permafrost.");
        success = TRUE;
    }
    catch (...)
    {
        PrintMessageToDebugLog("PermafrostIPC", "Exception in RequestSoundFontListFromPermafrost.");
        if (hPipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hPipe);
        }
        return FALSE;
    }

    return success;
}

// Quick check if Permafrost service is available
static BOOL IsPermafrostAvailable()
{
    if (PermafrostAvailable)
        return TRUE;

    HANDLE hPipe = CreateFileW(
        PERMAFROST_PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hPipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hPipe);
        PermafrostAvailable = TRUE;
        return TRUE;
    }

    return FALSE;
}

// Send a command to Permafrost and get response
// This is a simpler version for quick commands like PANIC
static BOOL SendCommandToPermafrost(const char *command, std::string &outResponse)
{
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    BOOL success = FALSE;

    try
    {
        hPipe = CreateFileW(
            PERMAFROST_PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);

        if (hPipe == INVALID_HANDLE_VALUE)
        {
            return FALSE;
        }

        // Set pipe mode
        DWORD mode = PIPE_READMODE_BYTE;
        SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);

        // Write command
        DWORD bytesWritten = 0;
        if (!WriteFile(hPipe, command, (DWORD)strlen(command), &bytesWritten, NULL))
        {
            CloseHandle(hPipe);
            return FALSE;
        }

        FlushFileBuffers(hPipe);

        // Read response with short timeout
        char buffer[4096] = {0};
        DWORD bytesRead = 0;
        DWORD startTime = GetTickCount();

        while ((GetTickCount() - startTime) < 500) // 500ms timeout for commands
        {
            DWORD bytesAvailable = 0;
            if (!PeekNamedPipe(hPipe, NULL, 0, NULL, &bytesAvailable, NULL))
                break;

            if (bytesAvailable > 0)
            {
                if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
                {
                    buffer[bytesRead] = '\0';
                    outResponse = buffer;
                    success = TRUE;
                    break;
                }
            }
            Sleep(10);
        }

        CloseHandle(hPipe);
    }
    catch (...)
    {
        if (hPipe != INVALID_HANDLE_VALUE)
            CloseHandle(hPipe);
        return FALSE;
    }

    return success;
}

// Check for pending mixer commands from Permafrost
// This is called from the watchdog/health thread to check for commands
// Returns the command type if one is pending, or empty string if none
static std::string CheckPermafrostMixerCommand()
{
    // For now, check via registry flag (simple and reliable)
    // Permafrost sets a flag, we read and clear it

    HKEY hKey = NULL;
    std::string command = "";

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\OmniMIDI\\Mixer", 0,
                      KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        DWORD panicFlag = 0;
        DWORD dwType = REG_DWORD;
        DWORD dwSize = sizeof(DWORD);

        // Check panic flag
        if (RegQueryValueExW(hKey, L"PanicRequest", NULL, &dwType,
                             (LPBYTE)&panicFlag, &dwSize) == ERROR_SUCCESS)
        {
            if (panicFlag != 0)
            {
                command = PERMAFROST_CMD_PANIC;

                // Clear the flag
                DWORD zero = 0;
                RegSetValueExW(hKey, L"PanicRequest", 0, REG_DWORD,
                               (LPBYTE)&zero, sizeof(DWORD));
            }
        }

        RegCloseKey(hKey);
    }

    return command;
}

// Execute a mixer command received from Permafrost
// This is the dispatcher that calls the appropriate handler
static void ExecutePermafrostMixerCommand(const std::string &command)
{
    if (command.empty())
        return;

    if (command == PERMAFROST_CMD_PANIC)
    {
        PrintMessageToDebugLog("PermafrostIPC", "Executing PANIC command from Permafrost.");

        // Call ResetSynth to send all-notes-off and reset controllers
        // The FALSE, FALSE params mean: don't clear EVBuffer, don't send SysEx reset
        ResetSynth(FALSE, FALSE);

// Also acknowledge via AudioBus if it's connected
#ifdef AUDIOBUS_VERSION
        if (AudioBus_IsConnected())
        {
            AudioBus_AcknowledgePanic();
        }
#endif

        PrintMessageToDebugLog("PermafrostIPC", "PANIC command executed.");
    }
    else if (command == PERMAFROST_CMD_RESET)
    {
        PrintMessageToDebugLog("PermafrostIPC", "Executing RESET command from Permafrost.");

        // Full reset with SysEx
        ResetSynth(FALSE, TRUE);

        PrintMessageToDebugLog("PermafrostIPC", "RESET command executed.");
    }
}

// Poll for mixer commands from Permafrost
// This should be called periodically from the health/watchdog thread
static void PollPermafrostMixerCommands()
{
    // Method 1: Check registry for commands
    std::string command = CheckPermafrostMixerCommand();
    if (!command.empty())
    {
        ExecutePermafrostMixerCommand(command);
    }

// Method 2: Check AudioBus shared memory for panic flag
#ifdef AUDIOBUS_VERSION
    if (AudioBus_IsConnected() && AudioBus_CheckPanicRequest())
    {
        PrintMessageToDebugLog("PermafrostIPC", "Panic request detected in AudioBus shared memory.");
        ResetSynth(FALSE, FALSE);
        AudioBus_AcknowledgePanic();
    }
#endif
}

// Initialize Permafrost mixer integration
// Creates the registry key and any other resources needed
static void InitializePermafrostMixer()
{
    // Create the mixer registry key if it doesn't exist
    HKEY hKey = NULL;
    DWORD disposition = 0;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\OmniMIDI\\Mixer", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, &disposition) == ERROR_SUCCESS)
    {
        // Initialize default values
        DWORD zero = 0;
        RegSetValueExW(hKey, L"PanicRequest", 0, REG_DWORD, (LPBYTE)&zero, sizeof(DWORD));

        RegCloseKey(hKey);

        if (disposition == REG_CREATED_NEW_KEY)
        {
            PrintMessageToDebugLog("PermafrostIPC", "Created Mixer registry key.");
        }
    }

    PermafrostMixerEnabled = TRUE;
    PrintMessageToDebugLog("PermafrostIPC", "Permafrost mixer integration initialized.");
}
