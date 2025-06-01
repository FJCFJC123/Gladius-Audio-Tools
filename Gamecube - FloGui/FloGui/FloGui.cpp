#include <algorithm>
#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <set>
#include <tuple>
#include <cstring> // For strlen, strcmp
#include <cstdio>  // For snprintf
#include <stdexcept> // For std::stoi exceptions

#pragma comment(lib, "comctl32.lib")

// Control IDs
constexpr int ID_MENU_OPEN = 201;
constexpr int ID_LIST_EVENTS = 101;
constexpr int ID_COMBO_FILTER = 202;

// EventMap Entry Controls
constexpr int IDC_EDIT_ID = 301;
constexpr int IDC_EDIT_BUNDLE = 302;
constexpr int IDC_EDIT_SIMPLE = 303;
constexpr int IDC_EDIT_NAME = 304;
constexpr int IDC_COMBO_SEC = 305;
constexpr int IDC_BTN_ADD = 306;
constexpr int IDC_BTN_SAVE = 307;

// SoundDataFile & SimpleEvent Entry Controls
constexpr int IDC_EDIT_SDF_ID = 401;
constexpr int IDC_EDIT_SDF_TYPE_CHAR = 402;
constexpr int IDC_EDIT_SDF_FILENAME = 403;
constexpr int IDC_EDIT_SE_SOUNDFILE_ID = 404; // For SimpleEvent's 2nd field
constexpr int IDC_EDIT_SE_TYPE_VAL = 405;    // For SimpleEvent's 3rd field
constexpr int IDC_EDIT_SE_BUNDLE_IDX = 406;  // For SimpleEvent's 4th field
constexpr int IDC_BTN_ADD_SOUND_SIMPLE = 407;

// Static labels for input fields (will use their HWNDs directly if needed for moving)
HWND      g_hLblEventMapSectionTitle;
HWND      g_hLblEvId, g_hLblEvBundle, g_hLblEvSimple, g_hLblEvName, g_hLblEvSection;
HWND      g_hLblSdfSectionTitle;
HWND      g_hLblSdfId, g_hLblSdfType, g_hLblSdfFilename;
HWND      g_hLblSeSfId, g_hLblSeType, g_hLblSeBundle;


// Event structure (for EventMaps)
struct SoundEventEntry {
    int event_id;
    int bundle_index;
    int simple_event; // This ID should match a SoundDataFileEntry.id and a SimpleEvent.id
    std::wstring event_name;
    std::wstring section; // "Pre" or "Pos"
    std::wstring sound_data_filename;
    bool is_newly_added = false; // To help SaveFlo identify new EventMap entries
};

// Sound Data File structure
struct SoundDataFileEntry {
    int id;
    std::wstring type_char; // 'c', 'b', etc.
    std::wstring filename;
    bool is_newly_added = false; // To help SaveFlo identify new entries
};

// Simple Event parameters (for newly added ones)
struct NewSimpleEventParams {
    int id;             // This is the Simple Event ID, same as the new SoundDataFile ID
    int sound_file_id;  // The 2nd field in the SimpleEvents line (often same as id)
    int type_val;       // The 3rd field (e.g., 2)
    int bundle_idx;     // The 4th field (e.g., 0, 1)
    // No need for is_newly_added here, g_newlyAddedSimpleEventParams itself implies new
};


// Globals
HINSTANCE g_hInst;
HWND      g_hMainWnd;
HWND      g_hListEvents;
HWND      g_hComboFilter;
HWND      g_hLabelPre;
HWND      g_hLabelPos;

// EventMap Input Globals
HWND      g_hEditID;
HWND      g_hEditBundle;
HWND      g_hEditSimple;
HWND      g_hEditName;
HWND      g_hComboSection;
HWND      g_hBtnAdd;
HWND      g_hBtnSave;

// SoundDataFile & SimpleEvent Input Globals
HWND      g_hEditSdfId;
HWND      g_hEditSdfTypeChar;
HWND      g_hEditSdfFilename;
HWND      g_hEditSeSoundFileId;
HWND      g_hEditSeTypeVal;
HWND      g_hEditSeBundleIdx;
HWND      g_hBtnAddSoundSimple;


std::vector<SoundEventEntry> g_events;
std::vector<SoundDataFileEntry> g_soundDataFiles;
std::vector<NewSimpleEventParams> g_newlyAddedSimpleEventParams;

std::wstring g_filterName;
// Counts displayed and used for identifying *new* entries for saving
int g_preTotalEventMapEntries = 0;
int g_posTotalEventMapEntries = 0;
int g_loadedPreEventMapCount = 0;
int g_loadedPosEventMapCount = 0;

int g_loadedSoundDataFileCount = 0;
int g_loadedSimpleEventCount = 0;

std::wstring g_loadedPath;

// Sorting globals
int g_sortColumn = -1;
bool g_sortAscending = true;


