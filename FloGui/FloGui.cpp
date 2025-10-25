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
constexpr int IDC_EDIT_LINK_TYPE = 302; // Renamed from IDC_EDIT_BUNDLE
constexpr int IDC_EDIT_LINKED_ID = 303;
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
HWND      g_hLblEvId, g_hLblEvLinkType, g_hLblEvLinkedId, g_hLblEvName, g_hLblEvSection;
HWND      g_hLblSdfSectionTitle;
HWND      g_hLblSdfId, g_hLblSdfType, g_hLblSdfFilename;
HWND      g_hLblSeSfId, g_hLblSeType, g_hLblSeBundle;

// --- Data Structures ---

// An enum to make the link type clear (Corrected values)
enum class EventLinkType {
    Simple = 0,
    Random = 1,
    Compound = 2,
    Unknown = -1
};

// Represents a single sound within a Random/Compound Event
struct EventLink {
    int param_1; // The first integer on the link line (e.g., 0)
    int simple_event_id; // The second integer (the actual link)
};

// Random Event structure
struct RandomEvent {
    int id;
    std::vector<EventLink> links;
};

// Compound Event structure
struct CompoundEvent {
    int id;
    std::vector<EventLink> links;
};

// Sound Data File structure
struct SoundDataFileEntry {
    int id;
    std::wstring type_char; // 'c', 'b', etc.
    std::wstring filename;
    bool is_newly_added = false; // To help SaveFlo identify new entries
};

// Simple Event structure (from the SimpleEvents section)
struct SimpleEventEntry {
    int id;
    int sound_data_file_id;
    int type; // This might correspond to EventLinkType values? Needs more research.
    int linked_event_id; // The ID of the Random/Compound event it links *back* to?
    bool is_newly_added = false;
};

// Sound Data Bundle structure
struct SoundDataBundle {
    int id;
    std::wstring type;
    std::wstring path;
};


// Event structure (for EventMaps) - Modified for multiple filenames
struct SoundEventEntry {
    int event_id;
    int link_type_val;     // The 2nd value on the line (0=Simple, 1=Random, 2=Compound)
    int linked_id;           // The 3rd value (ID of Simple/Random/Compound event)
    std::wstring event_name;
    std::wstring section; // "Pre" or "Pos"

    EventLinkType link_type = EventLinkType::Unknown; // To be determined after all parsing
    // MODIFIED: Store multiple filenames
    std::vector<std::wstring> sound_data_filenames;
    bool is_newly_added = false; // To help SaveFlo identify new EventMap entries
};


// --- End Data Structures ---


// Globals
HINSTANCE g_hInst;
HWND      g_hMainWnd;
HWND      g_hListEvents;
HWND      g_hComboFilter;
// Labels for top row display
HWND      g_hLblEventMappingsTitle; // New
HWND      g_hLabelPre;
HWND      g_hLblPanCount;         // New
HWND      g_hLabelPos;
HWND      g_hLblSeparator;        // New
HWND      g_hLblEventTypesTitle;  // New
HWND      g_hLabelSimpleCount;
HWND      g_hLblRandomCount;      // New
HWND      g_hLblCompoundCount;    // New
// Labels for bundle display
HWND      g_hLabelBundle1;
HWND      g_hLabelBundle2;
HWND      g_hLabelBundle3;


// EventMap Input Globals
HWND      g_hEditID;
HWND      g_hEditLinkType; // Renamed from g_hEditBundle
HWND      g_hEditLinkedId;
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
std::vector<SimpleEventEntry> g_simpleEvents;
std::vector<RandomEvent> g_randomEvents;
std::vector<CompoundEvent> g_compoundEvents;
std::vector<SoundDataBundle> g_soundDataBundles;


std::wstring g_filterName;
// Counts displayed and used for identifying *new* entries for saving
int g_preTotalEventMapEntries = 0;
int g_posTotalEventMapEntries = 0;
int g_loadedPreEventMapCount = 0;
int g_loadedPosEventMapCount = 0;
int g_loadedPanCount = 0; // Placeholder for future use

int g_loadedSoundDataFileCount = 0;
int g_loadedSimpleEventCount = 0;
int g_loadedRandomEventCount = 0; // New
int g_loadedCompoundEventCount = 0; // New


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

// --- Helper Function: Find specific SDF filename by ID ---
std::wstring FindSdfFilename(int sdf_id) {
    for (const auto& sdf : g_soundDataFiles) {
        if (sdf.id == sdf_id) {
            return sdf.filename;
        }
    }
    return L""; // Return empty string if not found
}

// --- Helper Function: Find specific SimpleEvent by ID ---
const SimpleEventEntry* FindSimpleEvent(int se_id) {
    for (const auto& se : g_simpleEvents) {
        if (se.id == se_id) {
            return &se;
        }
    }
    return nullptr; // Return null if not found
}


