#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <system_error>
#include <iomanip> // For std::setw or other formatting if needed

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// Control IDs
#define IDC_EDIT_PATH   1001
#define IDC_BROWSE      1002
#define IDC_BUTTON_RUN  1003
#define IDC_LOG         1004

HINSTANCE g_hInst;
HWND g_hLog;

// --- Helper Functions ---
// Trim whitespace from std::wstring
std::wstring Trim(const std::wstring& s) {
    size_t start = s.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    size_t end = s.find_last_not_of(L" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Trim whitespace from std::string
std::string TrimA(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Log a line to the edit control
void AppendLog(const std::wstring& msg) {
    if (!g_hLog) return;
    int len = GetWindowTextLengthW(g_hLog);
    SendMessageW(g_hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_hLog, EM_REPLACESEL, 0, (LPARAM)msg.c_str());
    SendMessageW(g_hLog, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
}

// Lowercase file extension
std::wstring LowerExt(const std::filesystem::path& p) {
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext;
}

// Browse for a folder
std::wstring BrowseFolder(HWND hwnd) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Select root folder containing .flo files and 'extracted' subfolders";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::wstring(path);
        }
        CoTaskMemFree(pidl);
    }
    return L"";
}

// Helper to convert std::string (UTF-8) to std::wstring
std::wstring s2ws(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    if (size_needed == 0) return std::wstring(); // Error
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Helper to join a vector of integers into a wstring
std::wstring join_ids(const std::vector<int>& ids) {
    std::wstringstream ss;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) ss << L", ";
        ss << ids[i];
    }
    return ss.str();
}

// --- Data Structures for .flo Parsing ---
struct SoundDataFileEntry {
    int id;
    char type_char; // 'b', 'c', etc.
    std::string xbb_filename;
};

struct SimpleEventEntry {
    int id;
    int sound_data_file_id; // Links to SoundDataFileEntry.id
    int param_set_idx;
    int pan_idx;
};

struct RandomEventChoiceEntry {
    int type; // 0 for simple event
    int simple_event_id;
};
struct RandomEventEntry {
    int id;
    std::vector<RandomEventChoiceEntry> choices;
};

struct CompoundEventComponentEntry {
    int type; // 0 for simple event
    int simple_event_id;
    float delay;
};
struct CompoundEventEntry {
    int id;
    std::vector<CompoundEventComponentEntry> components;
};

struct EventMapEntry {
    std::string section_name; // "Pre", "Pan", "Pos"
    int id_col1;
    int type_col2;
    int event_ref_col3; // This is the ID that links to Simple/Random/Compound events
    std::wstring name_col4;
};

