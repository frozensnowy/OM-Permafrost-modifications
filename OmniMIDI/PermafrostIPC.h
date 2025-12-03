/*
OmniMIDI Permafrost IPC
Named pipe client for communication with Permafrost service
*/
#pragma once

#define PERMAFROST_PIPE_NAME L"\\\\.\\pipe\\OmniMIDI_Permafrost"
#define PERMAFROST_TIMEOUT_MS 1000
#define PERMAFROST_BUFFER_SIZE 65536

static BOOL PermafrostAvailable = FALSE;

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