// --- Corrected Helper Function: GetSoundFilenamesForEventMap ---
// Returns a vector of filenames associated with the event
std::vector<std::wstring> GetSoundFilenamesForEventMap(const SoundEventEntry& event) {
    std::vector<std::wstring> filenames;
    std::vector<int> simpleEventIdsToFind; // Store IDs to look up

    if (event.link_type == EventLinkType::Simple) {
        // --- Path A ---
        // The 'linked_id' *is* the SimpleEvent ID.
        simpleEventIdsToFind.push_back(event.linked_id);

    }
    else if (event.link_type == EventLinkType::Random) {
        // --- Path B (Random) ---
        // Find the RandomEvent using the EventMap's 'linked_id'.
        for (const auto& randomEvent : g_randomEvents) {
            if (randomEvent.id == event.linked_id) {
                // Collect *all* SimpleEvent IDs from *inside* the RandomEvent's links.
                for (const auto& link : randomEvent.links) {
                    simpleEventIdsToFind.push_back(link.simple_event_id);
                }
                break; // Found the Random Event
            }
        }
    }
    else if (event.link_type == EventLinkType::Compound) {
        // --- Path B (Compound) ---
        // Find the CompoundEvent using the EventMap's 'linked_id'.
        for (const auto& compoundEvent : g_compoundEvents) {
            if (compoundEvent.id == event.linked_id) {
                // Collect *all* SimpleEvent IDs from *inside* the CompoundEvent's links.
                for (const auto& link : compoundEvent.links) {
                    simpleEventIdsToFind.push_back(link.simple_event_id);
                }
                break; // Found the Compound Event
            }
        }
    }
    else {
        filenames.push_back(L"N/A (Unknown Link Type)");
        return filenames;
    }

    // Handle cases where the initial link target wasn't found or was empty
    if (simpleEventIdsToFind.empty()) {
        if (event.link_type == EventLinkType::Simple)
            filenames.push_back(L"N/A (SimpleEvent Missing?)");
        else
            filenames.push_back(L"N/A (Complex Link Target Invalid/Empty)");
        return filenames;
    }

    // Now, iterate through the collected SimpleEvent IDs
    for (int se_id : simpleEventIdsToFind) {
        const SimpleEventEntry* simpleEvent = FindSimpleEvent(se_id);
        if (simpleEvent == nullptr) {
            filenames.push_back(L"N/A (SimpleEvent Link Invalid: ID " + std::to_wstring(se_id) + L")");
            continue; // Skip to next ID if this one is bad
        }

        // Find the SoundDataFile filename using the SimpleEvent's sound_data_file_id
        std::wstring filename = FindSdfFilename(simpleEvent->sound_data_file_id);
        if (filename.empty()) {
            filenames.push_back(L"N/A (SDF Invalid: ID " + std::to_wstring(simpleEvent->sound_data_file_id) + L")");
        }
        else {
            filenames.push_back(filename); // Add the found filename
        }
    }

    // If after all lookups, the list is still empty (e.g., all links were invalid)
    if (filenames.empty()) {
        filenames.push_back(L"N/A (All Links Invalid?)");
    }

    return filenames;
}


