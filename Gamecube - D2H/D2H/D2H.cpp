#define UNICODE
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

// Big-endian helpers
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

// File layout constants
#define FILE_HEADER_SIZE   0x20
#define NAME_REGION_SIZE   0x100
#define DS2_HEADER_SIZE    0xC0
#define ENTRY_SIZE         (NAME_REGION_SIZE + DS2_HEADER_SIZE)

// Globals
static HWND        g_hList = NULL;
static WNDPROC     g_ListProc = NULL; // For subclassing listbox
static std::vector<std::wstring> g_ds2Files;

// Subclass procedure to catch WM_DROPFILES on the listbox
LRESULT CALLBACK ListBoxProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DROPFILES)
        // Forward to main window for unified handling
        return SendMessageW(GetParent(hWnd), WM_DROPFILES, wParam, lParam);
    return CallWindowProcW(g_ListProc, hWnd, msg, wParam, lParam);
}

// Browse-for-folder (not used for repack but kept for Extract)
BOOL BrowseForFolder(HWND hwnd, wchar_t* path, const wchar_t* title) {
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = hwnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path)) {
            CoTaskMemFree(pidl);
            return TRUE;
        }
        CoTaskMemFree(pidl);
    }
    return FALSE;
}

// Extract .d2h index to a human-readable TXT (unchanged)
void ExtractD2h(HWND hwnd) {
    OPENFILENAMEW ofn = { sizeof(ofn) };
    wchar_t d2hPath[MAX_PATH] = L"", txtPath[MAX_PATH] = L"";

    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"D2H Files (*.d2h)\0*.d2h\0";
    ofn.lpstrFile = d2hPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = L"Open D2H File";
    if (!GetOpenFileNameW(&ofn)) return;

    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0";
    ofn.lpstrFile = txtPath;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrTitle = L"Save As TXT";
    if (!GetSaveFileNameW(&ofn)) return;

    FILE* d2h = _wfopen(d2hPath, L"rb");
    FILE* txt = _wfopen(txtPath, L"w");
    if (!d2h || !txt) {
        MessageBoxW(hwnd, L"Cannot open files", L"Error", MB_OK | MB_ICONERROR);
        if (d2h) fclose(d2h);
        if (txt) fclose(txt);
        return;
    }

    unsigned char buf4[4];
    fread(buf4, 1, 4, d2h);
    unsigned int count = get32be(buf4);
    fwprintf(txt, L"Entry Count: %u\n\n", count);

    fseek(d2h, FILE_HEADER_SIZE, SEEK_SET);

    unsigned char* entry = (unsigned char*)malloc(ENTRY_SIZE);
    for (unsigned int i = 0; i < count; i++) {
        fread(entry, 1, ENTRY_SIZE, d2h);
        char name8[NAME_REGION_SIZE + 1] = { 0 };
        size_t nlen = strnlen((char*)entry, NAME_REGION_SIZE);
        memcpy(name8, entry, nlen);
        fwprintf(txt, L"Entry %u: %hs\n", i + 1, name8);
        unsigned char* meta = entry + NAME_REGION_SIZE;
        unsigned int samples = get32be(meta);
        unsigned int nibCnt = get32be(meta + 4);
        unsigned int rate = get32be(meta + 8);
        unsigned short loopF = (meta[12] << 8) | meta[13];
        unsigned int loopStart = get32be(meta + 16);
        unsigned int loopEnd = get32be(meta + 20);
        fwprintf(txt,
            L"  Samples: %u\n  NibbleCount: %u\n  Rate: %u\n"
            L"  LoopFlag: %u\n  LoopStart: %u\n  LoopEnd: %u\n\n",
            samples, nibCnt, rate, loopF, loopStart, loopEnd
        );
    }
    free(entry);
    fclose(d2h);
    fclose(txt);
    MessageBoxW(hwnd, L"Extraction to TXT complete", L"Done", MB_OK);
}