// --- .flo Parsing Logic ---
// Parse .flo: map SoundDataFile_Index to NameString, and gather detailed info
bool ParseFlo(
    const std::filesystem::path& floPath,
    std::map<int, std::wstring>& outNameToRename, // Key: SDF_Index, Value: Name from EventMap
    std::map<int, SoundDataFileEntry>& outSoundDataFiles,
    std::map<int, SimpleEventEntry>& outSimpleEvents,
    std::map<int, RandomEventEntry>& outRandomEvents,
    std::map<int, CompoundEventEntry>& outCompoundEvents,
    std::vector<EventMapEntry>& outEventMapEntriesList)
{
    outNameToRename.clear();
    outSoundDataFiles.clear();
    outSimpleEvents.clear();
    outRandomEvents.clear();
    outCompoundEvents.clear();
    outEventMapEntriesList.clear();

    std::ifstream file_stream(floPath);
    if (!file_stream) {
        AppendLog(L"  Error: Could not open .flo file: " + floPath.wstring());
        return false;
    }

    std::string line;
    std::string current_section_header;
    std::string current_eventmap_subsection;
    int entries_to_read = 0;
    int entries_read_in_section = 0;

    // Temporary maps for resolving during EventMaps parsing
    std::map<int, int> simple_event_to_sdf_map; // SimpleEvent.ID -> SDF.ID

    while (std::getline(file_stream, line)) {
        line = TrimA(line);
        if (line.empty()) continue;

        if (entries_to_read > 0) { // Currently reading entries for a section
            std::stringstream ss(line);
            std::vector<std::string> tokens;
            std::string token;
            while (std::getline(ss, token, ',')) {
                tokens.push_back(TrimA(token));
            }

            try {
                if (current_section_header == "SoundDataFiles" && tokens.size() >= 3) {
                    SoundDataFileEntry sdf_entry;
                    sdf_entry.id = std::stoi(tokens[0]);
                    sdf_entry.type_char = tokens[1].empty() ? ' ' : tokens[1][0];
                    sdf_entry.xbb_filename = tokens[2];
                    outSoundDataFiles[sdf_entry.id] = sdf_entry;
                }
                else if (current_section_header == "SimpleEvents" && tokens.size() >= 4) {
                    SimpleEventEntry se_entry;
                    se_entry.id = std::stoi(tokens[0]);
                    se_entry.sound_data_file_id = std::stoi(tokens[1]);
                    se_entry.param_set_idx = std::stoi(tokens[2]);
                    se_entry.pan_idx = std::stoi(tokens[3]);
                    outSimpleEvents[se_entry.id] = se_entry;
                    simple_event_to_sdf_map[se_entry.id] = se_entry.sound_data_file_id;
                }
                else if (current_section_header == "RandomEvents") {
                    // This section has a sub-structure: ID, NumChoices then NumChoices lines
                    // The current `entries_to_read` logic is for the main ID lines
                    RandomEventEntry re_entry;
                    re_entry.id = std::stoi(tokens[0]);
                    int num_choices = std::stoi(tokens[1]);
                    for (int i = 0; i < num_choices; ++i) {
                        if (!std::getline(file_stream, line)) break;
                        line = TrimA(line);
                        std::stringstream ss_choice(line);
                        std::vector<std::string> choice_tokens;
                        while (std::getline(ss_choice, token, ',')) choice_tokens.push_back(TrimA(token));
                        if (choice_tokens.size() >= 2) {
                            RandomEventChoiceEntry choice;
                            choice.type = std::stoi(choice_tokens[0]);
                            choice.simple_event_id = std::stoi(choice_tokens[1]);
                            re_entry.choices.push_back(choice);
                        }
                    }
                    outRandomEvents[re_entry.id] = re_entry;
                }
                else if (current_section_header == "CompoundEvents") {
                    CompoundEventEntry ce_entry;
                    ce_entry.id = std::stoi(tokens[0]);
                    int num_components = std::stoi(tokens[1]);
                    for (int i = 0; i < num_components; ++i) {
                        if (!std::getline(file_stream, line)) break;
                        line = TrimA(line);
                        std::stringstream ss_comp(line);
                        std::vector<std::string> comp_tokens;
                        while (std::getline(ss_comp, token, ',')) comp_tokens.push_back(TrimA(token));
                        if (comp_tokens.size() >= 3) {
                            CompoundEventComponentEntry component;
                            component.type = std::stoi(comp_tokens[0]);
                            component.simple_event_id = std::stoi(comp_tokens[1]);
                            component.delay = std::stof(comp_tokens[2]);
                            ce_entry.components.push_back(component);
                        }
                    }
                    outCompoundEvents[ce_entry.id] = ce_entry;
                }
                else if (current_section_header == "EventMaps" && !current_eventmap_subsection.empty() && tokens.size() >= 4) {
                    EventMapEntry em_entry;
                    em_entry.section_name = current_eventmap_subsection;
                    em_entry.id_col1 = std::stoi(tokens[0]);
                    em_entry.type_col2 = std::stoi(tokens[1]);
                    em_entry.event_ref_col3 = std::stoi(tokens[2]);

                    std::string name_str_utf8 = tokens[3];
                    size_t star_pos = name_str_utf8.find('*');
                    if (star_pos != std::string::npos) {
                        name_str_utf8 = name_str_utf8.substr(star_pos + 1);
                    }
                    em_entry.name_col4 = s2ws(name_str_utf8);
                    outEventMapEntriesList.push_back(em_entry);
                }
            }
            catch (const std::exception& e) {
                AppendLog(L"  Error parsing line in section " + s2ws(current_section_header) + L": " + s2ws(line) + L" - " + s2ws(e.what()));
            }
            entries_read_in_section++;
            if (entries_read_in_section >= entries_to_read) {
                entries_to_read = 0; // Finished reading entries for this count
                if (current_section_header != "EventMaps") current_section_header = ""; // Reset unless in EventMaps sub-parsing
            }
        }
        else { // Looking for a section header or count
            if (line == "SoundDataFiles" || line == "SimpleEvents" || line == "RandomEvents" ||
                line == "CompoundEvents" || line == "EventMaps" ||
                line == "SoundDataBundles" || line == "SoundParameterSets" || // Other sections to skip for now
                line == "Pre" || line == "Pan" || line == "Pos") { // EventMaps subsections

                if (line == "Pre" || line == "Pan" || line == "Pos") { // EventMaps subsections
                    if (current_section_header == "EventMaps") {
                        current_eventmap_subsection = line;
                    }
                    else {
                        // This is unexpected, might be a format variation or parse error
                        AppendLog(L"  Warning: Found EventMaps subsection '" + s2ws(line) + L"' without main 'EventMaps' header active.");
                        current_eventmap_subsection = ""; // Reset
                        current_section_header = ""; // Reset
                        continue;
                    }
                }
                else { // Main section header
                    current_section_header = line;
                    current_eventmap_subsection = ""; // Reset subsection if moving to a new main section
                }

                // Read the next line for count
                if (std::getline(file_stream, line)) {
                    line = TrimA(line);
                    try {
                        entries_to_read = std::stoi(line);
                        entries_read_in_section = 0;
                        if (entries_to_read == 0 && (current_section_header == "EventMaps" && (current_eventmap_subsection == "Pan" || current_eventmap_subsection == "Pos"))) {
                            // Pan/Pos might have 0 entries and still be valid subsections
                        }
                        else if (entries_to_read == 0) {
                            current_section_header = ""; // Empty section, reset
                        }
                    }
                    catch (const std::exception&) {
                        AppendLog(L"  Error: Expected count after header '" + s2ws(current_section_header) + L"', but got: " + s2ws(line));
                        entries_to_read = 0;
                        current_section_header = "";
                    }
                }
                else { // EOF after header
                    break;
                }
            }
        }
    }
    file_stream.close();

    // Post-parsing: Build the outNameToRename map (SDF_Index -> NameString)
    for (const auto& em_entry : outEventMapEntriesList) {
        int event_ref = em_entry.event_ref_col3;
        const std::wstring& name_str = em_entry.name_col4;
        int sdf_idx_to_rename = -1;

        // Try event_ref as SimpleEvent ID
        auto it_se_map = simple_event_to_sdf_map.find(event_ref);
        if (it_se_map != simple_event_to_sdf_map.end()) {
            sdf_idx_to_rename = it_se_map->second;
        }
        else {
            // Try event_ref as RandomEvent ID (resolve to first SimpleEvent's SDF)
            auto it_re = outRandomEvents.find(event_ref);
            if (it_re != outRandomEvents.end() && !it_re->second.choices.empty()) {
                int first_se_id_in_re = it_re->second.choices[0].simple_event_id;
                auto it_se_map_in_re = simple_event_to_sdf_map.find(first_se_id_in_re);
                if (it_se_map_in_re != simple_event_to_sdf_map.end()) {
                    sdf_idx_to_rename = it_se_map_in_re->second;
                }
            }
            else {
                // Try event_ref as CompoundEvent ID (resolve to first SimpleEvent's SDF)
                auto it_ce = outCompoundEvents.find(event_ref);
                if (it_ce != outCompoundEvents.end() && !it_ce->second.components.empty()) {
                    int first_se_id_in_ce = it_ce->second.components[0].simple_event_id;
                    auto it_se_map_in_ce = simple_event_to_sdf_map.find(first_se_id_in_ce);
                    if (it_se_map_in_ce != simple_event_to_sdf_map.end()) {
                        sdf_idx_to_rename = it_se_map_in_ce->second;
                    }
                }
                // Fallback: if event_ref itself is an SDF_ID (matches original code's implicit behavior if SimpleEvents weren't considered)
                // This is less likely to be correct given the Gladius example (e.g. EventMap ref 56 -> SE 56 -> SDF 32)
                // else if (outSoundDataFiles.count(event_ref)) {
                //    sdf_idx_to_rename = event_ref; 
                // }
            }
        }

        if (sdf_idx_to_rename != -1) {
            if (outNameToRename.count(sdf_idx_to_rename)) {
                // Potentially log this as a warning: multiple EventMap entries might resolve to naming the same SDF_Index.
                // The current behavior is to overwrite, last one wins.
                // AppendLog(L"  WARNING: SoundDataFile ID " + std::to_wstring(sdf_idx_to_rename) +
                //           L" already mapped to '" + outNameToRename[sdf_idx_to_rename] +
                //           L"'. Remapping to '" + name_str + L"' from EventMap (section " +
                //           s2ws(em_entry.section_name) + L", ID1 " + std::to_wstring(em_entry.id_col1) +L").");
            }
            outNameToRename[sdf_idx_to_rename] = name_str;
        }
        else {
            AppendLog(L"  Warning: EventMap entry (section " + s2ws(em_entry.section_name) + L", ID1 " +
                std::to_wstring(em_entry.id_col1) + L", Name '" + name_str +
                L"') refers to event " + std::to_wstring(event_ref) +
                L", which could not be resolved to a SoundDataFile ID for renaming.");
        }
    }
    return true;
}