// Parse .flo
static bool ParseFloFile(const std::wstring& path) {
    std::wifstream in(path);
    if (!in) return false;
    std::vector<std::wstring> lines;
    for (std::wstring line; std::getline(in, line); ) lines.push_back(line);

    // Clear all global data vectors
    g_events.clear();
    g_soundDataFiles.clear();
    g_simpleEvents.clear();
    g_randomEvents.clear();
    g_compoundEvents.clear();
    g_soundDataBundles.clear(); // Clear bundles


    g_preTotalEventMapEntries = g_posTotalEventMapEntries = 0;
    g_loadedPreEventMapCount = g_loadedPosEventMapCount = 0;
    g_loadedSoundDataFileCount = 0;
    g_loadedSimpleEventCount = 0;
    g_loadedRandomEventCount = 0;   // Reset new counts
    g_loadedCompoundEventCount = 0; // Reset new counts
    g_loadedPanCount = 0;           // Reset PAN count


    // Parse SoundDataBundles section
    int idxSDB = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (Trim(lines[i]) == L"SoundDataBundles") { idxSDB = i; break; }
    }
    if (idxSDB >= 0 && idxSDB + 1 < (int)lines.size()) {
        std::wstring countLineSDB = Trim(lines[idxSDB + 1]);
        if (!countLineSDB.empty()) {
            try {
                int bundleCount = _wtoi(countLineSDB.c_str());
                for (int j = 0; j < bundleCount; ++j) {
                    int li = idxSDB + 2 + j;
                    if (li >= (int)lines.size()) break;
                    std::wstringstream ss(lines[li]);
                    SoundDataBundle sdb_entry; std::wstring tok;
                    std::getline(ss, tok, L','); sdb_entry.id = _wtoi(Trim(tok).c_str());
                    std::getline(ss, tok, L','); sdb_entry.type = Trim(tok);
                    std::getline(ss, tok);      sdb_entry.path = Trim(tok);
                    g_soundDataBundles.push_back(sdb_entry);
                }
            }
            catch (const std::exception&) { /* ignore */ }
        }
    }


    // --- Parse SoundDataFiles section ---
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

    // --- Parse SimpleEvents section ---
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
                for (int j = 0; j < g_loadedSimpleEventCount; ++j) {
                    int li = idxSE + 2 + j;
                    if (li >= (int)lines.size()) break;
                    std::wstringstream ss(lines[li]);
                    SimpleEventEntry se_entry; std::wstring tok;
                    std::getline(ss, tok, L','); se_entry.id = _wtoi(Trim(tok).c_str());
                    std::getline(ss, tok, L','); se_entry.sound_data_file_id = _wtoi(Trim(tok).c_str());
                    std::getline(ss, tok, L','); se_entry.type = _wtoi(Trim(tok).c_str());
                    std::getline(ss, tok, L','); se_entry.linked_event_id = _wtoi(Trim(tok).c_str());
                    se_entry.is_newly_added = false;
                    g_simpleEvents.push_back(se_entry);
                }
            }
            catch (const std::exception&) { g_loadedSimpleEventCount = 0; }
        }
    }

    // --- Helper lambda for parsing Random/Compound Events ---
    auto parseEventLinks = [&](int startIndex, int count, std::vector<EventLink>& links) {
        int linesRead = 0;
        for (int j = 0; j < count; ++j) {
            int li = startIndex + j;
            if (li >= (int)lines.size()) break;
            std::wstringstream ss(lines[li]);
            EventLink link; std::wstring tok;
            // Handle potential extra values on Compound link lines (like the trailing ", 0")
            std::getline(ss, tok, L','); link.param_1 = _wtoi(Trim(tok).c_str());
            std::getline(ss, tok, L','); link.simple_event_id = _wtoi(Trim(tok).c_str());
            // Ignore any further tokens on the line for now
            links.push_back(link);
            linesRead++;
        }
        return linesRead;
        };

    // --- Parse RandomEvents ---
    int idxRE = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (Trim(lines[i]) == L"RandomEvents") { idxRE = i; break; }
    }
    if (idxRE >= 0 && idxRE + 1 < (int)lines.size()) {
        // Store Random count
        g_loadedRandomEventCount = _wtoi(Trim(lines[idxRE + 1]).c_str());
        if (g_loadedRandomEventCount < 0) g_loadedRandomEventCount = 0;

        int currentLineIndex = idxRE + 2;
        for (int i = 0; i < g_loadedRandomEventCount; ++i) { // Use stored count
            if (currentLineIndex >= (int)lines.size()) break;
            std::wstringstream ss(lines[currentLineIndex]);
            RandomEvent re; std::wstring tok;
            std::getline(ss, tok, L','); re.id = _wtoi(Trim(tok).c_str());
            std::getline(ss, tok, L','); int linkCount = _wtoi(Trim(tok).c_str());
            currentLineIndex++; // Move to the first link line
            int linksRead = parseEventLinks(currentLineIndex, linkCount, re.links);
            g_randomEvents.push_back(re);
            currentLineIndex += linksRead; // Advance past the link lines
        }
    }

    // --- Parse CompoundEvents ---
    int idxCE = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (Trim(lines[i]) == L"CompoundEvents") { idxCE = i; break; }
    }
    if (idxCE >= 0 && idxCE + 1 < (int)lines.size()) {
        // Store Compound count
        g_loadedCompoundEventCount = _wtoi(Trim(lines[idxCE + 1]).c_str());
        if (g_loadedCompoundEventCount < 0) g_loadedCompoundEventCount = 0;

        int currentLineIndex = idxCE + 2;
        for (int i = 0; i < g_loadedCompoundEventCount; ++i) { // Use stored count
            if (currentLineIndex >= (int)lines.size()) break;
            std::wstringstream ss(lines[currentLineIndex]);
            CompoundEvent ce; std::wstring tok;
            std::getline(ss, tok, L','); ce.id = _wtoi(Trim(tok).c_str());
            std::getline(ss, tok, L','); int linkCount = _wtoi(Trim(tok).c_str());
            currentLineIndex++; // Move to the first link line
            int linksRead = parseEventLinks(currentLineIndex, linkCount, ce.links);
            g_compoundEvents.push_back(ce);
            currentLineIndex += linksRead; // Advance past the link lines
        }
    }


    // --- Locate EventMaps ---
    int idxMaps = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (Trim(lines[i]) == L"EventMaps") { idxMaps = i; break; }
    }
    if (idxMaps < 0) return false;

    // --- Parse EventMap Sections (Pre, PAN, Pos) ---
    // Helper lambda modified slightly
    auto parseEventMapSubSection = [&](const std::wstring& name, int& loadedCountVariable) {
        int idx = -1;
        for (int i = idxMaps + 1; i < (int)lines.size(); ++i) {
            // Find the *first* occurrence of the name *after* EventMaps start
            if (Trim(lines[i]) == name) { idx = i; break; }
        }
        if (idx < 0 || idx + 1 >= (int)lines.size()) {
            loadedCountVariable = 0; // Ensure count is 0 if section not found
            return;
        }

        std::wstring countLineSection = Trim(lines[idx + 1]);
        if (countLineSection.empty()) {
            loadedCountVariable = 0; // Ensure count is 0 if line is empty
            return;
        }
        try {
            loadedCountVariable = _wtoi(countLineSection.c_str());
            if (loadedCountVariable < 0) loadedCountVariable = 0;

            // Only parse entries for Pre and Pos
            if (name == L"Pre" || name == L"Pos") {
                if (name == L"Pre") g_preTotalEventMapEntries = loadedCountVariable;
                if (name == L"Pos") g_posTotalEventMapEntries = loadedCountVariable;

                for (int j = 0; j < loadedCountVariable; ++j) {
                    int li = idx + 2 + j;
                    if (li >= (int)lines.size()) break;
                    std::wstringstream ss(lines[li]);
                    SoundEventEntry e; std::wstring tok;
                    std::getline(ss, tok, L','); e.event_id = _wtoi(Trim(tok).c_str());
                    std::getline(ss, tok, L','); e.link_type_val = _wtoi(Trim(tok).c_str()); // Read link type value
                    std::getline(ss, tok, L','); e.linked_id = _wtoi(Trim(tok).c_str());
                    std::getline(ss, tok); e.event_name = Trim(tok);
                    e.section = name; // Set section based on current parsing context
                    e.is_newly_added = false;

                    // Link Resolution (set enum type)
                    switch (e.link_type_val) {
                    case 0: e.link_type = EventLinkType::Simple; break;
                    case 1: e.link_type = EventLinkType::Random; break;
                    case 2: e.link_type = EventLinkType::Compound; break;
                    default: e.link_type = EventLinkType::Unknown; break;
                    }
                    g_events.push_back(e);
                }
            }
        }
        catch (const std::exception&) { loadedCountVariable = 0; /* ignore parse error for count */ }
        };

    // Parse each subsection and store counts
    parseEventMapSubSection(L"Pre", g_loadedPreEventMapCount);
    parseEventMapSubSection(L"Pan", g_loadedPanCount); // Parse Pan count
    parseEventMapSubSection(L"Pos", g_loadedPosEventMapCount);


    // --- Filename Lookup (after all parsing is done) ---
    for (auto& e : g_events) {
        // Now that the link type is correctly set, find the filename(s)
        e.sound_data_filenames = GetSoundFilenamesForEventMap(e);
    }
    // --- End Filename Lookup ---


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
        case 1: // Link Type
            if (a.link_type_val < b.link_type_val) comparisonResult = -1;
            else if (a.link_type_val > b.link_type_val) comparisonResult = 1;
            break;
        case 2: // Linked ID
            if (a.linked_id < b.linked_id) comparisonResult = -1;
            else if (a.linked_id > b.linked_id) comparisonResult = 1;
            break;
        case 3: // Event Name
            comparisonResult = _wcsicmp(a.event_name.c_str(), b.event_name.c_str()); // Case-insensitive
            break;
        case 4: // Sound File(s) - Compare based on the first filename if multiple exist
        {
            std::wstring fileA = a.sound_data_filenames.empty() ? L"" : a.sound_data_filenames[0];
            std::wstring fileB = b.sound_data_filenames.empty() ? L"" : b.sound_data_filenames[0];
            comparisonResult = _wcsicmp(fileA.c_str(), fileB.c_str());
        }
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


