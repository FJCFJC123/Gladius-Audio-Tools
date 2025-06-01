#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vector>
#include <string>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

#define ID_BUTTON_EXTRACT 101
#define ID_BUTTON_REPACK  102

HWND hMainWnd;

// WAV header writer (for simple headers, not used in repack)
typedef struct WAVHeader {
    char     riff[4];
    uint32_t filesize;
    char     wave[4];
    char     fmt[4];
    uint32_t fmt_length;
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     data[4];
    uint32_t data_length;
} WAVHeader;

// File / folder pickers
static BOOL SelectFile(HWND hwnd, wchar_t* path, const wchar_t* filter) {
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = path;
    ofn.lpstrFilter = filter;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    return GetOpenFileNameW(&ofn);
}

static BOOL SelectFolder(HWND hwnd, wchar_t* path) {
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Select Directory";
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        SHGetPathFromIDListW(pidl, path);
        return TRUE;
    }
    return FALSE;
}

// Extraction logic: read headers from XBB, then pull data from XSB
static void DoExtract(HWND hwnd) {
    wchar_t xbbPath[MAX_PATH] = { 0 }, xsbPath[MAX_PATH] = { 0 }, outDir[MAX_PATH] = { 0 };
    if (!SelectFile(hwnd, xbbPath, L"XBB Files\0*.xbb\0All Files\0*.*\0")) return;
    wcscpy_s(xsbPath, xbbPath);
    PathRemoveExtensionW(xsbPath);
    wcscat_s(xsbPath, L".xsb");
    if (!SelectFolder(hwnd, outDir)) return;

    FILE* fXBB = _wfopen(xbbPath, L"rb");
    if (!fXBB) { MessageBoxW(hwnd, L"Failed to open .XBB", L"Error", MB_OK); return; }
    FILE* fXSB = _wfopen(xsbPath, L"rb");
    if (!fXSB) { fclose(fXBB); MessageBoxW(hwnd, L"Failed to open .XSB", L"Error", MB_OK); return; }

    uint32_t fileSize = 0, entryCount = 0;
    fread(&fileSize, 4, 1, fXBB);
    fread(&entryCount, 4, 1, fXBB);

    uint32_t entryStart = 8;
    for (uint32_t i = 0; i < entryCount; i++) {
        fseek(fXBB, entryStart + 4, SEEK_SET);
        uint32_t chunkSize = 0;
        fread(&chunkSize, 4, 1, fXBB);
        uint32_t headerLen = chunkSize;

        fseek(fXBB, entryStart, SEEK_SET);
        uint8_t* headerBuf = (uint8_t*)malloc(headerLen);
        if (!headerBuf) break;
        fread(headerBuf, 1, headerLen, fXBB);

        fseek(fXBB, entryStart + headerLen, SEEK_SET);
        uint32_t dataOffset = 0, dataLength = 0;
        fread(&dataOffset, 4, 1, fXBB);
        fread(&dataLength, 4, 1, fXBB);

        if (dataLength > 0) {
            uint32_t newChunkSize = (headerLen - 8) + dataLength;
            memcpy(headerBuf + 4, &newChunkSize, 4);
            memcpy(headerBuf + 44, &dataLength, 4);
        }

        wchar_t outPath[MAX_PATH];
        swprintf(outPath, MAX_PATH, L"%s\\track_%03u.wav", outDir, i);

        FILE* out = _wfopen(outPath, L"wb");
        if (!out) { free(headerBuf); break; }
        fwrite(headerBuf, 1, headerLen, out);
        free(headerBuf);

        if (dataLength > 0) {
            fseek(fXSB, dataOffset, SEEK_SET);
            uint8_t* dataBuf = (uint8_t*)malloc(dataLength);
            if (dataBuf) {
                fread(dataBuf, 1, dataLength, fXSB);
                fwrite(dataBuf, 1, dataLength, out);
                free(dataBuf);
            }
        }
        fclose(out);

        entryStart += headerLen + 8;
    }

    fclose(fXBB);
    fclose(fXSB);
    MessageBoxW(hwnd, L"Extraction complete!", L"Done", MB_OK);
}

