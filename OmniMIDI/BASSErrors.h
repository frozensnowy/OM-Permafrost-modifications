/*
BASS Error Handling
Maps BASS error codes to human-readable messages and suggested fixes.
Fixes cringy code.
*/
#pragma once

// Stringify macro for error code names
#define BASS_ERROR_CASE(err) \
	case err:                \
		return L#err;

static LPCWSTR GetBASSErrorName(INT code)
{
	switch (code)
	{
		BASS_ERROR_CASE(BASS_OK)
		BASS_ERROR_CASE(BASS_ERROR_MEM)
		BASS_ERROR_CASE(BASS_ERROR_FILEOPEN)
		BASS_ERROR_CASE(BASS_ERROR_DRIVER)
		BASS_ERROR_CASE(BASS_ERROR_BUFLOST)
		BASS_ERROR_CASE(BASS_ERROR_HANDLE)
		BASS_ERROR_CASE(BASS_ERROR_FORMAT)
		BASS_ERROR_CASE(BASS_ERROR_POSITION)
		BASS_ERROR_CASE(BASS_ERROR_INIT)
		BASS_ERROR_CASE(BASS_ERROR_START)
		BASS_ERROR_CASE(BASS_ERROR_SSL)
		BASS_ERROR_CASE(BASS_ERROR_ALREADY)
		BASS_ERROR_CASE(BASS_ERROR_NOCHAN)
		BASS_ERROR_CASE(BASS_ERROR_ILLTYPE)
		BASS_ERROR_CASE(BASS_ERROR_ILLPARAM)
		BASS_ERROR_CASE(BASS_ERROR_NO3D)
		BASS_ERROR_CASE(BASS_ERROR_NOEAX)
		BASS_ERROR_CASE(BASS_ERROR_DEVICE)
		BASS_ERROR_CASE(BASS_ERROR_NOPLAY)
		BASS_ERROR_CASE(BASS_ERROR_FREQ)
		BASS_ERROR_CASE(BASS_ERROR_NOTFILE)
		BASS_ERROR_CASE(BASS_ERROR_NOHW)
		BASS_ERROR_CASE(BASS_ERROR_EMPTY)
		BASS_ERROR_CASE(BASS_ERROR_NONET)
		BASS_ERROR_CASE(BASS_ERROR_CREATE)
		BASS_ERROR_CASE(BASS_ERROR_NOFX)
		BASS_ERROR_CASE(BASS_ERROR_NOTAVAIL)
		BASS_ERROR_CASE(BASS_ERROR_DECODE)
		BASS_ERROR_CASE(BASS_ERROR_DX)
		BASS_ERROR_CASE(BASS_ERROR_TIMEOUT)
		BASS_ERROR_CASE(BASS_ERROR_FILEFORM)
		BASS_ERROR_CASE(BASS_ERROR_SPEAKER)
		BASS_ERROR_CASE(BASS_ERROR_VERSION)
		BASS_ERROR_CASE(BASS_ERROR_CODEC)
		BASS_ERROR_CASE(BASS_ERROR_ENDED)
		BASS_ERROR_CASE(BASS_ERROR_BUSY)
		BASS_ERROR_CASE(BASS_ERROR_WASAPI)
		BASS_ERROR_CASE(BASS_ERROR_WASAPI_BUFFER)
		BASS_ERROR_CASE(BASS_ERROR_WASAPI_RAW)
		BASS_ERROR_CASE(BASS_ERROR_WASAPI_DENIED)
		BASS_ERROR_CASE(BASS_ERROR_MIDI_INCLUDE)
		BASS_ERROR_CASE(BASS_ERROR_UNKNOWN)
	default:
		return L"BASS_ERROR_UNKNOWN";
	}
}