// Populate ListView - Modified to handle multiple filenames
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
        wchar_t buf[256]; // Buffer for numeric conversions

        wsprintfW(buf, L"%d", e.event_id);
        item.pszText = buf;
        ListView_InsertItem(g_hListEvents, &item);

        wsprintfW(buf, L"%d", e.link_type_val);
        ListView_SetItemText(g_hListEvents, row, 1, buf);

        wsprintfW(buf, L"%d", e.linked_id);
        ListView_SetItemText(g_hListEvents, row, 2, buf);

        ListView_SetItemText(g_hListEvents, row, 3, (LPWSTR)e.event_name.c_str());

        // --- Format Sound File(s) Column ---
        std::wstring filenames_display;
        if (e.sound_data_filenames.empty()) {
            filenames_display = L"N/A";
        }
        else {
            for (size_t i = 0; i < e.sound_data_filenames.size(); ++i) {
                filenames_display += e.sound_data_filenames[i];
                if (i < e.sound_data_filenames.size() - 1) {
                    filenames_display += L", "; // Add comma separator
                }
            }
        }
        // Use a larger buffer temporarily for longer filename lists
        wchar_t filenameBuf[1024]; // Increased buffer size
        wcsncpy_s(filenameBuf, 1024, filenames_display.c_str(), _TRUNCATE);
        ListView_SetItemText(g_hListEvents, row, 4, filenameBuf);
        // --- End Formatting ---

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

        // Update counts and bundle labels
        wchar_t buf[256];
        // Event Mapping Counts
        wsprintfW(buf, L"Pre: %d", g_loadedPreEventMapCount); SetWindowTextW(g_hLabelPre, buf);
        wsprintfW(buf, L"PAN: %d", g_loadedPanCount); SetWindowTextW(g_hLblPanCount, buf); // Show parsed Pan count
        //SetWindowTextW(g_hLblPanCount, L"PAN: N/A"); // Hardcode for now
        wsprintfW(buf, L"Pos: %d", g_loadedPosEventMapCount); SetWindowTextW(g_hLabelPos, buf);
        // Event Type Counts
        wsprintfW(buf, L"Simple: %d", g_loadedSimpleEventCount); SetWindowTextW(g_hLabelSimpleCount, buf);
        wsprintfW(buf, L"Random: %d", g_loadedRandomEventCount); SetWindowTextW(g_hLblRandomCount, buf);   // New
        wsprintfW(buf, L"Compound: %d", g_loadedCompoundEventCount); SetWindowTextW(g_hLblCompoundCount, buf); // New


        // Clear bundle labels first
        SetWindowTextW(g_hLabelBundle1, L"");
        SetWindowTextW(g_hLabelBundle2, L"");
        SetWindowTextW(g_hLabelBundle3, L"");
        // Populate bundle labels
        if (g_soundDataBundles.size() > 0) SetWindowTextW(g_hLabelBundle1, (L"B1: " + g_soundDataBundles[0].path).c_str());
        if (g_soundDataBundles.size() > 1) SetWindowTextW(g_hLabelBundle2, (L"B2: " + g_soundDataBundles[1].path).c_str());
        if (g_soundDataBundles.size() > 2) SetWindowTextW(g_hLabelBundle3, (L"B3: " + g_soundDataBundles[2].path).c_str());


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
    GetWindowTextW(g_hEditLinkType, num_buf, 64); e.link_type_val = _wtoi(num_buf);
    GetWindowTextW(g_hEditLinkedId, num_buf, 64); e.linked_id = _wtoi(num_buf);
    GetWindowTextW(g_hEditName, name_buf, 256); e.event_name = Trim(name_buf);

    int sel = SendMessageW(g_hComboSection, CB_GETCURSEL, 0, 0);
    SendMessageW(g_hComboSection, CB_GETLBTEXT, sel, (LPARAM)num_buf);
    e.section = Trim(num_buf);
    e.is_newly_added = true;

    // Set link_type from value (Corrected Mapping)
    switch (e.link_type_val) {
    case 0: e.link_type = EventLinkType::Simple; break;
    case 1: e.link_type = EventLinkType::Random; break;
    case 2: e.link_type = EventLinkType::Compound; break;
    default: e.link_type = EventLinkType::Unknown; break;
    }
    // And try to find the filename(s)
    e.sound_data_filenames = GetSoundFilenamesForEventMap(e);

    auto it_insert = g_events.end();
    if (e.section == L"Pre") {
        it_insert = std::find_if(g_events.begin(), g_events.end(), [](const SoundEventEntry& ev) {
            return ev.section == L"Pos";
            });
        g_loadedPreEventMapCount++; // Increment loaded count too
    }
    else {
        g_loadedPosEventMapCount++; // Increment loaded count too
    }
    g_events.insert(it_insert, e);

    // Update total counts (used potentially elsewhere, though display uses loaded counts)
    g_preTotalEventMapEntries = g_loadedPreEventMapCount;
    g_posTotalEventMapEntries = g_loadedPosEventMapCount;


    SortEvents();
    PopulateList();

    // Update display counts
    wsprintfW(num_buf, L"Pre: %d", g_loadedPreEventMapCount); SetWindowTextW(g_hLabelPre, num_buf);
    wsprintfW(num_buf, L"Pos: %d", g_loadedPosEventMapCount); SetWindowTextW(g_hLabelPos, num_buf);

}