// Trim whitespace (wstring)
static std::wstring Trim(const std::wstring& s) {
    const wchar_t* ws = L" \t\r\n";
    auto start = s.find_first_not_of(ws);
    if (start == std::wstring::npos) return L"";
    auto end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

// Trim whitespace (string)
static std::string TrimA(const std::string& s) {
    const char* ws = " \t\r\n";
    auto start = s.find_first_not_of(ws);
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

// Parse .flo
static bool ParseFloFile(const std::wstring& path) {
    std::wifstream in(path);
    if (!in) return false;
    std::vector<std::wstring> lines;
    for (std::wstring line; std::getline(in, line); ) lines.push_back(line);

    g_events.clear();
    g_soundDataFiles.clear();
    g_newlyAddedSimpleEventParams.clear();

    g_preTotalEventMapEntries = g_posTotalEventMapEntries = 0;
    g_loadedPreEventMapCount = g_loadedPosEventMapCount = 0;
    g_loadedSoundDataFileCount = 0;
    g_loadedSimpleEventCount = 0;

    // Parse SoundDataFiles section
    int idxSDF = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (Trim(lines[i]) == L"SoundDataFiles") { idxSDF = i; break; }
    }
    if (idxSDF >= 0 && idxSDF + 1 < (int)lines.size()) {
        std::wstring countLineSDF = Trim(lines[idxSDF + 1]);
        if (!countLineSDF.empty()) {
            try {
                g_loadedSoundDataFileCount = _wtoi(countLineSDF.c_str());
                if (g_loadedSoundDataFileCount < 0) g_loadedSoundDataFileCount = 0;
                for (int j = 0; j < g_loadedSoundDataFileCount; ++j) {
                    int li = idxSDF + 2 + j;
                    if (li >= (int)lines.size()) break;
                    std::wstringstream ss(lines[li]);
                    SoundDataFileEntry sdf_entry; std::wstring tok;
                    std::getline(ss, tok, L','); sdf_entry.id = _wtoi(Trim(tok).c_str());
                    std::getline(ss, tok, L','); sdf_entry.type_char = Trim(tok);
                    std::getline(ss, tok);      sdf_entry.filename = Trim(tok);
                    sdf_entry.is_newly_added = false;
                    g_soundDataFiles.push_back(sdf_entry);
                }
            }
            catch (const std::exception&) { g_loadedSoundDataFileCount = 0; }
        }
    }

    // Parse SimpleEvents count
    int idxSE = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (Trim(lines[i]) == L"SimpleEvents") { idxSE = i; break; }
    }
    if (idxSE >= 0 && idxSE + 1 < (int)lines.size()) {
        std::wstring countLineSE = Trim(lines[idxSE + 1]);
        if (!countLineSE.empty()) {
            try {
                g_loadedSimpleEventCount = _wtoi(countLineSE.c_str());
                if (g_loadedSimpleEventCount < 0) g_loadedSimpleEventCount = 0;
            }
            catch (const std::exception&) { g_loadedSimpleEventCount = 0; }
        }
    }

    // Locate EventMaps
    int idxMaps = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (Trim(lines[i]) == L"EventMaps") { idxMaps = i; break; }
    }
    if (idxMaps < 0) return false;

    auto parseEventMapSection = [&](const std::wstring& name, int& totalCountVariable, int& loadedCountVariable) {
        int idx = -1;
        for (int i = idxMaps + 1; i < (int)lines.size(); ++i) {
            if (Trim(lines[i]) == name) { idx = i; break; }
        }
        if (idx < 0 || idx + 1 >= (int)lines.size()) return;

        std::wstring countLineSection = Trim(lines[idx + 1]);
        if (countLineSection.empty()) return;
        try {
            loadedCountVariable = _wtoi(countLineSection.c_str());
            if (loadedCountVariable < 0) loadedCountVariable = 0;
            totalCountVariable = loadedCountVariable; // Initialize total with loaded

            for (int j = 0; j < loadedCountVariable; ++j) {
                int li = idx + 2 + j;
                if (li >= (int)lines.size()) break;
                std::wstringstream ss(lines[li]);
                SoundEventEntry e; std::wstring tok;
                std::getline(ss, tok, L','); e.event_id = _wtoi(Trim(tok).c_str());
                std::getline(ss, tok, L','); e.bundle_index = _wtoi(Trim(tok).c_str());
                std::getline(ss, tok, L','); e.simple_event = _wtoi(Trim(tok).c_str());
                std::getline(ss, tok); e.event_name = Trim(tok);
                e.section = name;
                e.is_newly_added = false;

                e.sound_data_filename = L"N/A";
                for (const auto& sdf : g_soundDataFiles) {
                    if (sdf.id == e.simple_event) {
                        e.sound_data_filename = sdf.filename;
                        break;
                    }
                }
                g_events.push_back(e);
            }
        }
        catch (const std::exception&) { /* ignore parse error for count */ }
        };
    parseEventMapSection(L"Pre", g_preTotalEventMapEntries, g_loadedPreEventMapCount);
    parseEventMapSection(L"Pos", g_posTotalEventMapEntries, g_loadedPosEventMapCount);

    std::stable_sort(g_events.begin(), g_events.end(), [](const SoundEventEntry& a, const SoundEventEntry& b) {
        if (a.section == L"Pre" && b.section == L"Pos") return true;
        if (a.section == L"Pos" && b.section == L"Pre") return false;
        return a.event_id < b.event_id;
        });

    return true;
}


// Sort g_events based on g_sortColumn and g_sortAscending
static void SortEvents() {
    if (g_sortColumn < 0) return; // No column selected for sorting

    std::sort(g_events.begin(), g_events.end(), [](const SoundEventEntry& a, const SoundEventEntry& b) {
        int comparisonResult = 0;
        // Ensure "Pre" always comes before "Pos" as a primary sort key if not sorting by section
        if (g_sortColumn != 5) { // 5 is the "Section" column
            if (a.section == L"Pre" && b.section == L"Pos") return g_sortAscending ? true : true; // Pre always first
            if (a.section == L"Pos" && b.section == L"Pre") return g_sortAscending ? false : false; // Pos always last
        }

        switch (g_sortColumn) {
        case 0: // Event ID
            if (a.event_id < b.event_id) comparisonResult = -1;
            else if (a.event_id > b.event_id) comparisonResult = 1;
            break;
        case 1: // Bundle
            if (a.bundle_index < b.bundle_index) comparisonResult = -1;
            else if (a.bundle_index > b.bundle_index) comparisonResult = 1;
            break;
        case 2: // Simple ID
            if (a.simple_event < b.simple_event) comparisonResult = -1;
            else if (a.simple_event > b.simple_event) comparisonResult = 1;
            break;
        case 3: // Event Name
            comparisonResult = _wcsicmp(a.event_name.c_str(), b.event_name.c_str()); // Case-insensitive
            break;
        case 4: // Sound File
            comparisonResult = _wcsicmp(a.sound_data_filename.c_str(), b.sound_data_filename.c_str());
            break;
        case 5: // Section
            comparisonResult = a.section.compare(b.section); // Pre vs Pos
            if (comparisonResult == 0) { // If same section, then by event_id
                if (a.event_id < b.event_id) comparisonResult = -1;
                else if (a.event_id > b.event_id) comparisonResult = 1;
            }
            break;
        }
        return g_sortAscending ? (comparisonResult < 0) : (comparisonResult > 0);
        });
}