static LPCWSTR GetBASSErrorDescription(INT code)
{
	switch (code)
	{
	case BASS_OK:
		return L"No error.";
	case BASS_ERROR_MEM:
		return L"Out of memory.";
	case BASS_ERROR_FILEOPEN:
		return L"Can't open the file.";
	case BASS_ERROR_DRIVER:
		return L"No available driver. Device may be in use.";
	case BASS_ERROR_BUFLOST:
		return L"Sample buffer was lost.";
	case BASS_ERROR_HANDLE:
		return L"Invalid handle.";
	case BASS_ERROR_FORMAT:
		return L"Sample format not supported by device.";
	case BASS_ERROR_POSITION:
		return L"Invalid position (beyond end or not downloaded yet).";
	case BASS_ERROR_INIT:
		return L"BASS_Init hasn't been called.";
	case BASS_ERROR_START:
		return L"BASS_Start hasn't been called.";
	case BASS_ERROR_SSL:
		return L"SSL/HTTPS not available.";
	case BASS_ERROR_ALREADY:
		return L"Already initialized.";
	case BASS_ERROR_NOCHAN:
		return L"No free channels available.";
	case BASS_ERROR_ILLTYPE:
		return L"Illegal type specified.";
	case BASS_ERROR_ILLPARAM:
		return L"Illegal parameter.";
	case BASS_ERROR_NO3D:
		return L"No 3D support.";
	case BASS_ERROR_NOEAX:
		return L"No EAX support.";
	case BASS_ERROR_DEVICE:
		return L"Invalid device.";
	case BASS_ERROR_NOPLAY:
		return L"Not playing.";
	case BASS_ERROR_FREQ:
		return L"Illegal sample rate.";
	case BASS_ERROR_NOTFILE:
		return L"Not a file stream.";
	case BASS_ERROR_NOHW:
		return L"No hardware voices available.";
	case BASS_ERROR_EMPTY:
		return L"MOD has no sequence data.";
	case BASS_ERROR_NONET:
		return L"No internet connection.";
	case BASS_ERROR_CREATE:
		return L"Couldn't create the file.";
	case BASS_ERROR_NOFX:
		return L"Effects not available.";
	case BASS_ERROR_NOTAVAIL:
		return L"Requested data not available.";
	case BASS_ERROR_DECODE:
		return L"Channel is a decoding channel.";
	case BASS_ERROR_DX:
		return L"DirectX init failed.";
	case BASS_ERROR_TIMEOUT:
		return L"Connection timed out.";
	case BASS_ERROR_FILEFORM:
		return L"Unsupported file format.";
	case BASS_ERROR_SPEAKER:
		return L"Speaker config unavailable.";
	case BASS_ERROR_VERSION:
		return L"BASS version mismatch.";
	case BASS_ERROR_CODEC:
		return L"Codec not available.";
	case BASS_ERROR_ENDED:
		return L"Stream has ended.";
	case BASS_ERROR_BUSY:
		return L"Device busy (exclusive mode or not ready).";
	case BASS_ERROR_WASAPI:
		return L"WASAPI not available.";
	case BASS_ERROR_WASAPI_BUFFER:
		return L"Invalid WASAPI buffer size.";
	case BASS_ERROR_WASAPI_RAW:
		return L"RAW mode not supported by device APO.";
	case BASS_ERROR_WASAPI_DENIED:
		return L"WASAPI access denied.";
	case BASS_ERROR_MIDI_INCLUDE:
		return L"SFZ #include file not found.";
	default:
		return L"Unknown error.";
	}
}