// Repack logic: rebuild XBB/XSB from WAV files
static void DoRepack(HWND hwnd) {
    wchar_t wavDir[MAX_PATH] = { 0 }, xbbPath[MAX_PATH] = { 0 }, xsbPath[MAX_PATH] = { 0 };
    if (!SelectFolder(hwnd, wavDir)) return;

    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = xbbPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"XBB Files\0*.xbb\0All Files\0*.*\0";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;

    wcscpy_s(xsbPath, xbbPath);
    PathRemoveExtensionW(xsbPath);
    wcscat_s(xsbPath, L".xsb");

    // Gather .wav files
    std::vector<std::wstring> wavFiles;
    WIN32_FIND_DATAW fd;
    wchar_t search[MAX_PATH];
    swprintf(search, MAX_PATH, L"%s\\*.wav", wavDir);
    HANDLE hFind = FindFirstFileW(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            wchar_t full[MAX_PATH];
            PathCombineW(full, wavDir, fd.cFileName);
            wavFiles.push_back(full);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    uint32_t entryCount = (uint32_t)wavFiles.size();
    FILE* fXBB = _wfopen(xbbPath, L"wb");
    FILE* fXSB = _wfopen(xsbPath, L"wb");
    if (!fXBB || !fXSB) {
        MessageBoxW(hwnd, L"Failed to create output files", L"Error", MB_OK);
        if (fXBB) fclose(fXBB);
        if (fXSB) fclose(fXSB);
        return;
    }

    // Write placeholder header
    fwrite("\0\0\0\0", 4, 1, fXBB);
    fwrite(&entryCount, 4, 1, fXBB);

    uint32_t entryStart = 8;
    uint32_t currentOffset = 0;

    for (uint32_t i = 0; i < entryCount; i++) {
        // Read WAV file
        FILE* w = _wfopen(wavFiles[i].c_str(), L"rb");
        fseek(w, 0, SEEK_END);
        uint32_t fileSize = ftell(w);
        fseek(w, 0, SEEK_SET);

        // Read chunkSize and dataLength from WAV header
        uint32_t chunkSize = 0, dataLength = 0;
        fseek(w, 4, SEEK_SET);
        fread(&chunkSize, 4, 1, w);
        fseek(w, 44, SEEK_SET);
        fread(&dataLength, 4, 1, w);

        uint32_t headerLen = (chunkSize + 8) - dataLength;

        // Read headerBuf
        fseek(w, 0, SEEK_SET);
        uint8_t* headerBuf = (uint8_t*)malloc(headerLen);
        fread(headerBuf, 1, headerLen, w);

        // Read dataBuf
        uint8_t* dataBuf = (uint8_t*)malloc(dataLength);
        fseek(w, headerLen, SEEK_SET);
        fread(dataBuf, 1, dataLength, w);
        fclose(w);

        // Write entry header
        fseek(fXBB, entryStart, SEEK_SET);
        fwrite(headerBuf, 1, headerLen, fXBB);
        entryStart += headerLen;

        // Write metadata (dataOffset, dataLength)
        fwrite(&currentOffset, 4, 1, fXBB);
        fwrite(&dataLength, 4, 1, fXBB);
        entryStart += 8;

        // Write raw audio to XSB
        fseek(fXSB, currentOffset, SEEK_SET);
        fwrite(dataBuf, 1, dataLength, fXSB);

        currentOffset += dataLength;
        free(headerBuf);
        free(dataBuf);
    }

    // Finalize XBB fileSize
    uint32_t finalSize = entryStart;
    fseek(fXBB, 0, SEEK_SET);
    fwrite(&finalSize, 4, 1, fXBB);

    fclose(fXBB);
    fclose(fXSB);
    MessageBoxW(hwnd, L"Repack complete!", L"Done", MB_OK);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == ID_BUTTON_EXTRACT)
            DoExtract(hwnd);
        else if (LOWORD(wParam) == ID_BUTTON_REPACK)
            DoRepack(hwnd);
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"XBBExtractor";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    hMainWnd = CreateWindowW(wc.lpszClassName, L"XBB/XSB Audio Tool",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 500, 200,
        NULL, NULL, hInst, NULL);

    CreateWindowW(L"BUTTON", L"Extract Audio Files",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        50, 40, 180, 30, hMainWnd, (HMENU)ID_BUTTON_EXTRACT, hInst, NULL);

    CreateWindowW(L"BUTTON", L"Repack WAV to XBB/XSB",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        260, 40, 180, 30, hMainWnd, (HMENU)ID_BUTTON_REPACK, hInst, NULL);

    ShowWindow(hMainWnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}