// Main processing function
void ProcessFolder(const std::wstring& rootPath) {
    namespace fs = std::filesystem;
    AppendLog(L"Starting scan of " + rootPath);

    std::error_code ec;
    fs::directory_options scan_options = fs::directory_options::skip_permission_denied;

    for (auto const& dir_entry : fs::recursive_directory_iterator(rootPath, scan_options, ec)) {
        if (ec) {
            AppendLog(L"  Error accessing " + dir_entry.path().wstring() + L": " + s2ws(ec.message()));
            ec.clear();
            continue;
        }

        if (dir_entry.is_regular_file() && LowerExt(dir_entry.path()) == L".flo") {
            const fs::path& floPath = dir_entry.path();
            AppendLog(L"Processing .flo: " + floPath.filename().wstring());

            fs::path extractedDir = floPath.parent_path() / L"extracted";
            if (!fs::exists(extractedDir) || !fs::is_directory(extractedDir)) {
                AppendLog(L"  No 'extracted' folder found at: " + extractedDir.wstring() + L", skipping this .flo.");
                continue;
            }

            std::map<int, std::wstring> nameMapForRenaming; // SDF_Index -> NameString
            std::map<int, SoundDataFileEntry> soundDataFiles;
            std::map<int, SimpleEventEntry> simpleEvents;
            std::map<int, RandomEventEntry> randomEvents;
            std::map<int, CompoundEventEntry> compoundEvents;
            std::vector<EventMapEntry> eventMapEntriesList;

            if (!ParseFlo(floPath, nameMapForRenaming, soundDataFiles, simpleEvents, randomEvents, compoundEvents, eventMapEntriesList)) {
                AppendLog(L"  Failed to parse .flo file: " + floPath.wstring());
                continue;
            }
            AppendLog(L"  .flo parsing complete. Mappings for renaming: " + std::to_wstring(nameMapForRenaming.size()));

            // --- Log Event Name Duplication ---
            std::map<std::wstring, std::vector<int>> eventNameReferencingIDs; // Name_col4 -> list of event_ref_col3
            for (const auto& em_entry : eventMapEntriesList) {
                eventNameReferencingIDs[em_entry.name_col4].push_back(em_entry.event_ref_col3);
            }
            AppendLog(L"  --- Event Name Linkage ---");
            for (const auto& pair : eventNameReferencingIDs) {
                if (pair.second.size() > 1) {
                    AppendLog(L"    Sound Name '" + pair.first + L"' is used by " + std::to_wstring(pair.second.size()) +
                        L" EventMap entries (referencing event IDs: " + join_ids(pair.second) + L").");
                }
            }

            // --- Deeper Analysis Logging ---
            AppendLog(L"  --- Detailed .flo Analysis ---");
            AppendLog(L"    SoundDataFiles parsed: " + std::to_wstring(soundDataFiles.size()));
            AppendLog(L"    SimpleEvents parsed: " + std::to_wstring(simpleEvents.size()));
            AppendLog(L"    RandomEvents parsed: " + std::to_wstring(randomEvents.size()));
            AppendLog(L"    CompoundEvents parsed: " + std::to_wstring(compoundEvents.size()));
            AppendLog(L"    EventMap Entries parsed: " + std::to_wstring(eventMapEntriesList.size()));

            AppendLog(L"  --- Structure Connections ---");
            for (const auto& em_entry : eventMapEntriesList) {
                AppendLog(L"    EventMap [" + s2ws(em_entry.section_name) + L"] (ID1:" + std::to_wstring(em_entry.id_col1) +
                    L", Type:" + std::to_wstring(em_entry.type_col2) + L", Name:'" + em_entry.name_col4 +
                    L"') refers to Event ID: " + std::to_wstring(em_entry.event_ref_col3));

                int event_ref = em_entry.event_ref_col3;
                auto it_se = simpleEvents.find(event_ref);
                if (it_se != simpleEvents.end()) {
                    AppendLog(L"      -> Links to SimpleEvent ID " + std::to_wstring(it_se->first) +
                        L", which uses SoundDataFile ID " + std::to_wstring(it_se->second.sound_data_file_id));
                    auto it_sdf = soundDataFiles.find(it_se->second.sound_data_file_id);
                    if (it_sdf != soundDataFiles.end()) {
                        AppendLog(L"        -> SoundDataFile: '" + s2ws(it_sdf->second.xbb_filename) + L"'");
                    }
                    else {
                        AppendLog(L"        -> SoundDataFile ID " + std::to_wstring(it_se->second.sound_data_file_id) + L" not found in SoundDataFiles list.");
                    }
                }
                else {
                    auto it_re = randomEvents.find(event_ref);
                    if (it_re != randomEvents.end()) {
                        AppendLog(L"      -> Links to RandomEvent ID " + std::to_wstring(it_re->first) +
                            L" with " + std::to_wstring(it_re->second.choices.size()) + L" choices:");
                        for (const auto& choice : it_re->second.choices) {
                            AppendLog(L"        - Choice: Type " + std::to_wstring(choice.type) + L", SimpleEvent ID " + std::to_wstring(choice.simple_event_id));
                            auto it_se_choice = simpleEvents.find(choice.simple_event_id);
                            if (it_se_choice != simpleEvents.end()) {
                                AppendLog(L"          -> Uses SoundDataFile ID " + std::to_wstring(it_se_choice->second.sound_data_file_id));
                                auto it_sdf_choice = soundDataFiles.find(it_se_choice->second.sound_data_file_id);
                                if (it_sdf_choice != soundDataFiles.end()) {
                                    AppendLog(L"            -> SoundDataFile: '" + s2ws(it_sdf_choice->second.xbb_filename) + L"'");
                                }
                            }
                            else {
                                AppendLog(L"          -> SimpleEvent ID " + std::to_wstring(choice.simple_event_id) + L" not found.");
                            }
                        }
                    }
                    else {
                        auto it_ce = compoundEvents.find(event_ref);
                        if (it_ce != compoundEvents.end()) {
                            AppendLog(L"      -> Links to CompoundEvent ID " + std::to_wstring(it_ce->first) +
                                L" with " + std::to_wstring(it_ce->second.components.size()) + L" components:");
                            for (const auto& comp : it_ce->second.components) {
                                AppendLog(L"        - Component: Type " + std::to_wstring(comp.type) + L", SimpleEvent ID " + std::to_wstring(comp.simple_event_id) + L", Delay " + std::to_wstring(comp.delay));
                                auto it_se_comp = simpleEvents.find(comp.simple_event_id);
                                if (it_se_comp != simpleEvents.end()) {
                                    AppendLog(L"          -> Uses SoundDataFile ID " + std::to_wstring(it_se_comp->second.sound_data_file_id));
                                    auto it_sdf_comp = soundDataFiles.find(it_se_comp->second.sound_data_file_id);
                                    if (it_sdf_comp != soundDataFiles.end()) {
                                        AppendLog(L"            -> SoundDataFile: '" + s2ws(it_sdf_comp->second.xbb_filename) + L"'");
                                    }
                                }
                                else {
                                    AppendLog(L"          -> SimpleEvent ID " + std::to_wstring(comp.simple_event_id) + L" not found.");
                                }
                            }
                        }
                        else {
                            AppendLog(L"      -> Event ID " + std::to_wstring(event_ref) + L" not found in Simple, Random, or Compound events lists.");
                        }
                    }
                }
            }
            AppendLog(L"  --- Renaming WAV files in " + extractedDir.wstring() + L" ---");
            int files_renamed = 0;
            int rename_errors = 0;
            std::map<std::wstring, int> renamed_file_collision_check; // Target_Name -> Original_SDF_ID

            for (auto& wav_file_entry : fs::directory_iterator(extractedDir, ec)) {
                if (ec) { ec.clear(); continue; }
                if (!wav_file_entry.is_regular_file() || LowerExt(wav_file_entry.path()) != L".wav") continue;

                std::wstring stem = Trim(wav_file_entry.path().stem().wstring());
                std::wstring stem_numeric_part = stem;

                // Handle "track_N" format
                if (stem.rfind(L"track_", 0) == 0) { // starts with track_
                    stem_numeric_part = stem.substr(6);
                }

                int sdf_idx = -1;
                if (std::all_of(stem_numeric_part.begin(), stem_numeric_part.end(), ::iswdigit) && !stem_numeric_part.empty()) {
                    try {
                        sdf_idx = std::stoi(stem_numeric_part);
                    }
                    catch (const std::out_of_range&) {
                        AppendLog(L"  Warning: WAV stem number too large: " + stem_numeric_part);
                        continue;
                    }
                    catch (const std::invalid_argument&) {
                        AppendLog(L"  Warning: WAV stem is not a valid number: " + stem_numeric_part);
                        continue;
                    }
                }
                else {
                    AppendLog(L"  Skipping WAV with non-numeric/non-track_N stem: " + wav_file_entry.path().filename().wstring());
                    continue;
                }

                if (sdf_idx < 0) continue;

                auto itMap = nameMapForRenaming.find(sdf_idx);
                if (itMap != nameMapForRenaming.end()) {
                    std::wstring newBaseName = itMap->second;
                    // Sanitize newBaseName for filesystem (remove invalid chars like '*' more robustly if needed)
                    std::replace(newBaseName.begin(), newBaseName.end(), L'*', L'_');
                    std::replace(newBaseName.begin(), newBaseName.end(), L'/', L'_');
                    std::replace(newBaseName.begin(), newBaseName.end(), L'\\', L'_');
                    // Could add more sanitization for other invalid filename characters.

                    fs::path newPath = wav_file_entry.path().parent_path() / (newBaseName + L".wav");

                    // Collision detection for target filename
                    if (renamed_file_collision_check.count(newPath.filename().wstring())) {
                        AppendLog(L"  Error: Rename collision for target '" + newPath.filename().wstring() +
                            L"'. Original file '" + wav_file_entry.path().filename().wstring() + L"' (SDF ID " + std::to_wstring(sdf_idx) +
                            L") and previous file (SDF ID " + std::to_wstring(renamed_file_collision_check[newPath.filename().wstring()]) +
                            L") map to the same name. Skipping rename for " + wav_file_entry.path().filename().wstring());
                        rename_errors++;
                        continue;
                    }
                    if (fs::exists(newPath) && newPath != wav_file_entry.path()) {
                        AppendLog(L"  Error: Target file '" + newPath.filename().wstring() + L"' already exists. Skipping rename for " + wav_file_entry.path().filename().wstring());
                        rename_errors++;
                        continue;
                    }


                    std::error_code rename_ec;
                    fs::rename(wav_file_entry.path(), newPath, rename_ec);
                    if (rename_ec) {
                        AppendLog(L"  Error renaming " + wav_file_entry.path().filename().wstring() + L" to " + newPath.filename().wstring() + L": " + s2ws(rename_ec.message()));
                        rename_errors++;
                    }
                    else {
                        AppendLog(L"  Renamed " + wav_file_entry.path().filename().wstring() + L" to " + newPath.filename().wstring());
                        files_renamed++;
                        renamed_file_collision_check[newPath.filename().wstring()] = sdf_idx;
                    }
                }
                else {
                    // AppendLog(L"  No rename rule for SDF ID " + std::to_wstring(sdf_idx) + L" (from " + wav_file_entry.path().filename().wstring() + L")");
                }
            }
            AppendLog(L"  Finished processing " + floPath.filename().wstring() + L". Renamed: " + std::to_wstring(files_renamed) + L", Errors: " + std::to_wstring(rename_errors));
        }
    }
    AppendLog(L"Processing complete for all folders.");
}