static LPCWSTR GetBASSErrorFix(INT code)
{
	switch (code)
	{
	case BASS_OK:
		return L"Nothing's wrong. You shouldn't see this.";

	case BASS_ERROR_MEM:
		return L"Not enough memory. Try a smaller SoundFont, or use 64-bit if available.";

	case BASS_ERROR_FILEOPEN:
		return L"Check the file exists and the drive is accessible.";

	case BASS_ERROR_DRIVER:
	case BASS_ERROR_BUSY:
		return L"Another app may have exclusive access to the device. "
			   L"Close other audio apps or check for another OmniMIDI instance.";

	case BASS_ERROR_BUFLOST:
		return L"Sound card timed out. Try increasing buffer size or switch devices.";

	case BASS_ERROR_FORMAT:
	case BASS_ERROR_FREQ:
		return L"Unsupported audio format. If playback works, ignore this. "
			   L"Otherwise change the frequency in settings.";

	case BASS_ERROR_NOCHAN:
		return L"Can't allocate stream. If VirtualMIDISynth 1.x is installed, remove it.";

	case BASS_ERROR_ILLPARAM:
		return L"ASIO/WASAPI device may not support a setting. Try disabling it or switch devices.";

	case BASS_ERROR_DEVICE:
		return L"Device doesn't exist. Check your audio settings.";

	case BASS_ERROR_NOPLAY:
		return L"Driver error - restart the app.";

	case BASS_ERROR_CREATE:
		return L"Permission denied or BASS error creating file.";

	case BASS_ERROR_NOTAVAIL:
		return L"Audio data not ready. Could be a buffer timeout or dead stream. Restart the app.";

	case BASS_ERROR_SPEAKER:
		return L"Output unavailable. Make sure nothing has exclusive control.";

	case BASS_ERROR_WASAPI_BUFFER:
		return L"Buffer size invalid or too small. Try a different value.";

	case BASS_ERROR_WASAPI_RAW:
		return L"Device APO doesn't support RAW mode. Try the stock Microsoft HD Audio driver.";

	case BASS_ERROR_MIDI_INCLUDE:
		return L"SoundFont may be corrupted. Try a different one.";

	case BASS_ERROR_HANDLE:
	case BASS_ERROR_INIT:
	case BASS_ERROR_ALREADY:
	case BASS_ERROR_VERSION:
	case BASS_ERROR_WASAPI:
	case BASS_ERROR_WASAPI_DENIED:
		return L"Restart the app. If it keeps happening, report an issue on GitHub.";

	default:
		return L"Unknown cause. Report an issue if this persists.";
	}
}

// Find OmniMIDIDialog.exe path
static BOOL GetDialogExePath(WCHAR *outPath, size_t outSize)
{
	WCHAR sysDir[MAX_PATH] = {0};
	if (!GetSystemDirectoryW(sysDir, MAX_PATH))
		return FALSE;

	swprintf_s(outPath, outSize, L"%s\\OmniMIDI\\OmniMIDIDialog.exe", sysDir);
	return GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES;
}

// Escape argument for command line (wrap in quotes if needed)
static void EscapeArg(char *dest, size_t destSize, const char *src)
{
	if (src == NULL || *src == '\0')
	{
		strcpy_s(dest, destSize, "\"\"");
		return;
	}
	// Wrap in quotes and escape any internal quotes
	sprintf_s(dest, destSize, "\"%s\"", src);
}

// Forward to external dialog exe
static BOOL LaunchDialogExe(int error, const char *engine, const char *context,
							const char *errorName, const char *desc, const char *fix, BOOL isFatal)
{
	WCHAR exePath[MAX_PATH] = {0};
	if (!GetDialogExePath(exePath, MAX_PATH))
		return FALSE;

	// Build command line
	char cmdLine[4096] = {0};
	char escapedName[256], escapedDesc[512], escapedFix[512], escapedContext[256], escapedEngine[64];

	EscapeArg(escapedEngine, sizeof(escapedEngine), engine);
	EscapeArg(escapedName, sizeof(escapedName), errorName);
	EscapeArg(escapedDesc, sizeof(escapedDesc), desc);
	EscapeArg(escapedFix, sizeof(escapedFix), fix);
	EscapeArg(escapedContext, sizeof(escapedContext), context);

	sprintf_s(cmdLine, sizeof(cmdLine),
			  "\"%S\" --type error --engine %s --code %d --name %s --desc %s --fix %s --context %s%s",
			  exePath, escapedEngine, error, escapedName, escapedDesc, escapedFix, escapedContext,
			  isFatal ? " --fatal" : "");

	// Launch process
	STARTUPINFOA si = {0};
	PROCESS_INFORMATION pi = {0};
	si.cb = sizeof(si);

	if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
		return FALSE;

	// Wait for dialog to close
	WaitForSingleObject(pi.hProcess, INFINITE);

	DWORD exitCode = 0;
	GetExitCodeProcess(pi.hProcess, &exitCode);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return TRUE;
}

