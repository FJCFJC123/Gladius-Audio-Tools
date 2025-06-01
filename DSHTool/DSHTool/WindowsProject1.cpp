#define UNICODE
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commctrl.h>   // For ListView
#pragma comment(lib, "Comctl32.lib") // Link Comctl32.lib

// big-endian helpers
static inline unsigned int get32be(const unsigned char* p) {
    return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 |
        (unsigned int)p[2] << 8 | (unsigned int)p[3];
}
static inline void put32be(unsigned char* p, unsigned int v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

#define FILE_HEADER_SIZE 0x20    // 32-byte file header (count + padding)
#define NAME_REGION_SIZE 0x100   // 256-byte filename+padding per entry
#define DSP_HEADER_SIZE  0x60    // 96-byte DSP metadata per entry
#define ENTRY_SIZE       (NAME_REGION_SIZE + DSP_HEADER_SIZE) // 0x160 (352 bytes)

// --- Global variables for ListView and DSP file list ---
HWND g_hListView;
#define MAX_DSP_FILES 1024
wchar_t g_dspFilePaths[MAX_DSP_FILES][MAX_PATH];
int g_dspFileCount = 0;

// --- Control IDs ---
#define IDC_LISTVIEW_DSPS 101
#define ID_BUTTON_EXTRACT 1
#define ID_BUTTON_REPACK 2
#define ID_BUTTON_CLEAR_LIST 3 // New button ID


void ExtractDsh(HWND hwnd) {
    OPENFILENAMEW ofn = { sizeof(ofn) };
    wchar_t dshPath[MAX_PATH] = L"", txtPath[MAX_PATH] = L"";
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"DSH Files (*.dsh)\0*.dsh\0";
    ofn.lpstrFile = dshPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = L"Open DSH File";
    if (!GetOpenFileNameW(&ofn)) return;

    wcscpy(txtPath, dshPath);
    wchar_t* dot = wcsrchr(txtPath, L'.');
    if (dot) wcscpy(dot, L".txt"); else wcscat(txtPath, L".txt");

    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0";
    ofn.lpstrFile = txtPath;
    ofn.lpstrTitle = L"Save As TXT";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;

    FILE* dsh = _wfopen(dshPath, L"rb");
    FILE* txt = _wfopen(txtPath, L"w,ccs=UTF-8");
    if (!dsh || !txt) {
        if (dsh) fclose(dsh);
        if (txt) fclose(txt);
        MessageBoxW(hwnd, L"Failed to open files.", L"Error", MB_OK);
        return;
    }

    unsigned char buf4[4];
    fread(buf4, 1, 4, dsh);
    unsigned int count = get32be(buf4);
    fwprintf(txt, L"Entry Count: %u\n\n", count);

    fseek(dsh, FILE_HEADER_SIZE, SEEK_SET);

    unsigned char* entry = (unsigned char*)malloc(ENTRY_SIZE);
    if (!entry) {
        fclose(dsh); fclose(txt);
        MessageBoxW(hwnd, L"Memory allocation failed.", L"Error", MB_OK);
        return;
    }

    for (unsigned int i = 0; i < count; i++) {
        if (fread(entry, 1, ENTRY_SIZE, dsh) != ENTRY_SIZE) {
            fwprintf(txt, L"Error: Could not read full entry %u.\n", i + 1);
            break;
        }
        char name8_raw[NAME_REGION_SIZE + 1];
        memcpy(name8_raw, entry, NAME_REGION_SIZE);
        name8_raw[NAME_REGION_SIZE] = '\0';

        char* first_null = (char*)memchr(name8_raw, '\0', NAME_REGION_SIZE);
        size_t name_len = first_null ? (first_null - name8_raw) : NAME_REGION_SIZE;

        wchar_t name16[MAX_PATH];
        int charsConverted = MultiByteToWideChar(CP_UTF8, 0, name8_raw, (int)name_len, name16, MAX_PATH - 1);
        name16[charsConverted] = L'\0';

        fwprintf(txt, L"%3u: %s", i + 1, name16);

        unsigned char* dh = entry + NAME_REGION_SIZE;
        unsigned int samples = get32be(dh);
        unsigned int nibbleCnt = get32be(dh + 4);
        unsigned int rate = get32be(dh + 8);
        unsigned short loopFlg = (unsigned short)((dh[12] << 8) | dh[13]);
        unsigned int loopStart = get32be(dh + 16);
        unsigned int loopEnd = get32be(dh + 20);
        fwprintf(txt, L"  samples=%u nibble=%u rate=%u loop=%u start=%u end=%u\n",
            samples, nibbleCnt, rate, loopFlg, loopStart, loopEnd);
    }
    free(entry);
    fclose(txt);
    fclose(dsh);
    MessageBoxW(hwnd, L"Extraction to TXT complete.", L"Done", MB_OK);
}

void ClearDspList() {
    if (g_hListView) {
        ListView_DeleteAllItems(g_hListView);
    }
    // Optionally, zero out the path array if memory hygiene is critical,
    // but just resetting the count is usually sufficient as old paths won't be accessed.
    // memset(g_dspFilePaths, 0, sizeof(g_dspFilePaths)); 
    g_dspFileCount = 0;
}

void AddFileToList(const wchar_t* fullPath) {
    if (g_dspFileCount >= MAX_DSP_FILES) {
        MessageBoxW(NULL, L"Maximum number of files (1024) reached.", L"Info", MB_OK);
        return;
    }

    wchar_t fileName[MAX_PATH];
    const wchar_t* pName = wcsrchr(fullPath, L'\\');
    if (pName) {
        wcscpy(fileName, pName + 1);
    }
    else {
        wcscpy(fileName, fullPath);
    }

    LVITEMW lvi = { 0 };
    lvi.mask = LVIF_TEXT;
    lvi.iItem = g_dspFileCount;
    lvi.iSubItem = 0;
    lvi.pszText = fileName;

    if (ListView_InsertItem(g_hListView, &lvi) == -1) {
        MessageBoxW(NULL, L"Failed to add item to list view.", L"Error", MB_OK);
        return;
    }

    wcscpy(g_dspFilePaths[g_dspFileCount], fullPath);
    g_dspFileCount++;
}

void RepackDshFromList(HWND hwnd) {
    if (g_dspFileCount == 0) {
        MessageBoxW(hwnd, L"No DSP files in the list to repack.", L"Info", MB_OK);
        return;
    }

    OPENFILENAMEW ofn = { sizeof(ofn) };
    wchar_t outPath[MAX_PATH] = L"output.dsh"; // Suggest a default name
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"DSH Files (*.dsh)\0*.dsh\0";
    ofn.lpstrFile = outPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Save DSH As";
    if (!GetSaveFileNameW(&ofn)) return;

    FILE* f = _wfopen(outPath, L"wb");
    if (!f) { MessageBoxW(hwnd, L"Cannot create DSH file.", L"Error", MB_OK); return; }

    unsigned char buf4[4]; put32be(buf4, (unsigned int)g_dspFileCount);
    fwrite(buf4, 1, 4, f);
    for (int i = 4; i < FILE_HEADER_SIZE; i++) fputc(0, f);

    unsigned char* entry = (unsigned char*)malloc(ENTRY_SIZE);
    if (!entry) {
        MessageBoxW(hwnd, L"Memory allocation failed for entry buffer.", L"Error", MB_OK);
        fclose(f);
        return;
    }

    BOOL all_successful = TRUE;
    for (int i = 0; i < g_dspFileCount; i++) {
        memset(entry, 0, ENTRY_SIZE);

        wchar_t dspFilenameOnly[MAX_PATH];
        const wchar_t* pName = wcsrchr(g_dspFilePaths[i], L'\\');
        if (pName) {
            wcscpy(dspFilenameOnly, pName + 1);
        }
        else {
            wcscpy(dspFilenameOnly, g_dspFilePaths[i]);
        }

        char name8_utf8[NAME_REGION_SIZE]; // Buffer for UTF-8, exactly NAME_REGION_SIZE for safety
        memset(name8_utf8, 0, NAME_REGION_SIZE); // Ensure it's zeroed

        int bytesWritten = WideCharToMultiByte(CP_UTF8, 0, dspFilenameOnly, -1,
            name8_utf8, NAME_REGION_SIZE - 1, // Leave space for null if it fits
            NULL, NULL);

        if (bytesWritten > 0) {
            // WideCharToMultiByte with -1 for wcslen includes the null terminator if it fits.
            // If the UTF-8 string (including null) is >= NAME_REGION_SIZE, it's truncated.
            // We copy strlen(name8_utf8) bytes, which is the content without null.
            // If strlen(name8_utf8) == NAME_REGION_SIZE, the region won't be null-terminated.
            // If strlen(name8_utf8) < NAME_REGION_SIZE, it will be null-terminated within name8_utf8,
            // and the rest of 'entry's name region is already zeroed.
            size_t actualNameLen = strlen(name8_utf8);
            memcpy(entry, name8_utf8, actualNameLen);
            // The rest of the entry name region is already zeroed from the initial memset(entry, 0, ENTRY_SIZE)
        }
        else {
            // Error in conversion or empty filename
            all_successful = FALSE; // Mark as not fully successful
            wchar_t errorMsg[MAX_PATH + 100];
            swprintf(errorMsg, sizeof(errorMsg) / sizeof(wchar_t),
                L"Warning: Could not convert filename '%s' to UTF-8. Name will be blank.",
                dspFilenameOnly);
            MessageBoxW(hwnd, errorMsg, L"Warning", MB_OK);
        }


        FILE* dsp = _wfopen(g_dspFilePaths[i], L"rb");
        if (dsp) {
            fseek(dsp, 0, SEEK_END);
            long dsp_size = ftell(dsp);
            fseek(dsp, 0, SEEK_SET);

            if (dsp_size < DSP_HEADER_SIZE) {
                all_successful = FALSE; // Mark as not fully successful
                wchar_t errorMsg[MAX_PATH + 200];
                swprintf(errorMsg, sizeof(errorMsg) / sizeof(wchar_t),
                    L"Warning: DSP file '%s' is smaller (%ld bytes) than DSP header size (%d bytes). Metadata will be zeroed.",
                    g_dspFilePaths[i], dsp_size, DSP_HEADER_SIZE);
                MessageBoxW(hwnd, errorMsg, L"Warning", MB_OK);
            }
            else {
                fread(entry + NAME_REGION_SIZE, 1, DSP_HEADER_SIZE, dsp);
            }
            fclose(dsp);
        }
        else {
            all_successful = FALSE; // Mark as not fully successful
            wchar_t errorMsg[MAX_PATH + 100];
            swprintf(errorMsg, sizeof(errorMsg) / sizeof(wchar_t),
                L"Warning: Could not open DSP file '%s'. Its metadata will be zeroed.",
                g_dspFilePaths[i]);
            MessageBoxW(hwnd, errorMsg, L"Warning", MB_OK);
        }
        fwrite(entry, 1, ENTRY_SIZE, f);
    }
    free(entry);
    fclose(f);

    if (all_successful) {
        MessageBoxW(hwnd, L"Repacked DSH complete from list.", L"Done", MB_OK);
    }
    else {
        MessageBoxW(hwnd, L"Repacked DSH complete from list, but some warnings occurred (see previous messages).", L"Done with Warnings", MB_ICONWARNING | MB_OK);
    }

    // Clear the list after repacking
    ClearDspList();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BUTTON_EXTRACT) {
            ExtractDsh(hwnd);
        }
        else if (LOWORD(wParam) == ID_BUTTON_REPACK) {
            RepackDshFromList(hwnd);
        }
        else if (LOWORD(wParam) == ID_BUTTON_CLEAR_LIST) { // Handle new button
            ClearDspList();
        }
        return 0;
    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;
        UINT numFiles = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        wchar_t filePath[MAX_PATH];

        for (UINT i = 0; i < numFiles; ++i) {
            if (DragQueryFileW(hDrop, i, filePath, MAX_PATH) > 0) {
                const wchar_t* ext = wcsrchr(filePath, L'.');
                if (ext && _wcsicmp(ext, L".dsp") == 0) {
                    AddFileToList(filePath);
                }
            }
        }
        DragFinish(hDrop);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"DshToolWindowClass";
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"DSH Tool GUI", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 370, // Increased height for clear button
        NULL, NULL, hInst, NULL);

    if (!hwnd) {
        MessageBoxW(NULL, L"Window Creation Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    g_hListView = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"SysListView32", L"",
        WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        10, 10, 365, 150,
        hwnd, (HMENU)IDC_LISTVIEW_DSPS, hInst, NULL
    );

    if (!g_hListView) {
        MessageBoxW(hwnd, L"ListView Creation Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
    }
    else {
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt = LVCFMT_LEFT;
        lvc.cx = 350;
        wchar_t columnText[] = L"DSP Files (Drag & Drop .dsp files here)";
        lvc.pszText = columnText;
        ListView_InsertColumn(g_hListView, 0, &lvc);
    }

    CreateWindowW(L"BUTTON", L"Extract DSH to TXT",
        WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        50, 170, 300, 40, hwnd, (HMENU)ID_BUTTON_EXTRACT, hInst, NULL);

    CreateWindowW(L"BUTTON", L"Build DSH from List",
        WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        50, 220, 300, 40, hwnd, (HMENU)ID_BUTTON_REPACK, hInst, NULL);

    // New "Clear List" button
    CreateWindowW(L"BUTTON", L"Clear DSP List",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, // Not default
        50, 270, 300, 40, hwnd, (HMENU)ID_BUTTON_CLEAR_LIST, hInst, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = { 0 };
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}