// Populate ListView
static void PopulateList() {
    ListView_DeleteAllItems(g_hListEvents);
    LVITEMW item{}; item.mask = LVIF_TEXT;
    int row = 0;
    for (auto& e : g_events) {
        if (!g_filterName.empty() && g_filterName != L"All") {
            auto pos = e.event_name.find(L'*');
            std::wstring sub = (pos != std::wstring::npos) ? e.event_name.substr(pos + 1) : e.event_name;
            auto us = sub.find(L'_');
            std::wstring grp = (us != std::wstring::npos) ? sub.substr(0, us) : sub;
            if (grp != g_filterName) continue;
        }
        item.iItem = row;
        wchar_t buf[256];

        wsprintfW(buf, L"%d", e.event_id);
        item.pszText = buf;
        ListView_InsertItem(g_hListEvents, &item);

        wsprintfW(buf, L"%d", e.bundle_index);
        ListView_SetItemText(g_hListEvents, row, 1, buf);

        wsprintfW(buf, L"%d", e.simple_event);
        ListView_SetItemText(g_hListEvents, row, 2, buf);

        ListView_SetItemText(g_hListEvents, row, 3, (LPWSTR)e.event_name.c_str());
        ListView_SetItemText(g_hListEvents, row, 4, (LPWSTR)e.sound_data_filename.c_str());
        ListView_SetItemText(g_hListEvents, row, 5, (LPWSTR)e.section.c_str());

        row++;
    }
}

// Open and Load
static void OpenAndLoadFlo(HWND hwnd) {
    OPENFILENAMEW ofn{ sizeof(ofn) };
    wchar_t file[MAX_PATH] = {};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"FLO Files (*.flo)\0*.flo\0All Files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn) && ParseFloFile(file)) {
        g_loadedPath = file;
        SetWindowTextW(g_hMainWnd, (L"FLO Event Editor - " + g_loadedPath).c_str());
        std::set<std::wstring> names{ L"All" };
        for (auto& e : g_events) {
            auto pos = e.event_name.find(L'*');
            std::wstring sub = (pos != std::wstring::npos) ? e.event_name.substr(pos + 1) : e.event_name;
            auto us = sub.find(L'_');
            names.insert((us != std::wstring::npos) ? sub.substr(0, us) : sub);
        }
        SendMessageW(g_hComboFilter, CB_RESETCONTENT, 0, 0);
        for (auto& n : names)
            SendMessageW(g_hComboFilter, CB_ADDSTRING, 0, (LPARAM)n.c_str());
        SendMessageW(g_hComboFilter, CB_SETCURSEL, 0, 0);
        g_filterName = L"All";

        g_sortColumn = -1;
        SortEvents(); // Apply default sort (Pre then Pos, then by ID)
        PopulateList();

        wchar_t buf[64];
        wsprintfW(buf, L"Pre: %d", g_preTotalEventMapEntries); SetWindowTextW(g_hLabelPre, buf);
        wsprintfW(buf, L"Pos: %d", g_posTotalEventMapEntries); SetWindowTextW(g_hLabelPos, buf);
    }
    else if (wcslen(file) > 0) {
        MessageBoxW(hwnd, L"Failed to parse the selected .flo file.", L"Parse Error", MB_OK | MB_ICONERROR);
    }
}

// Add Event (to EventMaps)
static void AddEvent() {
    SoundEventEntry e;
    wchar_t num_buf[64];
    wchar_t name_buf[256];

    GetWindowTextW(g_hEditID, num_buf, 64); e.event_id = _wtoi(num_buf);
    GetWindowTextW(g_hEditBundle, num_buf, 64); e.bundle_index = _wtoi(num_buf);
    GetWindowTextW(g_hEditSimple, num_buf, 64); e.simple_event = _wtoi(num_buf);
    GetWindowTextW(g_hEditName, name_buf, 256); e.event_name = Trim(name_buf);

    int sel = SendMessageW(g_hComboSection, CB_GETCURSEL, 0, 0);
    SendMessageW(g_hComboSection, CB_GETLBTEXT, sel, (LPARAM)num_buf);
    e.section = Trim(num_buf);
    e.is_newly_added = true;

    e.sound_data_filename = L"N/A";
    for (const auto& sdf : g_soundDataFiles) {
        if (sdf.id == e.simple_event) {
            e.sound_data_filename = sdf.filename;
            break;
        }
    }

    auto it_insert = g_events.end();
    if (e.section == L"Pre") {
        it_insert = std::find_if(g_events.begin(), g_events.end(), [](const SoundEventEntry& ev) {
            return ev.section == L"Pos";
            });
        g_preTotalEventMapEntries++;
    }
    else {
        g_posTotalEventMapEntries++;
    }
    g_events.insert(it_insert, e);

    SortEvents();
    PopulateList();

    wsprintfW(num_buf, L"Pre: %d", g_preTotalEventMapEntries); SetWindowTextW(g_hLabelPre, num_buf);
    wsprintfW(num_buf, L"Pos: %d", g_posTotalEventMapEntries); SetWindowTextW(g_hLabelPos, num_buf);
}