// Fallback to MessageBox if dialog exe not available
static void ShowMessageBoxFallback(int error, const char *engine, const char *context,
								   const char *errorName, const char *desc, const char *fix)
{
	char title[256] = {0};
	char msg[2048] = {0};

	sprintf_s(title, "OmniMIDI - %s Error", engine);
	sprintf_s(msg, "%s error: %s (E%d)\n\n%s", engine, errorName, error, desc);

	if (context && *context)
		sprintf_s(msg + strlen(msg), sizeof(msg) - strlen(msg), "\n\nContext: %s", context);

	sprintf_s(msg + strlen(msg), sizeof(msg) - strlen(msg), "\n\nSuggested fix:\n%s", fix);

	if (_stricmp(engine, "BASSASIO") == 0 && error != BASS_ERROR_UNKNOWN)
		strcat_s(msg, "\n\nTry changing the ASIO device in the configurator.");

	strcat_s(msg, "\n\nReport issues at github.com/FrozenSnowy/OmniMIDI-Permafrost");

	MessageBoxA(NULL, msg, title, MB_OK | MB_ICONERROR);
}

// Show error dialog and log it
static void ShowBASSError(int error, int mode, const char *engine, const char *context, int showDialog)
{
	WCHAR errorNameW[128] = {0};
	WCHAR errorDescW[256] = {0};

	wcscpy_s(errorNameW, GetBASSErrorName(error));
	wcscpy_s(errorDescW, GetBASSErrorDescription(error));

	// Always log
	PrintBASSErrorMessageToDebugLog(errorNameW, errorDescW);

	if (!showDialog)
		return;

	// Convert wide strings to narrow
	char errorName[128] = {0};
	char desc[256] = {0};
	char fix[512] = {0};
	wcstombs(errorName, errorNameW, 128);
	wcstombs(desc, errorDescW, 256);
	wcstombs(fix, GetBASSErrorFix(error), 512);

	// Check if this is a fatal error
	BOOL isFatal = (error == BASS_ERROR_UNKNOWN ||
					(error >= BASS_ERROR_FILEOPEN && error <= BASS_ERROR_SSL) ||
					error == BASS_ERROR_ILLTYPE ||
					(error >= BASS_ERROR_NOPLAY && error <= BASS_ERROR_NOTFILE) ||
					error == BASS_ERROR_CODEC);

	// Try modern dialog first, fallback to MessageBox
	if (!LaunchDialogExe(error, engine, context, errorName, desc, fix, isFatal))
	{
		ShowMessageBoxFallback(error, engine, context, errorName, desc, fix);
	}

	if (isFatal)
	{
		exit(ERROR_INVALID_FUNCTION);
	}
}

// Check for BASS/BASSASIO errors and show dialog if needed
static BOOL CheckBASSError(BOOL isASIO, int mode, const char *context, BOOL showError)
{
	int error = isASIO ? BASS_ASIO_ErrorGetCode() : BASS_ErrorGetCode();
	if (error != BASS_OK)
	{
		ShowBASSError(error, mode, isASIO ? "BASSASIO" : "BASS", context, showError);
		return FALSE;
	}
	return TRUE;
}

// Backwards compatibility aliases
#define ReturnBASSError GetBASSErrorName
#define ReturnBASSErrorDesc GetBASSErrorDescription
#define ReturnBASSErrorFix GetBASSErrorFix
#define ShowError ShowBASSError
#define CheckUp CheckBASSError