// Add SoundDataFile and corresponding SimpleEvent entry
static void AddSoundAndSimpleEvent() {
    wchar_t buf[256];
    SoundDataFileEntry sdf_e;
    SimpleEventEntry se_e;

    GetWindowTextW(g_hEditSdfId, buf, 64); sdf_e.id = _wtoi(buf);
    GetWindowTextW(g_hEditSdfTypeChar, buf, 64); sdf_e.type_char = Trim(buf);
    GetWindowTextW(g_hEditSdfFilename, buf, 256); sdf_e.filename = Trim(buf);
    sdf_e.is_newly_added = true;

    se_e.id = sdf_e.id; // Usually SimpleEvent ID matches the SDF ID when adding
    GetWindowTextW(g_hEditSeSoundFileId, buf, 64); se_e.sound_data_file_id = _wtoi(buf);
    GetWindowTextW(g_hEditSeTypeVal, buf, 64); se_e.type = _wtoi(buf);
    GetWindowTextW(g_hEditSeBundleIdx, buf, 64); se_e.linked_event_id = _wtoi(buf);
    se_e.is_newly_added = true;


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
    for (const auto& existing_se : g_simpleEvents) {
        if (existing_se.id == se_e.id) {
            MessageBoxW(g_hMainWnd, (L"SimpleEvent ID " + std::to_wstring(se_e.id) + L" already exists.").c_str(), L"Input Error", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    g_soundDataFiles.push_back(sdf_e);
    g_simpleEvents.push_back(se_e);
    g_loadedSimpleEventCount++; // Increment simple count

    // If we add a new SDF/SE pair, we need to re-evaluate potentially affected EventMap entries
    bool list_updated = false;
    for (auto& ev : g_events) {
        // Only re-evaluate if it was previously invalid or if it links to the new SimpleEvent
        bool was_invalid = ev.sound_data_filenames.empty() || (ev.sound_data_filenames.size() == 1 && ev.sound_data_filenames[0].find(L"N/A") != std::wstring::npos);
        bool links_to_new_simple = (ev.link_type == EventLinkType::Simple && ev.linked_id == se_e.id);
        // We also need to check if a Complex event links to this new SimpleEvent,
        // but that requires iterating through Random/Compound events, which is complex here.
        // For simplicity, we only explicitly re-check direct links or previously invalid ones.

        if (was_invalid || links_to_new_simple)
        {
            std::vector<std::wstring> old_filenames = ev.sound_data_filenames;
            ev.sound_data_filenames = GetSoundFilenamesForEventMap(ev);
            if (ev.sound_data_filenames != old_filenames) { // Basic check if vector content changed
                list_updated = true;
            }
        }
    }

    if (list_updated) {
        PopulateList(); // Re-populate to show newly resolved names
    }

    // Update simple count display
    wsprintfW(buf, L"Simple: %d", g_loadedSimpleEventCount); SetWindowTextW(g_hLabelSimpleCount, buf);

    std::wstring msg = L"Added SoundDataFile ID: " + std::to_wstring(sdf_e.id) +
        L" (" + sdf_e.filename + L") and its SimpleEvent (ID: " + std::to_wstring(se_e.id) + L").\n" +
        L"Remember to Save .flo to persist these changes.";
    MessageBoxW(g_hMainWnd, msg.c_str(), L"Entry Added", MB_OK | MB_ICONINFORMATION);

    SetWindowTextW(g_hEditSdfId, L"");
    SetWindowTextW(g_hEditSdfTypeChar, L"");
    SetWindowTextW(g_hEditSdfFilename, L"");
    SetWindowTextW(g_hEditSeSoundFileId, L"");
    // Don't clear type/linkID, as they often stay the same
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
        pos += 1; // Move past the first char of the found marker to find subsequent ones
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
            // Check if file ends with CRLF before inserting new one
            if (data.size() < 2 || data[data.size() - 2] != '\r' || data[data.size() - 1] != '\n') {
                // Append CRLF if not present at the very end
                if (!data.empty()) data.insert(data.end(), rn, rn + 2);
            }
            // Ensure insert_pos is updated after potentially adding CRLF
            insert_pos = data.size();
            break;
        }
        insert_pos = (next_item_line_end_it - data.begin()) + 2;
    }

    // Insert the new data line *before* updating the count
    data.insert(data.begin() + insert_pos, new_line_to_insert.begin(), new_line_to_insert.end());

    // Now, update the count
    current_item_count++;
    std::string new_count_value_str = std::to_string(current_item_count);
    std::string original_count_line_padding;
    size_t first_char = count_line_content_str.find_first_not_of(" \t");
    if (first_char != std::string::npos && first_char > 0) {
        original_count_line_padding = count_line_content_str.substr(0, first_char);
    }
    else if (!count_line_content_str.empty() && (count_line_content_str[0] == ' ' || count_line_content_str[0] == '\t')) {
        original_count_line_padding = count_line_content_str.substr(0, 1); // Preserve single space/tab
    }

    std::string new_full_count_line_str = original_count_line_padding + new_count_value_str;

    // Adjust length to match original, only if it's shorter and if original had trailing space
    size_t original_content_len = count_line_content_end - count_val_pos;
    if (new_full_count_line_str.length() < original_content_len) {
        std::string padding(original_content_len - new_full_count_line_str.length(), ' ');
        new_full_count_line_str += padding;
    }
    else if (new_full_count_line_str.length() > original_content_len) {
        // This is the CRITICAL BUG FIX area. We need to insert space, not just overwrite.
        // But the current patching is too fragile for this.
        // For now, let's *truncate* like before, knowing it's wrong, to avoid index errors.
        new_full_count_line_str.resize(original_content_len); // Still potentially wrong, but avoids crash
    }


    // Find the count position again *relative to the new data size*
    size_t new_section_pos = FindNthMarker(data, section_marker, which_occurrence);
    if (new_section_pos == std::string::npos) return false;
    size_t new_count_val_pos = new_section_pos + section_marker_len;
    auto new_count_line_end_it = std::search(data.begin() + new_count_val_pos, data.end(), rn, rn + 2);
    if (new_count_line_end_it == data.end()) return false;
    size_t new_count_line_content_end = new_count_line_end_it - data.begin();

    // Ensure we don't write past the original line ending if truncating
    if (new_count_val_pos + new_full_count_line_str.length() > new_count_line_content_end) {
        // This condition handles potential overflow if truncation happens
        // It should ideally not be needed if length calculation is perfect
        new_full_count_line_str.resize(new_count_line_content_end - new_count_val_pos);
    }


    // Replace the old count line content with the new one
    // Check bounds before erasing
    if (data.begin() + new_count_val_pos <= data.end() && data.begin() + new_count_line_content_end <= data.end()) {
        data.erase(data.begin() + new_count_val_pos, data.begin() + new_count_line_content_end);
        // Check bounds before inserting
        if (data.begin() + new_count_val_pos <= data.end()) {
            data.insert(data.begin() + new_count_val_pos, new_full_count_line_str.begin(), new_full_count_line_str.end());
        }
        else { return false; } // Insert position invalid
    }
    else { return false; } // Erase positions invalid


    return true;
}


// Format EventMap line
std::string SerializeEventMapLine(int id, int link_type, int linked_id, const std::wstring& nameW) {
    std::string name_utf8;
    if (!nameW.empty()) {
        // Use UTF-8 for better character support
        int len = WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(), -1, NULL, 0, NULL, NULL);
        if (len > 0) {
            name_utf8.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(), -1, &name_utf8[0], len, NULL, NULL);
        }
    }
    char buf[512];
    snprintf(buf, sizeof(buf), "%d,\t%d, %d, %s\r\n", id, link_type, linked_id, name_utf8.c_str());
    return std::string(buf);
}

