#define UNICODE
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DSP_HEADER_SIZE 0x60
#define ALIGN_8(x) (((x) + 7) & ~7)

unsigned int get32be(unsigned char* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

unsigned short get16be(unsigned char* p) {
    return (p[0] << 8) | p[1];
}

void put32be(unsigned char* p, unsigned int val) {
    p[0] = (val >> 24) & 0xFF;
    p[1] = (val >> 16) & 0xFF;
    p[2] = (val >> 8) & 0xFF;
    p[3] = val & 0xFF;
}

void put16be(unsigned char* p, unsigned short val) {
    p[0] = (val >> 8) & 0xFF;
    p[1] = val & 0xFF;
}

BOOL BrowseForFolder(HWND hwnd, wchar_t* outPath, const wchar_t* title) {
    BROWSEINFOW bi = { 0 };
    bi.lpszTitle = title;
    bi.ulFlags = BIF_USENEWUI;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);

    if (pidl != 0) {
        SHGetPathFromIDListW(pidl, outPath);
        return TRUE;
    }
    return FALSE;
}

void ExtractSptSpd(HWND hwnd) {
    OPENFILENAME ofn = { 0 };
    wchar_t sptPath[MAX_PATH] = L"";
    wchar_t spdPath[MAX_PATH] = L"";
    wchar_t outDir[MAX_PATH] = L"";

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"SPT Files (*.spt)\0*.spt\0";
    ofn.lpstrFile = sptPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = L"Select SPT File";

    if (!GetOpenFileName(&ofn)) return;

    ofn.lpstrFilter = L"SPD Files (*.spd)\0*.spd\0";
    ofn.lpstrFile = spdPath;
    ofn.lpstrTitle = L"Select SPD File";
    if (!GetOpenFileName(&ofn)) return;

    if (!BrowseForFolder(hwnd, outDir, L"Select Output Folder for DSPs")) return;

    FILE* spt = _wfopen(sptPath, L"rb");
    FILE* spd = _wfopen(spdPath, L"rb");
    if (!spt || !spd) {
        MessageBoxW(hwnd, L"Failed to open SPT/SPD files.", L"Error", MB_OK);
        return;
    }

    unsigned char buf[4];
    fread(buf, 1, 4, spt);
    int filecount = get32be(buf);

    int part1idx = 4;
    int part2idx = part1idx + filecount * 0x1C;
    int dataoff = 0;

    for (int i = 0; i < filecount; i++) {
        unsigned char buf1[0x1C], buf2[0x2E];

        fseek(spt, part1idx, SEEK_SET);
        fread(buf1, 1, 0x1C, spt);
        fseek(spt, part2idx, SEEK_SET);
        fread(buf2, 1, 0x2E, spt);

        int nextdataoff = get32be(buf1 + 0x10) / 2 + 1;
        wchar_t fname[MAX_PATH];
        swprintf(fname, MAX_PATH, L"%s\\%03d.dsp", outDir, i);
        FILE* out = _wfopen(fname, L"wb");
        if (!out) continue;

        int size = (nextdataoff - dataoff);
        int samples = size * 7 / 4;

        put32be(buf, samples); fwrite(buf, 1, 4, out);
        put32be(buf, size * 2); fwrite(buf, 1, 4, out);
        fwrite(buf1 + 4, 1, 4, out);
        put16be(buf, get32be(buf1) & 1); fwrite(buf, 1, 2, out);
        put16be(buf, 0); fwrite(buf, 1, 2, out);
        put32be(buf, get32be(buf1 + 8) - dataoff * 2); fwrite(buf, 1, 4, out);
        put32be(buf, get32be(buf1 + 12) - dataoff * 2); fwrite(buf, 1, 4, out);
        put32be(buf, 2); fwrite(buf, 1, 4, out);
        fwrite(buf2, 1, 0x2E, out);

        fseek(out, 0x60, SEEK_SET);
        fseek(spd, dataoff, SEEK_SET);
        for (int j = 0; j < size; j++) fputc(fgetc(spd), out);
        fclose(out);

        dataoff = ((nextdataoff + 7) / 8) * 8;
        part1idx += 0x1C;
        part2idx += 0x2E;
    }
    fclose(spt);
    fclose(spd);
    MessageBoxW(hwnd, L"Extraction Complete!", L"Success", MB_OK);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static wchar_t folderPath[MAX_PATH] = L"";

    switch (uMsg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            if (BrowseForFolder(hwnd, folderPath, L"Select DSP Folder")) {
                MessageBoxW(hwnd, folderPath, L"Folder Selected", MB_OK);

                wchar_t outputSPT[MAX_PATH] = L"";
                wchar_t outputSPD[MAX_PATH] = L"";

                OPENFILENAME ofn = { 0 };
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"SPT Files (*.spt)\0*.spt\0";
                ofn.lpstrFile = outputSPT;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT;
                ofn.lpstrTitle = L"Save As SPT File";
                if (!GetSaveFileName(&ofn)) return 0;

                wcscpy(outputSPD, outputSPT);
                size_t len = wcslen(outputSPD);
                if (len > 4) wcscpy(&outputSPD[len - 4], L".spd");

                FILE* out_spt = _wfopen(outputSPT, L"wb");
                FILE* out_spd = _wfopen(outputSPD, L"wb");
                if (!out_spt || !out_spd) {
                    MessageBoxW(hwnd, L"Failed to create output files.", L"Error", MB_OK);
                    return 0;
                }

                int filecount = 0;
                for (int i = 0;; i++) {
                    wchar_t dsp_path_w[MAX_PATH];
                    swprintf(dsp_path_w, MAX_PATH, L"%s\\%03d.dsp", folderPath, i);
                    if (GetFileAttributesW(dsp_path_w) == INVALID_FILE_ATTRIBUTES) break;
                    filecount++;
                }

                unsigned char buf[4];
                put32be(buf, filecount);
                fwrite(buf, 1, 4, out_spt);

                unsigned int data_offset = 0;
                for (int i = 0; i < filecount; i++) {
                    wchar_t dsp_path_w[MAX_PATH];
                    char dsp_path[MAX_PATH];
                    swprintf(dsp_path_w, MAX_PATH, L"%s\\%03d.dsp", folderPath, i);
                    wcstombs(dsp_path, dsp_path_w, MAX_PATH);

                    FILE* dsp = fopen(dsp_path, "rb");
                    if (!dsp) continue;

                    unsigned char dsp_header[DSP_HEADER_SIZE];
                    fread(dsp_header, 1, DSP_HEADER_SIZE, dsp);

                    unsigned int sample_rate = get32be(dsp_header + 8);
                    unsigned int loop_flag = get16be(dsp_header + 0x0C);
                    unsigned int loop_start = get32be(dsp_header + 0x10);
                    unsigned int loop_end = get32be(dsp_header + 0x14);
                    unsigned int nibble_count = get32be(dsp_header + 4);
                    unsigned int data_size = nibble_count / 2;
                    unsigned int next_data_offset = data_offset + data_size;

                    unsigned char part1[0x1C] = { 0 };
                    put32be(part1, loop_flag ? 1 : 0);
                    put32be(part1 + 4, sample_rate);
                    put32be(part1 + 8, loop_start * 2 + data_offset * 2);
                    put32be(part1 + 12, loop_end * 2 + data_offset * 2);
                    put32be(part1 + 0x10, next_data_offset * 2);
                    fwrite(part1, 1, 0x1C, out_spt);

                    unsigned char part2[0x2E] = { 0 };
                    memcpy(part2, dsp_header + 0x1C, 0x2E);
                    fwrite(part2, 1, 0x2E, out_spt);

                    fseek(dsp, DSP_HEADER_SIZE, SEEK_SET);
                    unsigned char* adpcm_data = (unsigned char*)malloc(data_size);
                    fread(adpcm_data, 1, data_size, dsp);
                    fwrite(adpcm_data, 1, data_size, out_spd);

                    int padding = ALIGN_8(data_size) - data_size;
                    for (int j = 0; j < padding; j++) fputc(0, out_spd);

                    free(adpcm_data);
                    fclose(dsp);

                    data_offset = ALIGN_8(next_data_offset);
                }

                fclose(out_spt);
                fclose(out_spd);
                MessageBoxW(hwnd, L"Repack Complete!", L"Success", MB_OK);
            }
        }
        else if (LOWORD(wParam) == 2) {
            ExtractSptSpd(hwnd);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"SptexRepackWindow";

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"SPTex Repack GUI", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 250, NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) return 0;

    CreateWindowW(L"BUTTON", L"Repack DSP Folder to SPT/SPD", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        50, 40, 300, 40, hwnd, (HMENU)1, hInstance, NULL);

    CreateWindowW(L"BUTTON", L"Extract DSPs from SPT/SPD", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        50, 100, 300, 40, hwnd, (HMENU)2, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = { 0 };
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