// Window proc
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
    {
        CreateWindowW(L"STATIC", L"Root Folder:", WS_CHILD | WS_VISIBLE, 10, 12, 80, 20, hwnd, NULL, g_hInst, NULL);
        CreateWindowW(L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY,
            90, 10, 320, 25, hwnd, (HMENU)IDC_EDIT_PATH, g_hInst, NULL);
        CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 420, 10, 80, 25, hwnd, (HMENU)IDC_BROWSE, g_hInst, NULL);
        CreateWindowW(L"BUTTON", L"Rename", WS_CHILD | WS_VISIBLE, 510, 10, 80, 25, hwnd, (HMENU)IDC_BUTTON_RUN, g_hInst, NULL);

        g_hLog = CreateWindowW(L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
            10, 45, 580, 300, hwnd, (HMENU)IDC_LOG, g_hInst, NULL);
        SendMessageW(g_hLog, EM_LIMITTEXT, 0, 0); // Effectively unlimited within reason for an EDIT control
    }
    break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BROWSE) {
            std::wstring p = BrowseFolder(hwnd);
            if (!p.empty()) SetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_PATH), p.c_str());
        }
        else if (LOWORD(wParam) == IDC_BUTTON_RUN) {
            wchar_t buf[MAX_PATH]; GetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_PATH), buf, MAX_PATH);
            std::wstring root(buf);
            if (root.empty()) {
                MessageBoxW(hwnd, L"Select a root folder first.", L"Error", MB_OK | MB_ICONERROR);
            }
            else if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
                MessageBoxW(hwnd, L"Selected path is not a valid directory.", L"Error", MB_OK | MB_ICONERROR);
            }
            else {
                SetWindowTextW(g_hLog, L""); // Clear log
                ProcessFolder(root);
            }
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

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPWSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInst;
    if (FAILED(OleInitialize(NULL))) {
        MessageBoxW(NULL, L"OleInitialize failed.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WavRenameGladiusWindowClass";
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,                              // Optional window styles.
        wc.lpszClassName,               // Window class
        L"Gladius WAV Renamer & Analyzer", // Window text
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, // Window style
        CW_USEDEFAULT, CW_USEDEFAULT,   // Position
        620, 400,                       // Size (width, height)
        NULL,                           // Parent window    
        NULL,                           // Menu
        hInst,                          // Instance handle
        NULL                            // Additional application data
    );

    if (hwnd == NULL) {
        MessageBoxW(NULL, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    OleUninitialize();
    return (int)msg.wParam;
}