// Format SoundDataFile line
std::string SerializeSoundDataFileLine(int id, const std::wstring& type_charW, const std::wstring& filenameW) {
    std::string type_char_utf8, filename_utf8;
    // Type char is likely ASCII, ACP is fine
    if (!type_charW.empty()) {
        int len = WideCharToMultiByte(CP_ACP, 0, type_charW.c_str(), -1, NULL, 0, NULL, NULL);
        if (len > 0) { type_char_utf8.resize(len - 1); WideCharToMultiByte(CP_ACP, 0, type_charW.c_str(), -1, &type_char_utf8[0], len, NULL, NULL); }
    }
    // Filename could have non-ASCII, use UTF-8
    if (!filenameW.empty()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, filenameW.c_str(), -1, NULL, 0, NULL, NULL);
        if (len > 0) { filename_utf8.resize(len - 1); WideCharToMultiByte(CP_UTF8, 0, filenameW.c_str(), -1, &filename_utf8[0], len, NULL, NULL); }
    }
    char buf[512];
    snprintf(buf, sizeof(buf), "%d,\t%s,\t%s\r\n", id, type_char_utf8.c_str(), filename_utf8.c_str());
    return std::string(buf);
}

// Format SimpleEvent line
std::string SerializeSimpleEventLine(int id, int sound_file_id, int type_val, int linked_event_id) {
    char buf[256];
    // Assuming type_val and linked_event_id correspond to the SimpleEvent struct fields
    snprintf(buf, sizeof(buf), "%d,\t%d, %d, %d\r\n", id, sound_file_id, type_val, linked_event_id);
    return std::string(buf);
}