// Add SoundDataFile and corresponding SimpleEvent entry parameters
static void AddSoundAndSimpleEvent() {
    wchar_t buf[256];
    SoundDataFileEntry sdf_e;
    NewSimpleEventParams se_p;

    GetWindowTextW(g_hEditSdfId, buf, 64); sdf_e.id = _wtoi(buf);
    GetWindowTextW(g_hEditSdfTypeChar, buf, 64); sdf_e.type_char = Trim(buf);
    GetWindowTextW(g_hEditSdfFilename, buf, 256); sdf_e.filename = Trim(buf);
    sdf_e.is_newly_added = true;

    se_p.id = sdf_e.id;
    GetWindowTextW(g_hEditSeSoundFileId, buf, 64); se_p.sound_file_id = _wtoi(buf);
    GetWindowTextW(g_hEditSeTypeVal, buf, 64); se_p.type_val = _wtoi(buf);
    GetWindowTextW(g_hEditSeBundleIdx, buf, 64); se_p.bundle_idx = _wtoi(buf);

    if (sdf_e.type_char.empty() || sdf_e.filename.empty()) {
        MessageBoxW(g_hMainWnd, L"SDF Type and Filename cannot be empty.", L"Input Error", MB_OK | MB_ICONWARNING);
        return;
    }
    for (const auto& existing_sdf : g_soundDataFiles) {
        if (existing_sdf.id == sdf_e.id) {
            MessageBoxW(g_hMainWnd, (L"SoundDataFile ID " + std::to_wstring(sdf_e.id) + L" already exists.").c_str(), L"Input Error", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    g_soundDataFiles.push_back(sdf_e);
    g_newlyAddedSimpleEventParams.push_back(se_p);

    std::wstring msg = L"Added SoundDataFile ID: " + std::to_wstring(sdf_e.id) +
        L" (" + sdf_e.filename + L") and its SimpleEvent mapping.\n" +
        L"Remember to Save .flo to persist these changes.";
    MessageBoxW(g_hMainWnd, msg.c_str(), L"Entry Added", MB_OK | MB_ICONINFORMATION);

    SetWindowTextW(g_hEditSdfId, L"");
    SetWindowTextW(g_hEditSdfTypeChar, L"");
    SetWindowTextW(g_hEditSdfFilename, L"");
    SetWindowTextW(g_hEditSeSoundFileId, L"");
}


// --- Patch helper: find nth marker ---
size_t FindNthMarker(const std::vector<char>& data, const char* marker, int n) {
    size_t pos = 0;
    int found = 0;
    size_t marker_len = strlen(marker);
    if (marker_len == 0) return std::string::npos;

    while (pos < data.size()) {
        auto it = std::search(data.begin() + pos, data.end(), marker, marker + marker_len);
        if (it == data.end()) return std::string::npos;
        pos = it - data.begin();
        found++;
        if (found == n) return pos;
        pos += 1;
    }
    return std::string::npos;
}

// --- Patch and insert line (updates count) ---
bool PatchInsertLineInSection(std::vector<char>& data,
    const char* section_marker, // e.g. "SoundDataFiles\r\n"
    int which_occurrence,
    const std::string& new_line_to_insert) // new_line_to_insert should end with \r\n
{
    size_t section_marker_len = strlen(section_marker);
    size_t section_pos = FindNthMarker(data, section_marker, which_occurrence);
    if (section_pos == std::string::npos) return false;

    size_t count_val_pos = section_pos + section_marker_len;
    const char rn[] = "\r\n";
    auto count_line_end_it = std::search(data.begin() + count_val_pos, data.end(), rn, rn + 2);
    if (count_line_end_it == data.end()) return false;

    size_t count_line_content_end = count_line_end_it - data.begin();
    size_t count_line_full_end = count_line_content_end + 2;

    std::string count_line_content_str(data.begin() + count_val_pos, data.begin() + count_line_content_end);
    int current_item_count = 0;
    try {
        std::string trimmed_count_str = TrimA(count_line_content_str);
        if (!trimmed_count_str.empty()) {
            current_item_count = std::stoi(trimmed_count_str);
        }
    }
    catch (const std::exception&) { return false; }

    if (current_item_count < 0) current_item_count = 0;

    size_t insert_pos = count_line_full_end;
    for (int i = 0; i < current_item_count; ++i) {
        auto next_item_line_end_it = std::search(data.begin() + insert_pos, data.end(), rn, rn + 2);
        if (next_item_line_end_it == data.end()) {
            insert_pos = data.size();
            if (data.size() >= 2 && (data[data.size() - 2] != '\r' || data[data.size() - 1] != '\n')) {
                data.insert(data.end(), rn, rn + 2); // Ensure CRLF before new line
                // insert_pos remains data.size() before this insertion, new_line_to_insert will go after this CRLF
            }
            else if (data.empty() || data.size() < 2) {
                // data.insert(data.end(), rn, rn+2); // This was problematic, should not add CRLF if it's the first line after count
            }
            break;
        }
        insert_pos = (next_item_line_end_it - data.begin()) + 2;
    }

    data.insert(data.begin() + insert_pos, new_line_to_insert.begin(), new_line_to_insert.end());

    current_item_count++;
    std::string new_count_value_str = std::to_string(current_item_count);

    std::string new_full_count_line_str = new_count_value_str;
    size_t original_content_len = count_line_content_str.length();

    if (new_full_count_line_str.length() < original_content_len) {
        std::string padding(original_content_len - new_full_count_line_str.length(), ' ');
        if (!count_line_content_str.empty() && count_line_content_str[0] == ' ' && count_line_content_str.back() != ' ' && new_full_count_line_str.find_first_not_of(' ') != std::string::npos) {
            new_full_count_line_str = padding + new_full_count_line_str; // Try to preserve right-alignment
        }
        else {
            new_full_count_line_str += padding; // Left-align and pad
        }
    }
    else if (new_full_count_line_str.length() > original_content_len) {
        new_full_count_line_str.resize(original_content_len);
    }
    if (data.begin() + count_val_pos + new_full_count_line_str.length() <= data.end()) {
        std::copy(new_full_count_line_str.begin(), new_full_count_line_str.end(), data.begin() + count_val_pos);
    }
    else {
        return false; // Not enough space to write new count (should not happen if original_content_len is respected)
    }

    return true;
}


// Format EventMap line
std::string SerializeEventMapLine(int id, int bundle, int simple, const std::wstring& nameW) {
    std::string name_utf8;
    if (!nameW.empty()) {
        int len = WideCharToMultiByte(CP_ACP, 0, nameW.c_str(), -1, NULL, 0, NULL, NULL);
        if (len > 0) {
            name_utf8.resize(len - 1);
            WideCharToMultiByte(CP_ACP, 0, nameW.c_str(), -1, &name_utf8[0], len, NULL, NULL);
        }
    }
    char buf[512];
    snprintf(buf, sizeof(buf), "%d,\t%d, %d, %s\r\n", id, bundle, simple, name_utf8.c_str());
    return std::string(buf);
}

// Format SoundDataFile line
std::string SerializeSoundDataFileLine(int id, const std::wstring& type_charW, const std::wstring& filenameW) {
    std::string type_char_utf8, filename_utf8;
    if (!type_charW.empty()) {
        int len = WideCharToMultiByte(CP_ACP, 0, type_charW.c_str(), -1, NULL, 0, NULL, NULL);
        if (len > 0) { type_char_utf8.resize(len - 1); WideCharToMultiByte(CP_ACP, 0, type_charW.c_str(), -1, &type_char_utf8[0], len, NULL, NULL); }
    }
    if (!filenameW.empty()) {
        int len = WideCharToMultiByte(CP_ACP, 0, filenameW.c_str(), -1, NULL, 0, NULL, NULL);
        if (len > 0) { filename_utf8.resize(len - 1); WideCharToMultiByte(CP_ACP, 0, filenameW.c_str(), -1, &filename_utf8[0], len, NULL, NULL); }
    }
    char buf[512];
    snprintf(buf, sizeof(buf), "%d,\t%s,\t%s\r\n", id, type_char_utf8.c_str(), filename_utf8.c_str());
    return std::string(buf);
}

// Format SimpleEvent line
std::string SerializeSimpleEventLine(int id, int sound_file_id, int type_val, int bundle_idx) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%d,\t%d, %d, %d\r\n", id, sound_file_id, type_val, bundle_idx);
    return std::string(buf);
}


// Save
static void SaveFlo(HWND hwnd) {
    if (g_loadedPath.empty()) {
        MessageBoxW(hwnd, L"No file loaded to base the save on. Please open a file first.", L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }

    OPENFILENAMEW ofn{ sizeof(ofn) };
    wchar_t outFile[MAX_PATH];
    wcsncpy_s(outFile, MAX_PATH, g_loadedPath.c_str(), _TRUNCATE);

    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = outFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"FLO Files (*.flo)\0*.flo\0All Files\0*.*\0";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    std::ifstream inFileStream(g_loadedPath, std::ios::binary);
    if (!inFileStream) {
        MessageBoxW(hwnd, (L"Failed to open original file for reading: " + g_loadedPath).c_str(), L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }
    std::vector<char> data((std::istreambuf_iterator<char>(inFileStream)), std::istreambuf_iterator<char>());
    inFileStream.close();

    const int sdf_occurrence = 1;
    const int se_occurrence = 1;
    const int pre_eventmap_occurrence = 2;
    const int pos_eventmap_occurrence = 2;

    // 1. Patch SoundDataFiles
    for (const auto& sdf_entry : g_soundDataFiles) {
        if (sdf_entry.is_newly_added) {
            std::string new_sdf_line = SerializeSoundDataFileLine(sdf_entry.id, sdf_entry.type_char, sdf_entry.filename);
            if (!PatchInsertLineInSection(data, "SoundDataFiles\r\n", sdf_occurrence, new_sdf_line)) {
                MessageBoxW(hwnd, L"Failed to patch 'SoundDataFiles' section. Save aborted.", L"Save Error", MB_OK | MB_ICONERROR);
                return;
            }
        }
    }

    // 2. Patch SimpleEvents
    for (const auto& se_params : g_newlyAddedSimpleEventParams) {
        std::string new_se_line = SerializeSimpleEventLine(se_params.id, se_params.sound_file_id, se_params.type_val, se_params.bundle_idx);
        if (!PatchInsertLineInSection(data, "SimpleEvents\r\n", se_occurrence, new_se_line)) {
            MessageBoxW(hwnd, L"Failed to patch 'SimpleEvents' section. Save aborted.", L"Save Error", MB_OK | MB_ICONERROR);
            return;
        }
    }

    // 3. Patch EventMaps (Pre/Pos)
    for (const auto& event_entry : g_events) {
        if (event_entry.is_newly_added) {
            std::string new_em_line = SerializeEventMapLine(event_entry.event_id, event_entry.bundle_index, event_entry.simple_event, event_entry.event_name);
            const char* section_marker = (event_entry.section == L"Pre") ? "Pre\r\n" : "Pos\r\n";
            int occurrence = (event_entry.section == L"Pre") ? pre_eventmap_occurrence : pos_eventmap_occurrence;
            if (!PatchInsertLineInSection(data, section_marker, occurrence, new_em_line)) {
                MessageBoxW(hwnd, (L"Failed to patch '" + event_entry.section + L"' EventMap section. Save aborted.").c_str(), L"Save Error", MB_OK | MB_ICONERROR);
                return;
            }
        }
    }

    std::ofstream outF(outFile, std::ios::binary | std::ios::trunc);
    if (!outF) {
        MessageBoxW(hwnd, (L"Failed to open output file for writing: " + std::wstring(outFile)).c_str(), L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }
    outF.write(data.data(), data.size());
    outF.close();

    g_loadedPath = outFile;
    SetWindowTextW(g_hMainWnd, (L"FLO Event Editor - " + g_loadedPath).c_str());

    for (auto& sdf : g_soundDataFiles) sdf.is_newly_added = false;
    for (auto& em : g_events) em.is_newly_added = false;
    g_newlyAddedSimpleEventParams.clear();

    g_loadedSoundDataFileCount = g_soundDataFiles.size();
    g_loadedSimpleEventCount += g_newlyAddedSimpleEventParams.size(); // This should be done by patch function
    g_loadedPreEventMapCount = g_preTotalEventMapEntries;
    g_loadedPosEventMapCount = g_posTotalEventMapEntries;

    MessageBoxW(hwnd, L"File saved successfully.", L"Save Success", MB_OK | MB_ICONINFORMATION);
}

void CreateInputControls(HWND hwnd, const RECT& rc_parent) {
    // Define layout constants relative to parent width/height
    const int margin = 10;
    const int input_h = 20;
    const int spacing = 5;
    const int button_h = 30;
    const int button_w = 120;

    int current_y = rc_parent.bottom - 270; // Initial Y for input area from bottom

    // --- EventMap Entry Inputs ---
    int x_pos = margin;
    int label_w = 70; // Increased label width
    int edit_w_short = 70;
    int edit_w_name = 220; // Wider name field
    int section_combo_w = 100;

    g_hLblEventMapSectionTitle = CreateWindowW(L"STATIC", L"Add Event Map Entry:", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, x_pos, current_y, 200, input_h, hwnd, NULL, g_hInst, NULL);
    current_y += input_h + spacing;

    g_hLblEvId = CreateWindowW(L"STATIC", L"Ev. ID:", WS_CHILD | WS_VISIBLE | SS_RIGHT, x_pos, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditID = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP, x_pos + label_w + spacing, current_y, edit_w_short, input_h, hwnd, (HMENU)IDC_EDIT_ID, g_hInst, NULL);

    int next_x = x_pos + label_w + spacing + edit_w_short + margin;
    g_hLblEvBundle = CreateWindowW(L"STATIC", L"Bundle:", WS_CHILD | WS_VISIBLE | SS_RIGHT, next_x, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditBundle = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP, next_x + label_w + spacing, current_y, edit_w_short, input_h, hwnd, (HMENU)IDC_EDIT_BUNDLE, g_hInst, NULL);

    next_x += label_w + spacing + edit_w_short + margin;
    g_hLblEvSimple = CreateWindowW(L"STATIC", L"SimpleID:", WS_CHILD | WS_VISIBLE | SS_RIGHT, next_x, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditSimple = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP, next_x + label_w + spacing, current_y, edit_w_short, input_h, hwnd, (HMENU)IDC_EDIT_SIMPLE, g_hInst, NULL);
    current_y += input_h + spacing;

    g_hLblEvName = CreateWindowW(L"STATIC", L"Name:", WS_CHILD | WS_VISIBLE | SS_RIGHT, x_pos, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditName = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, x_pos + label_w + spacing, current_y, edit_w_name, input_h, hwnd, (HMENU)IDC_EDIT_NAME, g_hInst, NULL);

    next_x = x_pos + label_w + spacing + edit_w_name + margin;
    g_hLblEvSection = CreateWindowW(L"STATIC", L"Section:", WS_CHILD | WS_VISIBLE | SS_RIGHT, next_x, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hComboSection = CreateWindowExW(0, WC_COMBOBOXW, NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        next_x + label_w + spacing, current_y, section_combo_w, 100, hwnd, (HMENU)IDC_COMBO_SEC, g_hInst, NULL);
    SendMessageW(g_hComboSection, CB_ADDSTRING, 0, (LPARAM)L"Pre");
    SendMessageW(g_hComboSection, CB_ADDSTRING, 0, (LPARAM)L"Pos");
    SendMessageW(g_hComboSection, CB_SETCURSEL, 0, 0);

    g_hBtnAdd = CreateWindowW(L"BUTTON", L"Add EventMap", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc_parent.right - margin - button_w, current_y - input_h / 2, button_w, button_h, hwnd, (HMENU)IDC_BTN_ADD, g_hInst, NULL);
    current_y += input_h + spacing * 2;

    // --- SoundDataFile & SimpleEvent Inputs ---
    g_hLblSdfSectionTitle = CreateWindowW(L"STATIC", L"Add SoundDataFile & SimpleEvent:", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, x_pos, current_y, 300, input_h, hwnd, NULL, g_hInst, NULL);
    current_y += input_h + spacing;

    g_hLblSdfId = CreateWindowW(L"STATIC", L"SDF ID:", WS_CHILD | WS_VISIBLE | SS_RIGHT, x_pos, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditSdfId = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP, x_pos + label_w + spacing, current_y, edit_w_short, input_h, hwnd, (HMENU)IDC_EDIT_SDF_ID, g_hInst, NULL);

    next_x = x_pos + label_w + spacing + edit_w_short + margin;
    g_hLblSdfType = CreateWindowW(L"STATIC", L"SDF Type:", WS_CHILD | WS_VISIBLE | SS_RIGHT, next_x, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditSdfTypeChar = CreateWindowW(L"EDIT", L"c", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, next_x + label_w + spacing, current_y, edit_w_short, input_h, hwnd, (HMENU)IDC_EDIT_SDF_TYPE_CHAR, g_hInst, NULL);
    current_y += input_h + spacing;

    g_hLblSdfFilename = CreateWindowW(L"STATIC", L"SDF File:", WS_CHILD | WS_VISIBLE | SS_RIGHT, x_pos, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditSdfFilename = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, x_pos + label_w + spacing, current_y, edit_w_name, input_h, hwnd, (HMENU)IDC_EDIT_SDF_FILENAME, g_hInst, NULL);
    current_y += input_h + spacing;

    g_hLblSeSfId = CreateWindowW(L"STATIC", L"SE SF_ID:", WS_CHILD | WS_VISIBLE | SS_RIGHT, x_pos, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditSeSoundFileId = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP, x_pos + label_w + spacing, current_y, edit_w_short, input_h, hwnd, (HMENU)IDC_EDIT_SE_SOUNDFILE_ID, g_hInst, NULL);

    next_x = x_pos + label_w + spacing + edit_w_short + margin;
    g_hLblSeType = CreateWindowW(L"STATIC", L"SE Type:", WS_CHILD | WS_VISIBLE | SS_RIGHT, next_x, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditSeTypeVal = CreateWindowW(L"EDIT", L"2", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP, next_x + label_w + spacing, current_y, edit_w_short, input_h, hwnd, (HMENU)IDC_EDIT_SE_TYPE_VAL, g_hInst, NULL);

    next_x += label_w + spacing + edit_w_short + margin;
    g_hLblSeBundle = CreateWindowW(L"STATIC", L"SE Bundle:", WS_CHILD | WS_VISIBLE | SS_RIGHT, next_x, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditSeBundleIdx = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP, next_x + label_w + spacing, current_y, edit_w_short, input_h, hwnd, (HMENU)IDC_EDIT_SE_BUNDLE_IDX, g_hInst, NULL);

    g_hBtnAddSoundSimple = CreateWindowW(L"BUTTON", L"Add SDF/SE", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc_parent.right - margin - button_w, current_y - input_h / 2, button_w, button_h, hwnd, (HMENU)IDC_BTN_ADD_SOUND_SIMPLE, g_hInst, NULL);

    g_hBtnSave = CreateWindowW(L"BUTTON", L"Save .flo", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc_parent.right - margin - button_w, rc_parent.bottom - margin - button_h, button_w, button_h, hwnd, (HMENU)IDC_BTN_SAVE, g_hInst, NULL);
}

void RepositionInputControls(HWND hwnd, const RECT& rc_parent) {
    const int margin = 10;
    const int input_h = 20;
    const int spacing = 5;
    const int button_h = 30;
    const int button_w = 120;

    int current_y = rc_parent.bottom - 270; // Initial Y for input area from bottom

    // --- EventMap Entry Inputs ---
    int x_pos = margin;
    int label_w = 70;
    int edit_w_short = 70;
    int section_combo_w = 100; // Example width, adjust as needed

    int edit_w_name = (std::max)(static_cast<int>(150), static_cast<int>((rc_parent.right - (x_pos + label_w + spacing + margin + label_w + spacing + section_combo_w + margin + button_w + margin)) / 1)); // Dynamic width for name
    if (edit_w_name < 150) edit_w_name = 150; // Minimum width
    

    MoveWindow(g_hLblEventMapSectionTitle, x_pos, current_y, 200, input_h, TRUE);
    current_y += input_h + spacing;

    MoveWindow(g_hLblEvId, x_pos, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditID, x_pos + label_w + spacing, current_y, edit_w_short, input_h, TRUE);

    int next_x = x_pos + label_w + spacing + edit_w_short + margin;
    MoveWindow(g_hLblEvBundle, next_x, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditBundle, next_x + label_w + spacing, current_y, edit_w_short, input_h, TRUE);

    next_x += label_w + spacing + edit_w_short + margin;
    MoveWindow(g_hLblEvSimple, next_x, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditSimple, next_x + label_w + spacing, current_y, edit_w_short, input_h, TRUE);
    current_y += input_h + spacing;

    MoveWindow(g_hLblEvName, x_pos, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditName, x_pos + label_w + spacing, current_y, edit_w_name, input_h, TRUE);

    next_x = x_pos + label_w + spacing + edit_w_name + margin;
    MoveWindow(g_hLblEvSection, next_x, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hComboSection, next_x + label_w + spacing, current_y, section_combo_w, 100, TRUE);

    MoveWindow(g_hBtnAdd, rc_parent.right - margin - button_w, current_y - input_h / 2, button_w, button_h, TRUE);
    current_y += input_h + spacing * 2;

    // --- SoundDataFile & SimpleEvent Inputs ---
    MoveWindow(g_hLblSdfSectionTitle, x_pos, current_y, 300, input_h, TRUE);
    current_y += input_h + spacing;

    MoveWindow(g_hLblSdfId, x_pos, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditSdfId, x_pos + label_w + spacing, current_y, edit_w_short, input_h, TRUE);

    next_x = x_pos + label_w + spacing + edit_w_short + margin;
    MoveWindow(g_hLblSdfType, next_x, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditSdfTypeChar, next_x + label_w + spacing, current_y, edit_w_short, input_h, TRUE);
    current_y += input_h + spacing;

    MoveWindow(g_hLblSdfFilename, x_pos, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditSdfFilename, x_pos + label_w + spacing, current_y, edit_w_name, input_h, TRUE); // Use adjusted edit_w_name
    current_y += input_h + spacing;

    MoveWindow(g_hLblSeSfId, x_pos, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditSeSoundFileId, x_pos + label_w + spacing, current_y, edit_w_short, input_h, TRUE);

    next_x = x_pos + label_w + spacing + edit_w_short + margin;
    MoveWindow(g_hLblSeType, next_x, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditSeTypeVal, next_x + label_w + spacing, current_y, edit_w_short, input_h, TRUE);

    next_x += label_w + spacing + edit_w_short + margin;
    MoveWindow(g_hLblSeBundle, next_x, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditSeBundleIdx, next_x + label_w + spacing, current_y, edit_w_short, input_h, TRUE);

    MoveWindow(g_hBtnAddSoundSimple, rc_parent.right - margin - button_w, current_y - input_h / 2, button_w, button_h, TRUE);

    MoveWindow(g_hBtnSave, rc_parent.right - margin - button_w, rc_parent.bottom - margin - button_h, button_w, button_h, TRUE);
}


// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hMainWnd = hwnd;
        INITCOMMONCONTROLSEX ic{ sizeof(ic), ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&ic);
        RECT rc; GetClientRect(hwnd, &rc);

        HMENU mb = CreateMenu(), mf = CreatePopupMenu();
        AppendMenuW(mf, MF_STRING, ID_MENU_OPEN, L"Open .flo...");
        AppendMenuW(mb, MF_POPUP, (UINT_PTR)mf, L"File");
        SetMenu(hwnd, mb);

        g_hComboFilter = CreateWindowExW(0, WC_COMBOBOXW, NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            10, 10, 200, 200, hwnd, (HMENU)ID_COMBO_FILTER, g_hInst, NULL);
        g_hLabelPre = CreateWindowW(L"STATIC", L"Pre: 0", WS_CHILD | WS_VISIBLE | SS_LEFT, 220, 10, 100, 20, hwnd, NULL, g_hInst, NULL);
        g_hLabelPos = CreateWindowW(L"STATIC", L"Pos: 0", WS_CHILD | WS_VISIBLE | SS_LEFT, 330, 10, 100, 20, hwnd, NULL, g_hInst, NULL);

        g_hListEvents = CreateWindowExW(0, WC_LISTVIEWW, NULL,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER | WS_TABSTOP,
            10, 40, rc.right - 20, 300, hwnd, (HMENU)ID_LIST_EVENTS, g_hInst, NULL);
        ListView_SetExtendedListViewStyle(g_hListEvents, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

        LVCOLUMNW col{ LVCF_TEXT | LVCF_WIDTH };
        std::pair<int, PCWSTR> defs[] = {
            {80, L"Event ID"}, {80, L"Bundle"}, {80, L"Simple ID"},
            {250, L"Event Name"}, {150, L"Sound File"}, {70,  L"Section"} // Section width increased slightly
        };
        for (int i = 0; i < 6; ++i) {
            col.cx = defs[i].first; col.pszText = (LPWSTR)defs[i].second;
            ListView_InsertColumn(g_hListEvents, i, &col);
        }

        CreateInputControls(hwnd, rc); // Create all bottom controls
        break;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(hwnd, &rc);
        // Top row
        MoveWindow(g_hComboFilter, 10, 10, 200, 200, TRUE);
        MoveWindow(g_hLabelPre, 220, 10, 100, 20, TRUE);
        MoveWindow(g_hLabelPos, 330, 10, 100, 20, TRUE);

        int list_bottom_margin = 280; // Margin for input controls area
        MoveWindow(g_hListEvents, 10, 40, rc.right - 20, rc.bottom - 40 - list_bottom_margin, TRUE);

        RepositionInputControls(hwnd, rc); // Reposition all bottom controls

        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        break;
    }
    case WM_NOTIFY:
        if (((LPNMHDR)lParam)->hwndFrom == g_hListEvents && ((LPNMHDR)lParam)->code == LVN_COLUMNCLICK) {
            LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
            int clickedColumn = pnmv->iSubItem;
            if (clickedColumn == g_sortColumn) {
                g_sortAscending = !g_sortAscending;
            }
            else {
                g_sortColumn = clickedColumn;
                g_sortAscending = true;
            }
            SortEvents();
            PopulateList();

            // Add sort indicator arrow
            HWND hHeader = ListView_GetHeader(g_hListEvents);
            HDITEM hdi = { 0 };
            hdi.mask = HDI_FORMAT;

            for (int i = 0; i < Header_GetItemCount(hHeader); ++i) {
                Header_GetItem(hHeader, i, &hdi);
                hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN); // Clear previous arrows
                if (i == g_sortColumn) {
                    hdi.fmt |= (g_sortAscending ? HDF_SORTUP : HDF_SORTDOWN);
                }
                Header_SetItem(hHeader, i, &hdi);
            }
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == g_hEditSdfId) {
            wchar_t sdf_id_text[64];
            GetWindowTextW(g_hEditSdfId, sdf_id_text, 64);
            SetWindowTextW(g_hEditSeSoundFileId, sdf_id_text);
        }

        switch (LOWORD(wParam)) {
        case ID_MENU_OPEN: OpenAndLoadFlo(hwnd); break;
        case ID_COMBO_FILTER:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                wchar_t buf[128] = {};
                int sel = SendMessageW(g_hComboFilter, CB_GETCURSEL, 0, 0);
                SendMessageW(g_hComboFilter, CB_GETLBTEXT, sel, (LPARAM)buf);
                g_filterName = buf;
                SortEvents(); // Re-sort if filter changes
                PopulateList();
            }
            break;
        case IDC_BTN_ADD: AddEvent(); break;
        case IDC_BTN_ADD_SOUND_SIMPLE: AddSoundAndSimpleEvent(); break;
        case IDC_BTN_SAVE: SaveFlo(hwnd); break;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Entry point
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInstance;
    INITCOMMONCONTROLSEX ic{ sizeof(ic),ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&ic);
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"FloGui";
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"FloGui", L"FLO Event Editor", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 850, 750, // Slightly wider default
        NULL, NULL, hInstance, NULL);
    if (!hwnd) return FALSE;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return (int)msg.wParam;
}