// Repack .d2h index from the dropped .ds2 files in drop order
void RepackD2h(HWND hwnd) {
    if (g_ds2Files.empty()) {
        MessageBoxW(hwnd, L"No DS2 files added.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    wchar_t outPath[MAX_PATH] = L""; // Must be zero-initialized!
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"D2H Files (*.d2h)\0*.d2h\0";
    ofn.lpstrFile = outPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrTitle = L"Save D2H As";
    ofn.lpstrDefExt = L"d2h";
    if (!GetSaveFileNameW(&ofn)) {
        DWORD err = CommDlgExtendedError();
        if (err) {
            wchar_t msg[128];
            swprintf(msg, 128, L"Dialog Error: 0x%08X", err);
            MessageBoxW(hwnd, msg, L"Common Dialog Error", MB_OK | MB_ICONERROR);
        }
        return;
    }

    FILE* f = _wfopen(outPath, L"wb");
    if (!f) {
        MessageBoxW(hwnd, L"Cannot create D2H file at the selected location.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Write header: count + padding
    unsigned char buf4[4];
    put32be(buf4, (unsigned int)g_ds2Files.size());
    fwrite(buf4, 1, 4, f);
    for (int i = 4; i < FILE_HEADER_SIZE; i++) fputc(0, f);

    // Write entries in drop order
    unsigned char* entry = (unsigned char*)malloc(ENTRY_SIZE);
    for (size_t i = 0; i < g_ds2Files.size(); i++) {
        memset(entry, 0, ENTRY_SIZE);

        // Filename region
        const std::wstring& fullpath = g_ds2Files[i];
        size_t slash = fullpath.find_last_of(L"\\/");
        std::wstring name = (slash == std::wstring::npos)
            ? fullpath
            : fullpath.substr(slash + 1);

        char name8[NAME_REGION_SIZE + 1] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0,
            name.c_str(), -1,
            name8, sizeof(name8),
            NULL, NULL);
        size_t len = strlen(name8);
        if (len > NAME_REGION_SIZE) len = NAME_REGION_SIZE;
        memcpy(entry, name8, len);

        // DS2 metadata
        FILE* ds2 = _wfopen(fullpath.c_str(), L"rb");
        if (ds2) {
            fread(entry + NAME_REGION_SIZE, 1, DS2_HEADER_SIZE, ds2);
            fclose(ds2);
        }

        fwrite(entry, 1, ENTRY_SIZE, f);
    }
    free(entry);
    fclose(f);

    MessageBoxW(hwnd, L"Repacked D2H complete", L"Done", MB_OK);

    g_ds2Files.clear();
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
}

// Main window proc: handles button clicks & file drops
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) ExtractD2h(hwnd);
        else if (LOWORD(wParam) == 2) RepackD2h(hwnd);
        return 0;

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        UINT   nFiles = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < nFiles; i++) {
            wchar_t buf[MAX_PATH];
            DragQueryFileW(hDrop, i, buf, MAX_PATH);
            wchar_t* ext = wcsrchr(buf, L'.');
            if (ext && _wcsicmp(ext, L".ds2") == 0) {
                // Avoid accidental duplicate entries (optional)
                bool exists = false;
                for (const auto& s : g_ds2Files)
                    if (_wcsicmp(s.c_str(), buf) == 0) { exists = true; break; }
                if (!exists) {
                    g_ds2Files.emplace_back(buf);
                    SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)buf);
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
    const wchar_t CLASS[] = L"D2hToolWindow";
    WNDCLASSW wc = { };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS, L"D2H Tool GUI", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 640,
        NULL, NULL, hInst, NULL
    );
    if (!hwnd) return 0;

    // main window accepts drops
    DragAcceptFiles(hwnd, TRUE);

    // listbox for dropped files
    g_hList = CreateWindowW(
        L"LISTBOX", NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER |
        LBS_EXTENDEDSEL | LBS_NOINTEGRALHEIGHT,
        10, 10, 1050, 480,
        hwnd, (HMENU)3, hInst, NULL
    );

    // subclass it so WM_DROPFILES goes to our WindowProc
    g_ListProc = (WNDPROC)SetWindowLongPtrW(
        g_hList, GWLP_WNDPROC,
        (LONG_PTR)ListBoxProc
    );
    // now listbox also accepts drops
    DragAcceptFiles(g_hList, TRUE);

    // buttons
    CreateWindowW(L"BUTTON", L"Extract D2H to TXT",
        WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        10, 560, 520, 40,
        hwnd, (HMENU)1, hInst, NULL
    );
    CreateWindowW(L"BUTTON", L"Repack D2H from DS2s",
        WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        540, 560, 520, 40,
        hwnd, (HMENU)2, hInst, NULL
    );

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}