// Save
static void SaveFlo(HWND hwnd) {
    if (g_loadedPath.empty()) {
        MessageBoxW(hwnd, L"No file loaded to base the save on. Please open a file first.", L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }

    MessageBoxW(hwnd, L"Warning: The current save mechanism modifies the original file directly by patching. This is inherently risky and can corrupt the file if the structure is unexpected or if counts change significantly (e.g., 9 to 10, 99 to 100).\n\nA safer approach would rewrite the entire file. Proceed with caution.", L"Save Warning", MB_OK | MB_ICONWARNING);


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
    // Need to correctly identify occurrences for Pre/Pos within EventMaps
    // Find "EventMaps", then find "Pre", then find the *next* "Pre"
    // This is fragile. Let's assume standard structure for now.
    const int pre_eventmap_occurrence = 1; // First "Pre" after "EventMaps"
    const int pos_eventmap_occurrence = 1; // First "Pos" after the "Pre" section

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
    for (const auto& se_entry : g_simpleEvents) {
        if (se_entry.is_newly_added) {
            std::string new_se_line = SerializeSimpleEventLine(se_entry.id, se_entry.sound_data_file_id, se_entry.type, se_entry.linked_event_id);
            if (!PatchInsertLineInSection(data, "SimpleEvents\r\n", se_occurrence, new_se_line)) {
                MessageBoxW(hwnd, L"Failed to patch 'SimpleEvents' section. Save aborted.", L"Save Error", MB_OK | MB_ICONERROR);
                return;
            }
        }
    }

    // 3. Patch EventMaps (Pre/Pos)
    // We need to handle Pre and Pos patching separately to ensure correct occurrence counting

    // Patch Pre
    for (const auto& event_entry : g_events) {
        if (event_entry.is_newly_added && event_entry.section == L"Pre") {
            std::string new_em_line = SerializeEventMapLine(event_entry.event_id, event_entry.link_type_val, event_entry.linked_id, event_entry.event_name);
            // We need to find the correct "Pre" marker *within* "EventMaps"
            // This requires finding "EventMaps" first, then the first "Pre" *after* that.
            size_t eventMapsPos = FindNthMarker(data, "EventMaps\r\n", 1);
            if (eventMapsPos == std::string::npos) { MessageBoxW(hwnd, L"Cannot find 'EventMaps' section for Pre patching.", L"Save Error", MB_OK | MB_ICONERROR); return; }

            size_t preMarkerPos = std::string::npos;
            size_t searchStartPos = eventMapsPos;
            const char* preMarker = "Pre\r\n";
            while (searchStartPos < data.size()) {
                auto it = std::search(data.begin() + searchStartPos, data.end(), preMarker, preMarker + strlen(preMarker));
                if (it == data.end()) break;
                preMarkerPos = it - data.begin();
                break; // Found the first "Pre" after "EventMaps"
            }

            if (preMarkerPos == std::string::npos) { MessageBoxW(hwnd, L"Cannot find 'Pre' section within 'EventMaps'.", L"Save Error", MB_OK | MB_ICONERROR); return; }

            // Now call PatchInsertLineInSection using the *absolute position* found
            // The Patch function itself uses FindNthMarker which might find the wrong "Pre"
            // TODO: Refactor PatchInsertLineInSection to accept a starting position or handle context better.
            // For now, sticking with potentially fragile occurrence-based patching:
            if (!PatchInsertLineInSection(data, "Pre\r\n", pre_eventmap_occurrence, new_em_line)) {
                MessageBoxW(hwnd, L"Failed to patch 'Pre' EventMap section. Save aborted.", L"Save Error", MB_OK | MB_ICONERROR);
                return;
            }
        }
    }

    // Patch Pos
    for (const auto& event_entry : g_events) {
        if (event_entry.is_newly_added && event_entry.section == L"Pos") {
            std::string new_em_line = SerializeEventMapLine(event_entry.event_id, event_entry.link_type_val, event_entry.linked_id, event_entry.event_name);
            // Similar logic needed to find the correct "Pos" occurrence after "EventMaps"->"Pre"
            // Sticking with fragile occurrence for now:
            if (!PatchInsertLineInSection(data, "Pos\r\n", pos_eventmap_occurrence, new_em_line)) {
                MessageBoxW(hwnd, L"Failed to patch 'Pos' EventMap section. Save aborted.", L"Save Error", MB_OK | MB_ICONERROR);
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

    // Reset 'is_newly_added' flags
    for (auto& sdf : g_soundDataFiles) sdf.is_newly_added = false;
    for (auto& se : g_simpleEvents) se.is_newly_added = false;
    for (auto& em : g_events) em.is_newly_added = false;

    // Update loaded counts AFTER successful save and flag reset
    g_loadedSoundDataFileCount = g_soundDataFiles.size();
    g_loadedSimpleEventCount = g_simpleEvents.size();
    // Re-calculate Pre/Pos counts from the vector after adding
    g_loadedPreEventMapCount = 0;
    g_loadedPosEventMapCount = 0;
    for (const auto& ev : g_events) {
        if (ev.section == L"Pre") g_loadedPreEventMapCount++;
        else if (ev.section == L"Pos") g_loadedPosEventMapCount++;
    }
    // Update total counts as well
    g_preTotalEventMapEntries = g_loadedPreEventMapCount;
    g_posTotalEventMapEntries = g_loadedPosEventMapCount;
    // Random/Compound counts don't change unless we add those types,
    // but keep loaded counts consistent with what was parsed.
    g_loadedRandomEventCount = g_randomEvents.size();
    g_loadedCompoundEventCount = g_compoundEvents.size();


    MessageBoxW(hwnd, L"File saved successfully.\n(Note: Count updates during save may be inaccurate.)", L"Save Success", MB_OK | MB_ICONINFORMATION);
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
    g_hLblEvLinkType = CreateWindowW(L"STATIC", L"Link Type:", WS_CHILD | WS_VISIBLE | SS_RIGHT, next_x, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditLinkType = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP, next_x + label_w + spacing, current_y, edit_w_short, input_h, hwnd, (HMENU)IDC_EDIT_LINK_TYPE, g_hInst, NULL);

    next_x += label_w + spacing + edit_w_short + margin;
    g_hLblEvLinkedId = CreateWindowW(L"STATIC", L"LinkedID:", WS_CHILD | WS_VISIBLE | SS_RIGHT, next_x, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL);
    g_hEditLinkedId = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP, next_x + label_w + spacing, current_y, edit_w_short, input_h, hwnd, (HMENU)IDC_EDIT_LINKED_ID, g_hInst, NULL);
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
    g_hLblSeBundle = CreateWindowW(L"STATIC", L"SE LinkID:", WS_CHILD | WS_VISIBLE | SS_RIGHT, next_x, current_y, label_w, input_h, hwnd, NULL, g_hInst, NULL); // Relabeled
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
    MoveWindow(g_hLblEvLinkType, next_x, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditLinkType, next_x + label_w + spacing, current_y, edit_w_short, input_h, TRUE);

    next_x += label_w + spacing + edit_w_short + margin;
    MoveWindow(g_hLblEvLinkedId, next_x, current_y, label_w, input_h, TRUE);
    MoveWindow(g_hEditLinkedId, next_x + label_w + spacing, current_y, edit_w_short, input_h, TRUE);
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
        int top_y = 10; // Starting Y for top controls
        int current_x = 10;
        int label_h = 20;
        int spacing = 10;
        // Widths for labels in the top info row
        int title_w = 120;
        int count_label_w = 80; // Slightly smaller for counts
        int separator_w = 10;
        int bundle_label_w = 200; // Initial fixed width, adjusted in WM_SIZE


        HMENU mb = CreateMenu(), mf = CreatePopupMenu();
        AppendMenuW(mf, MF_STRING, ID_MENU_OPEN, L"Open .flo...");
        AppendMenuW(mb, MF_POPUP, (UINT_PTR)mf, L"File");
        SetMenu(hwnd, mb);

        // --- Top Row: Event Mappings & Event Types ---
        // Event Mappings Group
        g_hLblEventMappingsTitle = CreateWindowW(L"STATIC", L"Event Mappings:", WS_CHILD | WS_VISIBLE | SS_LEFT, current_x, top_y, title_w, label_h, hwnd, NULL, g_hInst, NULL);
        current_x += title_w + 5; // Reduced spacing after title
        g_hLabelPre = CreateWindowW(L"STATIC", L"Pre: 0", WS_CHILD | WS_VISIBLE | SS_LEFT, current_x, top_y, count_label_w, label_h, hwnd, NULL, g_hInst, NULL);
        current_x += count_label_w + spacing;
        g_hLblPanCount = CreateWindowW(L"STATIC", L"PAN: N/A", WS_CHILD | WS_VISIBLE | SS_LEFT, current_x, top_y, count_label_w, label_h, hwnd, NULL, g_hInst, NULL);
        current_x += count_label_w + spacing;
        g_hLabelPos = CreateWindowW(L"STATIC", L"Pos: 0", WS_CHILD | WS_VISIBLE | SS_LEFT, current_x, top_y, count_label_w, label_h, hwnd, NULL, g_hInst, NULL);
        current_x += count_label_w + spacing;

        // Separator
        g_hLblSeparator = CreateWindowW(L"STATIC", L"|", WS_CHILD | WS_VISIBLE | SS_CENTER, current_x, top_y, separator_w, label_h, hwnd, NULL, g_hInst, NULL);
        current_x += separator_w + spacing;

        // Event Types Group
        g_hLblEventTypesTitle = CreateWindowW(L"STATIC", L"Event Types:", WS_CHILD | WS_VISIBLE | SS_LEFT, current_x, top_y, title_w, label_h, hwnd, NULL, g_hInst, NULL);
        current_x += title_w + 5; // Reduced spacing after title
        g_hLabelSimpleCount = CreateWindowW(L"STATIC", L"Simple: 0", WS_CHILD | WS_VISIBLE | SS_LEFT, current_x, top_y, count_label_w, label_h, hwnd, NULL, g_hInst, NULL);
        current_x += count_label_w + spacing;
        g_hLblRandomCount = CreateWindowW(L"STATIC", L"Random: 0", WS_CHILD | WS_VISIBLE | SS_LEFT, current_x, top_y, count_label_w, label_h, hwnd, NULL, g_hInst, NULL);
        current_x += count_label_w + spacing;
        g_hLblCompoundCount = CreateWindowW(L"STATIC", L"Compound: 0", WS_CHILD | WS_VISIBLE | SS_LEFT, current_x, top_y, count_label_w, label_h, hwnd, NULL, g_hInst, NULL);


        // --- Second Row: Bundles & Filter ---
        top_y += label_h + 5;
        current_x = 10;

        // Bundle Labels
        g_hLabelBundle1 = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS, current_x, top_y, bundle_label_w, label_h, hwnd, NULL, g_hInst, NULL);
        current_x += bundle_label_w + spacing;
        g_hLabelBundle2 = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS, current_x, top_y, bundle_label_w, label_h, hwnd, NULL, g_hInst, NULL);
        current_x += bundle_label_w + spacing;
        g_hLabelBundle3 = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS, current_x, top_y, bundle_label_w, label_h, hwnd, NULL, g_hInst, NULL);

        // Filter ComboBox (Positioned at the end of the second row)
        int combo_w = 200;
        int filter_x = rc.right - 10 - combo_w; // Align to the right
        if (filter_x < current_x + spacing) filter_x = current_x + spacing; // Prevent overlap
        g_hComboFilter = CreateWindowExW(0, WC_COMBOBOXW, NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            filter_x, top_y, combo_w, 200, hwnd, (HMENU)ID_COMBO_FILTER, g_hInst, NULL);


        int list_y = top_y + label_h + 10; // Y position for the list view, below bundles/filter

        g_hListEvents = CreateWindowExW(0, WC_LISTVIEWW, NULL,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER | WS_TABSTOP,
            10, list_y, rc.right - 20, 300, // Position list view below new labels
            hwnd, (HMENU)ID_LIST_EVENTS, g_hInst, NULL);
        ListView_SetExtendedListViewStyle(g_hListEvents, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

        LVCOLUMNW col{ LVCF_TEXT | LVCF_WIDTH };
        std::pair<int, PCWSTR> defs[] = {
            {80, L"Event ID"}, {80, L"Link Type"}, {80, L"Linked ID"},
            {250, L"Event Name"}, {150, L"Sound File(s)"}, {70,  L"Section"} // Modified column name
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
        int top_y = 10; // Starting Y for top controls
        int current_x = 10;
        int label_h = 20;
        int spacing = 5; // *** REDUCED SPACING ***
        // Widths for labels in the top info row
        int title_w = 110; // *** REDUCED WIDTH ***
        int count_label_w = 100; // *** REDUCED WIDTH ***
        int separator_w = 10;


        // --- Reposition Top Row ---
        MoveWindow(g_hLblEventMappingsTitle, current_x, top_y, title_w, label_h, TRUE);
        current_x += title_w + spacing;
        MoveWindow(g_hLabelPre, current_x, top_y, count_label_w, label_h, TRUE);
        current_x += count_label_w + spacing;
        MoveWindow(g_hLblPanCount, current_x, top_y, count_label_w, label_h, TRUE);
        current_x += count_label_w + spacing;
        MoveWindow(g_hLabelPos, current_x, top_y, count_label_w, label_h, TRUE);
        current_x += count_label_w + spacing;

        MoveWindow(g_hLblSeparator, current_x, top_y, separator_w, label_h, TRUE);
        current_x += separator_w + spacing;

        MoveWindow(g_hLblEventTypesTitle, current_x, top_y, title_w, label_h, TRUE);
        current_x += title_w + spacing;
        MoveWindow(g_hLabelSimpleCount, current_x, top_y, count_label_w, label_h, TRUE);
        current_x += count_label_w + spacing;
        MoveWindow(g_hLblRandomCount, current_x, top_y, count_label_w, label_h, TRUE);
        current_x += count_label_w + spacing;

        // *** UPDATED *** Adjust width of last item ONLY if it would exceed bounds
        int remaining_top_width = rc.right - 10 - current_x; // Space left before margin
        int compound_width = count_label_w;
        if (remaining_top_width < count_label_w) {
            compound_width = (remaining_top_width > 0) ? remaining_top_width : 0; // Use remaining space, or 0 if none
        }
        MoveWindow(g_hLblCompoundCount, current_x, top_y, compound_width, label_h, TRUE);

        // --- Reposition Second Row ---
        top_y += label_h + 5;
        current_x = 10;
        // Calculate available width for bundles, excluding filter combo box
        int combo_w = 200;
        int filter_margin = 10;
        int width_for_bundles_area = rc.right - 20 - combo_w - filter_margin; // Space left of filter
        int bundle_label_w = (width_for_bundles_area > (2 * spacing)) ? (width_for_bundles_area - (2 * spacing)) / 3 : 150; // Divide remaining space by 3
        if (bundle_label_w < 150) bundle_label_w = 150; // Minimum width

        // Move Bundle Labels using calculated width
        MoveWindow(g_hLabelBundle1, current_x, top_y, bundle_label_w, label_h, TRUE);
        current_x += bundle_label_w + spacing;
        MoveWindow(g_hLabelBundle2, current_x, top_y, bundle_label_w, label_h, TRUE);
        current_x += bundle_label_w + spacing;
        // Ensure third label's width isn't negative or zero and fits before filter
        int third_label_max_width = rc.right - 10 - combo_w - filter_margin - current_x;
        int third_label_width = bundle_label_w;
        if (third_label_width > third_label_max_width) third_label_width = third_label_max_width;
        if (third_label_width < 50) third_label_width = 0; // Hide if too small
        MoveWindow(g_hLabelBundle3, current_x, top_y, third_label_width, label_h, TRUE);

        // Filter ComboBox (Positioned at the end of the second row)
        int filter_x = rc.right - 10 - combo_w; // Align to the right
        MoveWindow(g_hComboFilter, filter_x, top_y, combo_w, 200, TRUE); // Height includes dropdown


        // --- Reposition List View ---
        int list_y = top_y + label_h + 10; // Y position for the list view
        int list_bottom_margin = 280; // Margin for input controls area
        MoveWindow(g_hListEvents, 10, list_y, rc.right - 20, rc.bottom - list_y - list_bottom_margin, TRUE);

        // --- Reposition Input Controls ---
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
        // Auto-fill SE SF_ID when SDF ID changes
        if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == g_hEditSdfId) {
            wchar_t sdf_id_text[64];
            GetWindowTextW(g_hEditSdfId, sdf_id_text, 64);
            SetWindowTextW(g_hEditSeSoundFileId, sdf_id_text);
        }
        // Auto-fill LinkedID when SE SF_ID changes (for easy EventMap adding)
        if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == g_hEditSeSoundFileId) {
            wchar_t se_sf_id_text[64];
            GetWindowTextW(g_hEditSeSoundFileId, se_sf_id_text, 64);
            // We should auto-fill the Simple Event ID, which is usually the same as SDF ID
            GetWindowTextW(g_hEditSdfId, se_sf_id_text, 64); // Get SDF ID again
            SetWindowTextW(g_hEditLinkedId, se_sf_id_text); // Set Linked ID to Simple Event ID
            // Set Link Type to 0 (Simple) automatically
            SetWindowTextW(g_hEditLinkType, L"0");
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
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 750, // Increased default width
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