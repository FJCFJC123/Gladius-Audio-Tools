// GladiusAudioTool_Complete_v4_Batch.cpp
// An all-in-one tool for extracting, analyzing, renaming, and repacking Gladius audio files.
// This version implements a fully recursive, unified batch processing system.

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem> // Requires C++17
#include <fstream>
#include <sstream>
#include <algorithm>
#include <system_error>
#include <cwctype>
#include <iomanip>
#include <cstdint>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")

namespace fs = std::filesystem;

// --- UI Control IDs ---
#define IDC_EDIT_PATH               1001
#define IDC_BROWSE                  1002
#define ID_BUTTON_EXTRACT_RENAME    1003
#define ID_BUTTON_REPACK            1004
#define IDC_LOG                     1005

// --- Global Variables ---
HINSTANCE g_hInst = nullptr;
HWND g_hLog = nullptr;

// =================================================================================
// SECTION: HELPER FUNCTIONS
// =================================================================================

static void AppendLog(const std::wstring& msg) {
    if (!g_hLog) return;
    int len = GetWindowTextLengthW(g_hLog);
    SendMessageW(g_hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_hLog, EM_REPLACESEL, 0, (LPARAM)msg.c_str());
    SendMessageW(g_hLog, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
}

static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::string TrimA(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::wstring s2ws(const std::string& str) {
    if (str.empty()) return L"";
    int need = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring out(need, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0], need);
    return out;
}

static std::wstring BrowseForFolderModern(HWND hwnd, const wchar_t* title) {
    std::wstring result_path;
    IFileOpenDialog* pfd = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        hr = pfd->GetOptions(&dwOptions);
        if (SUCCEEDED(hr)) {
            hr = pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        if (SUCCEEDED(hr)) {
            hr = pfd->SetTitle(title);
        }
        if (SUCCEEDED(hr)) {
            hr = pfd->Show(hwnd);
            if (SUCCEEDED(hr)) {
                IShellItem* psiResult;
                hr = pfd->GetResult(&psiResult);
                if (SUCCEEDED(hr)) {
                    PWSTR pszFilePath = NULL;
                    hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                    if (SUCCEEDED(hr)) {
                        result_path = pszFilePath;
                        CoTaskMemFree(pszFilePath);
                    }
                    psiResult->Release();
                }
            }
        }
        pfd->Release();
    }
    return result_path;
}

static std::wstring LowerExt(const fs::path& p) {
    std::wstring e = p.extension().wstring();
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    return e;
}

static int find_fourcc(const uint8_t* buf, size_t len, const char fourcc[4]) {
    if (len < 4) return -1;
    for (size_t i = 0; i + 4 <= len; ++i) {
        if (buf[i + 0] == (uint8_t)fourcc[0] && buf[i + 1] == (uint8_t)fourcc[1] &&
            buf[i + 2] == (uint8_t)fourcc[2] && buf[i + 3] == (uint8_t)fourcc[3]) {
            return (int)i;
        }
    }
    return -1;
}

static bool patch_wav_lengths(std::vector<uint8_t>& wavHeader, uint32_t dataLen) {
    if (wavHeader.size() < 12) return false;
    if (memcmp(&wavHeader[0], "RIFF", 4) != 0 || memcmp(&wavHeader[8], "WAVE", 4) != 0)
        return false;
    int dataPos = find_fourcc(wavHeader.data(), wavHeader.size(), "data");
    if (dataPos < 0 || dataPos + 8 >(int)wavHeader.size()) return false;
    memcpy(&wavHeader[dataPos + 4], &dataLen, 4);
    uint32_t riffSize = (uint32_t)(wavHeader.size() - 8);
    memcpy(&wavHeader[4], &riffSize, 4);
    return true;
}

static inline std::wstring CsvEscape(const std::wstring& s) {
    std::wstring out = s;
    size_t p = 0;
    while ((p = out.find(L"\"", p)) != std::wstring::npos) { out.replace(p, 1, L"\"\""); p += 2; }
    if (out.find_first_of(L",\"\n\r") != std::wstring::npos) return L"\"" + out + L"\"";
    return out;
}

// =================================================================================
// SECTION: .FLO PARSING & ANALYSIS LOGIC
// =================================================================================

struct SoundDataFileEntry { int id{}; char type_char{}; std::string xbb_filename; };
struct SimpleEventEntry { int id{}; int sound_data_file_id{}; int param_set_idx{}; int pan_idx{}; };
struct RandomEventEntry { int id{}; std::vector<int> choices_simple; std::vector<int> choices_sdf; };
struct CompoundEventEntry { int id{}; struct Comp { int simple_event_id{ -1 }; int sdf_id{ -1 }; float delay{ 0.0f }; }; std::vector<Comp> components; };
struct EventMapEntry { std::string section_name; int id_col1{}; int type_col2{}; int event_ref_col3{}; std::wstring name_col4; };
struct SPSRow { int id{}; std::vector<int> fields; };
struct SoundParameterSets { std::vector<SPSRow> pre, pan, pos; };
struct LinkProvenance { int eventmap_id{ -1 }; std::wstring section; int domain{ -1 }; int link_id{ -1 }; std::wstring global_name; int via_simple_id{ -1 }; int via_sdf_id{ -1 }; int appearance_idx{ -1 }; int expansion_size{ -1 }; };

static bool ParseFlo(const fs::path& floPath, std::map<int, SoundDataFileEntry>& soundDataFiles, std::map<int, SimpleEventEntry>& simpleEvents, std::map<int, RandomEventEntry>& randomEvents, std::map<int, CompoundEventEntry>& compoundEvents, std::vector<EventMapEntry>& eventMaps, SoundParameterSets& sps_out) {
    soundDataFiles.clear(); simpleEvents.clear(); randomEvents.clear(); compoundEvents.clear(); eventMaps.clear(); sps_out = {};
    std::ifstream in(floPath);
    if (!in) { AppendLog(L"  Error: Could not open .flo file: " + floPath.wstring()); return false; }
    std::string line, section, subSection; int entries_to_read = 0, read_in_this_block = 0;
    auto parse_int_list = [](const std::string& l) { std::vector<int> vals; std::stringstream ss(l); std::string tok; while (std::getline(ss, tok, ',')) { tok = TrimA(tok); if (!tok.empty()) { try { vals.push_back(std::stoi(tok)); } catch (...) {} } } return vals; };
    auto read_count_line = [&](int& outCount)->bool { std::streampos pos = in.tellg(); std::string cnt; if (!std::getline(in, cnt)) return false; cnt = TrimA(cnt); if (cnt.empty()) { if (!std::getline(in, cnt)) return false; cnt = TrimA(cnt); } try { outCount = std::stoi(cnt); return true; } catch (...) { in.clear(); in.seekg(pos); return false; } };
    while (std::getline(in, line)) {
        line = TrimA(line); if (line.empty()) continue;
        if (entries_to_read > 0) {
            if (section == "SoundDataFiles") { std::stringstream ss(line); std::string t0, t1, t2; std::getline(ss, t0, ','); std::getline(ss, t1, ','); std::getline(ss, t2); t0 = TrimA(t0); t1 = TrimA(t1); t2 = TrimA(t2); if (!t0.empty() && !t1.empty() && !t2.empty()) { SoundDataFileEntry e{}; e.id = std::stoi(t0); e.type_char = t1.empty() ? ' ' : t1[0]; e.xbb_filename = t2; soundDataFiles[e.id] = e; } }
            else if (section == "SimpleEvents") { auto v = parse_int_list(line); if (v.size() >= 4) { SimpleEventEntry e{ v[0],v[1],v[2],v[3] }; simpleEvents[e.id] = e; } }
            else if (section == "RandomEvents") { auto head = parse_int_list(line); if (head.size() >= 2) { RandomEventEntry re{}; re.id = head[0]; int n = head[1]; for (int i = 0; i < n; ++i) { std::string ch; if (!std::getline(in, ch)) break; ch = TrimA(ch); auto vals = parse_int_list(ch); if (vals.empty()) continue; if (vals.size() == 1) { re.choices_sdf.push_back(vals[0]); } else { re.choices_simple.push_back(vals[1]); } } randomEvents[re.id] = std::move(re); } else if (head.size() == 1) { RandomEventEntry re{}; re.id = head[0]; std::string next; if (!std::getline(in, next)) { randomEvents[re.id] = re; continue; } next = TrimA(next); int n = 0; try { n = std::stoi(next); } catch (...) { n = 0; } for (int i = 0; i < n; ++i) { std::string ch; if (!std::getline(in, ch)) break; ch = TrimA(ch); auto vals = parse_int_list(ch); if (vals.empty()) continue; if (vals.size() == 1) re.choices_sdf.push_back(vals[0]); else re.choices_simple.push_back(vals[1]); } randomEvents[re.id] = std::move(re); } }
            else if (section == "CompoundEvents") { auto head = parse_int_list(line); if (head.size() >= 2) { CompoundEventEntry ce{}; ce.id = head[0]; int n = head[1]; for (int i = 0; i < n; ++i) { std::string comp; if (!std::getline(in, comp)) break; comp = TrimA(comp); auto vals = parse_int_list(comp); if (vals.empty()) continue; CompoundEventEntry::Comp c{}; if (vals.size() == 1) { c.sdf_id = vals[0]; } else { c.simple_event_id = vals[1]; if (vals.size() >= 3) c.delay = (float)vals[2]; } ce.components.push_back(c); } compoundEvents[ce.id] = std::move(ce); } else if (head.size() == 1) { CompoundEventEntry ce{}; ce.id = head[0]; std::string next; if (!std::getline(in, next)) { compoundEvents[ce.id] = ce; continue; } next = TrimA(next); int n = 0; try { n = std::stoi(next); } catch (...) { n = 0; } for (int i = 0; i < n; ++i) { std::string comp; if (!std::getline(in, comp)) break; comp = TrimA(comp); auto vals = parse_int_list(comp); if (vals.empty()) continue; CompoundEventEntry::Comp c{}; if (vals.size() == 1) { c.sdf_id = vals[0]; } else { c.simple_event_id = vals[1]; if (vals.size() >= 3) c.delay = (float)vals[2]; } ce.components.push_back(c); } compoundEvents[ce.id] = std::move(ce); } }
            else if (section == "SoundParameterSets") { auto vals = parse_int_list(line); if (!vals.empty()) { SPSRow row{}; row.id = vals[0]; if (vals.size() > 1) row.fields.assign(vals.begin() + 1, vals.end()); if (subSection == "Pre") sps_out.pre.push_back(row); else if (subSection == "Pan") sps_out.pan.push_back(row); else if (subSection == "Pos") sps_out.pos.push_back(row); } }
            else if (section == "EventMaps") { std::stringstream ss(line); std::string t0, t1, t2, t3; std::getline(ss, t0, ','); std::getline(ss, t1, ','); std::getline(ss, t2, ','); std::getline(ss, t3); t0 = TrimA(t0); t1 = TrimA(t1); t2 = TrimA(t2); t3 = TrimA(t3); if (!t0.empty() && !t1.empty() && !t2.empty() && !t3.empty()) { EventMapEntry em{}; em.section_name = subSection; em.id_col1 = std::stoi(t0); em.type_col2 = std::stoi(t1); em.event_ref_col3 = std::stoi(t2); size_t star = t3.find('*'); if (star != std::string::npos) t3 = t3.substr(star + 1); em.name_col4 = s2ws(t3); eventMaps.push_back(std::move(em)); } }
            if (++read_in_this_block >= entries_to_read) { entries_to_read = 0; read_in_this_block = 0; if (section != "EventMaps" && section != "SoundParameterSets") section.clear(); subSection.clear(); } continue;
        }
        if (line == "SoundDataFiles" || line == "SimpleEvents" || line == "RandomEvents" || line == "CompoundEvents" || line == "SoundParameterSets" || line == "EventMaps" || line == "Pre" || line == "Pan" || line == "Pos") {
            if (line == "Pre" || line == "Pan" || line == "Pos") { if (section == "EventMaps" || section == "SoundParameterSets") { subSection = line; int cnt = 0; if (!read_count_line(cnt)) { AppendLog(L"  Error: Expected count after '" + s2ws(section + " / " + subSection) + L"'"); section.clear(); subSection.clear(); continue; } entries_to_read = cnt; read_in_this_block = 0; } else { AppendLog(L"  Warning: Found subsection '" + s2ws(line) + L"' without active parent header."); subSection.clear(); } }
            else { section = line; subSection.clear(); if (section == "EventMaps" || section == "SoundParameterSets") continue; std::string cnt; if (!std::getline(in, cnt)) break; cnt = TrimA(cnt); try { entries_to_read = std::stoi(cnt); read_in_this_block = 0; } catch (...) { AppendLog(L"  Error: Expected count after header '" + s2ws(section) + L"', but got: " + s2ws(cnt)); section.clear(); entries_to_read = 0; } }
        }
    }
    in.close(); return true;
}

enum { DOMAIN_SIMPLE = 0, DOMAIN_RANDOM = 1, DOMAIN_COMPOUND = 2 };
static void ExpandRandomToSdf(const RandomEventEntry* re, const std::map<int, int>& simple_to_sdf, std::vector<int>& out_sdf, std::vector<int>* out_simple = nullptr) { if (!re) return; if (!re->choices_simple.empty()) { for (int se : re->choices_simple) { auto it = simple_to_sdf.find(se); if (it != simple_to_sdf.end()) { out_sdf.push_back(it->second); if (out_simple) out_simple->push_back(se); } } } else if (!re->choices_sdf.empty()) { out_sdf.insert(out_sdf.end(), re->choices_sdf.begin(), re->choices_sdf.end()); } }
static void ExpandCompoundToSdf(const CompoundEventEntry* ce, const std::map<int, int>& simple_to_sdf, std::vector<int>& out_sdf, std::vector<int>* out_simple = nullptr) { if (!ce) return; for (const auto& c : ce->components) { if (c.simple_event_id >= 0) { auto it = simple_to_sdf.find(c.simple_event_id); if (it != simple_to_sdf.end()) { out_sdf.push_back(it->second); if (out_simple) out_simple->push_back(c.simple_event_id); } } else if (c.sdf_id >= 0) { out_sdf.push_back(c.sdf_id); } } }
static int ExpandRefWithFallback(int raw_type, int link_id, const std::map<int, SimpleEventEntry>& simpleEvents, const std::map<int, RandomEventEntry>& randomEvents, const std::map<int, CompoundEventEntry>& compoundEvents, const std::map<int, int>& simple_to_sdf, std::vector<int>& out_sdf_ids, std::vector<int>& out_via_simple) { int domain = (raw_type == 0) ? DOMAIN_SIMPLE : (raw_type == 1) ? DOMAIN_RANDOM : DOMAIN_COMPOUND; auto try_expand = [&](int dom)->bool { out_sdf_ids.clear(); out_via_simple.clear(); if (dom == DOMAIN_SIMPLE) { auto it = simpleEvents.find(link_id); if (it != simpleEvents.end()) { auto it2 = simple_to_sdf.find(link_id); if (it2 != simple_to_sdf.end()) { out_sdf_ids.push_back(it2->second); out_via_simple.push_back(link_id); } } } else if (dom == DOMAIN_RANDOM) { auto it = randomEvents.find(link_id); if (it != randomEvents.end()) ExpandRandomToSdf(&it->second, simple_to_sdf, out_sdf_ids, &out_via_simple); } else if (dom == DOMAIN_COMPOUND) { auto it = compoundEvents.find(link_id); if (it != compoundEvents.end()) ExpandCompoundToSdf(&it->second, simple_to_sdf, out_sdf_ids, &out_via_simple); } std::sort(out_sdf_ids.begin(), out_sdf_ids.end()); out_sdf_ids.erase(std::unique(out_sdf_ids.begin(), out_sdf_ids.end()), out_sdf_ids.end()); std::sort(out_via_simple.begin(), out_via_simple.end()); out_via_simple.erase(std::unique(out_via_simple.begin(), out_via_simple.end()), out_via_simple.end()); return !out_sdf_ids.empty(); }; if (try_expand(domain)) return domain; if (raw_type == 1 || raw_type == 2) { int swapped = (domain == DOMAIN_RANDOM) ? DOMAIN_COMPOUND : DOMAIN_RANDOM; if (try_expand(swapped)) { std::wstringstream ss; ss << L"  Warning: EventMap row (type=" << raw_type << L", ref=" << link_id << L") needed fallback to " << (swapped == DOMAIN_RANDOM ? L"RANDOM" : L"COMPOUND"); AppendLog(ss.str()); return swapped; } } return domain; }
static int SectionPriority(const std::string& s) { if (s == "Pos") return 0; if (s == "Pre") return 1; if (s == "Pan") return 2; return 3; }
static int DomainPriority(int d) { if (d == DOMAIN_SIMPLE) return 0; if (d == DOMAIN_RANDOM) return 1; if (d == DOMAIN_COMPOUND) return 2; return 3; }

// =================================================================================
// SECTION: CORE TOOL FUNCTIONS
// =================================================================================

// **NEW**: Batch extraction function that takes a path and doesn't prompt the user.
void BatchExtractAll(const std::wstring& rootPath) {
    AppendLog(L"--- Starting Recursive Batch Audio Extraction ---");
    AppendLog(L"Scanning for .xbb files in: " + rootPath);
    int xbb_files_found = 0;
    for (const auto& entry : fs::recursive_directory_iterator(rootPath)) {
        if (!entry.is_regular_file() || LowerExt(entry.path()) != L".xbb") continue;
        xbb_files_found++;
        const fs::path xbbPath = entry.path();
        fs::path xsbPath = xbbPath; xsbPath.replace_extension(L".xsb");
        AppendLog(L"Processing: " + xbbPath.wstring());
        fs::path outDir = entry.path().parent_path() / L"extracted";
        std::error_code ec; fs::create_directory(outDir, ec);
        FILE* fXBB = _wfopen(xbbPath.c_str(), L"rb");
        if (!fXBB) { AppendLog(L"  Error: Could not open " + xbbPath.filename().wstring()); continue; }
        FILE* fXSB = _wfopen(xsbPath.c_str(), L"rb");
        uint64_t xsbSize = 0;
        if (fXSB) { _fseeki64(fXSB, 0, SEEK_END); xsbSize = _ftelli64(fXSB); _fseeki64(fXSB, 0, SEEK_SET); }
        uint32_t xbbDeclared = 0, entryCount = 0;
        fread(&xbbDeclared, 4, 1, fXBB); fread(&entryCount, 4, 1, fXBB);
        _fseeki64(fXBB, 0, SEEK_END); const uint64_t xbbSize = _ftelli64(fXBB);
        uint64_t cursor = 8;
        for (uint32_t i = 0; i < entryCount; ++i) {
            if (cursor + 8 > xbbSize) break;
            _fseeki64(fXBB, cursor + 4, SEEK_SET);
            uint32_t headerLen = 0; fread(&headerLen, 4, 1, fXBB);
            if (headerLen == 0 || cursor + headerLen + 8 > xbbSize) { cursor += 8; continue; }
            std::vector<uint8_t> headerBuf(headerLen);
            _fseeki64(fXBB, cursor, SEEK_SET); fread(headerBuf.data(), 1, headerLen, fXBB);
            uint8_t tail8[8] = { 0 };
            _fseeki64(fXBB, cursor + headerLen, SEEK_SET); fread(&tail8[0], 1, 8, fXBB);
            uint32_t offLE = 0, lenLE = 0;
            memcpy(&offLE, &tail8[0], 4); memcpy(&lenLE, &tail8[4], 4);
            int dataPos = find_fourcc(headerBuf.data(), headerBuf.size(), "data");
            bool isInline = false; bool addTail8 = false; uint32_t headerDataLen = 0;
            if (dataPos >= 0 && dataPos + 8 <= (int)headerBuf.size()) {
                memcpy(&headerDataLen, &headerBuf[dataPos + 4], 4);
                uint64_t dataEnd = (uint64_t)dataPos + 8 + headerDataLen;
                if (headerDataLen > 0 && dataEnd == headerBuf.size()) isInline = true;
                if (!isInline && headerDataLen > 0 && dataEnd == headerBuf.size() + 8) { isInline = true; addTail8 = true; }
                if (headerDataLen >= 0xF0000000) { isInline = false; addTail8 = false; }
            }
            bool streamedOK = false; uint64_t off = 0, len = 0;
            if (!isInline) { off = offLE; len = lenLE; streamedOK = (fXSB != nullptr) && (len > 0) && (off + len <= xsbSize); }
            wchar_t outPath[MAX_PATH]; swprintf(outPath, MAX_PATH, L"%s\\track_%03u.wav", outDir.wstring().c_str(), i);
            FILE* out = _wfopen(outPath, L"wb"); if (!out) { cursor += headerLen + 8; continue; }
            if (isInline) {
                patch_wav_lengths(headerBuf, headerDataLen);
                uint32_t riffSz = (uint32_t)(headerBuf.size() - 8 + (addTail8 ? 8 : 0));
                memcpy(&headerBuf[4], &riffSz, 4);
                fwrite(headerBuf.data(), 1, headerBuf.size(), out);
                if (addTail8) fwrite(&tail8[0], 1, 8, out);
            }
            else if (streamedOK) {
                patch_wav_lengths(headerBuf, (uint32_t)len);
                uint32_t newRiff = (uint32_t)((uint64_t)headerBuf.size() + len - 8);
                memcpy(&headerBuf[4], &newRiff, 4);
                fwrite(headerBuf.data(), 1, headerBuf.size(), out);
                _fseeki64(fXSB, off, SEEK_SET);
                const size_t BUFSZ = 1 << 20;
                std::vector<uint8_t> buf(BUFSZ);
                uint64_t remaining = len;
                while (remaining) { size_t chunk = (size_t)std::min<uint64_t>(remaining, BUFSZ); size_t got = fread(buf.data(), 1, chunk, fXSB); if (got == 0) break; fwrite(buf.data(), 1, got, out); remaining -= got; }
            }
            else {
                patch_wav_lengths(headerBuf, 0);
                uint32_t riffSz = (uint32_t)(headerBuf.size() - 8);
                memcpy(&headerBuf[4], &riffSz, 4);
                fwrite(headerBuf.data(), 1, headerBuf.size(), out);
            }
            fclose(out);
            cursor += headerLen + 8;
        }
        fclose(fXBB); if (fXSB) fclose(fXSB);
    }
    if (xbb_files_found == 0) { AppendLog(L"Extraction pass complete. No .xbb files were found."); }
    else { AppendLog(L"Extraction pass complete. Processed " + std::to_wstring(xbb_files_found) + L" file(s)."); }
}

void AnalyzeAndRenameWavs(const std::wstring& rootPath) {
    AppendLog(L"--- Starting Recursive WAV Renaming and Analysis ---");
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { AppendLog(L"  Error accessing: " + s2ws(ec.message())); ec.clear(); continue; }
        if (!it->is_regular_file()) continue;
        const fs::path floPath = it->path(); if (LowerExt(floPath) != L".flo") continue;
        AppendLog(L"Processing .flo: " + floPath.wstring());
        fs::path extractedDir = floPath.parent_path() / L"extracted";
        if (!fs::exists(extractedDir) || !fs::is_directory(extractedDir)) { AppendLog(L"  No 'extracted' folder found, skipping rename for this .flo."); continue; }
        std::map<int, SoundDataFileEntry> soundDataFiles; std::map<int, SimpleEventEntry> simpleEvents; std::map<int, RandomEventEntry> randomEvents; std::map<int, CompoundEventEntry> compoundEvents; std::vector<EventMapEntry> eventMaps; SoundParameterSets sps;
        if (!ParseFlo(floPath, soundDataFiles, simpleEvents, randomEvents, compoundEvents, eventMaps, sps)) { continue; }
        AppendLog(L"  --- Detailed .flo Analysis ---");
        AppendLog(L"    Parsed: " + std::to_wstring(soundDataFiles.size()) + L" SDFs, " + std::to_wstring(simpleEvents.size()) + L" Simple, " + std::to_wstring(randomEvents.size()) + L" Random, " + std::to_wstring(compoundEvents.size()) + L" Compound, " + std::to_wstring(eventMaps.size()) + L" EventMap entries.");
        std::map<int, int> simple_to_sdf; for (auto& kv : simpleEvents) simple_to_sdf[kv.first] = kv.second.sound_data_file_id;
        std::map<int, std::vector<LinkProvenance>> sdf_to_eventmaps; int appearance_counter = 0, unresolved_rows = 0;
        for (const auto& em : eventMaps) {
            std::vector<int> sdf_ids_here, via_simple_ids;
            int used_domain = ExpandRefWithFallback(em.type_col2, em.event_ref_col3, simpleEvents, randomEvents, compoundEvents, simple_to_sdf, sdf_ids_here, via_simple_ids);
            if (sdf_ids_here.empty()) { ++unresolved_rows; }
            std::set<int> uniq_sdf(sdf_ids_here.begin(), sdf_ids_here.end());
            for (int sdf_id : uniq_sdf) { LinkProvenance p{}; p.eventmap_id = em.id_col1; p.section = s2ws(em.section_name); p.domain = used_domain; p.link_id = em.event_ref_col3; p.global_name = em.name_col4; p.appearance_idx = appearance_counter; p.expansion_size = (int)uniq_sdf.size(); p.via_sdf_id = sdf_id; if (!via_simple_ids.empty()) p.via_simple_id = via_simple_ids.front(); sdf_to_eventmaps[sdf_id].push_back(std::move(p)); }
            ++appearance_counter;
        }

        fs::path csvPath = floPath; csvPath.replace_extension(L".eventmap_sdf_links.csv"); AppendLog(L"  Writing link report: " + csvPath.filename().wstring());
        {
            std::wofstream csv(csvPath, std::ios::binary);
            if (!csv) { AppendLog(L"  Error: failed to create CSV: " + csvPath.wstring()); }
            else {
                csv << L"sdf_id,xbb_filename,eventmap_id,global_name\n";
                std::vector<int> sdf_ids_sorted; sdf_ids_sorted.reserve(soundDataFiles.size());
                for (const auto& kv : soundDataFiles) sdf_ids_sorted.push_back(kv.first);
                std::sort(sdf_ids_sorted.begin(), sdf_ids_sorted.end());

                for (int sdf_id : sdf_ids_sorted) {
                    std::wstring xbb;
                    auto itS = soundDataFiles.find(sdf_id);
                    if (itS != soundDataFiles.end()) xbb = s2ws(itS->second.xbb_filename);

                    auto itLinks = sdf_to_eventmaps.find(sdf_id);
                    if (itLinks == sdf_to_eventmaps.end() || itLinks->second.empty()) {
                        csv << sdf_id << L"," << CsvEscape(xbb) << L"\n";
                    }
                    else {
                        csv << sdf_id << L"," << CsvEscape(xbb) << L",\n";

                        auto links = itLinks->second;
                        std::stable_sort(links.begin(), links.end(), [](const LinkProvenance& a, const LinkProvenance& b) {
                            if (a.eventmap_id != b.eventmap_id) return a.eventmap_id < b.eventmap_id;
                            return a.appearance_idx < b.appearance_idx;
                            });

                        for (const auto& link : links) {
                            csv << L"\t" << link.eventmap_id << L"," << CsvEscape(link.global_name) << L"\n";
                        }
                    }
                }

                csv.close();
                AppendLog(L"  Link report written.");
            }
        }

        auto better_prov = [](const LinkProvenance& a, const LinkProvenance& b) { if (a.expansion_size != b.expansion_size) return a.expansion_size < b.expansion_size; int sa = SectionPriority(std::string(a.section.begin(), a.section.end())); int sb = SectionPriority(std::string(b.section.begin(), b.section.end())); if (sa != sb) return sa < sb; int da = DomainPriority(a.domain), db = DomainPriority(b.domain); if (da != db) return da < db; return a.appearance_idx < b.appearance_idx; };
        std::map<int, std::wstring> primaryNameForSdf; for (auto& kv : sdf_to_eventmaps) { int sdf = kv.first; auto vec = kv.second; std::stable_sort(vec.begin(), vec.end(), better_prov); if (!vec.empty()) primaryNameForSdf[sdf] = vec.front().global_name; }
        std::map<int, std::wstring> finalNameForSdf = primaryNameForSdf; std::map<std::wstring, std::vector<int>> byName; for (const auto& kvp : primaryNameForSdf) byName[kvp.second].push_back(kvp.first);
        for (auto& kvn : byName) { auto& sdfs = kvn.second; if (sdfs.size() <= 1) continue; std::sort(sdfs.begin(), sdfs.end()); for (size_t i = 0; i < sdfs.size(); ++i) { finalNameForSdf[sdfs[i]] = kvn.first + L"_" + std::to_wstring(i + 1); } }
        AppendLog(L"  --- Renaming WAV files in " + extractedDir.wstring() + L" ---");
        int renamed = 0, errors = 0; std::map<std::wstring, int> targetCollisionCheck;
        for (auto& f : fs::directory_iterator(extractedDir, ec)) {
            if (ec) { ec.clear(); continue; } if (!f.is_regular_file() || LowerExt(f.path()) != L".wav") continue;
            std::wstring stem = Trim(f.path().stem().wstring()); std::wstring num = stem; if (stem.rfind(L"track_", 0) == 0) num = stem.substr(6);
            if (num.empty() || !std::all_of(num.begin(), num.end(), ::iswdigit)) { AppendLog(L"  Skipping non-numeric WAV: " + f.path().filename().wstring()); continue; }
            int sdf_id = -1; try { sdf_id = std::stoi(num); }
            catch (...) { continue; }
            auto itN = finalNameForSdf.find(sdf_id); if (itN == finalNameForSdf.end()) continue;
            std::wstring base = itN->second; std::replace(base.begin(), base.end(), L'*', L'_'); std::replace(base.begin(), base.end(), L'/', L'_'); std::replace(base.begin(), base.end(), L'\\', L'_');
            fs::path newPath = f.path().parent_path() / (base + L".wav");
            if (targetCollisionCheck.count(newPath.filename().wstring())) { AppendLog(L"  Error: Rename collision for '" + newPath.filename().wstring() + L"'. Skipping."); ++errors; continue; }
            if (fs::exists(newPath) && newPath != f.path()) { AppendLog(L"  Error: Target file exists '" + newPath.filename().wstring() + L"'. Skipping."); ++errors; continue; }
            std::error_code rec; fs::rename(f.path(), newPath, rec);
            if (rec) { AppendLog(L"  Error renaming " + f.path().filename().wstring() + L": " + s2ws(rec.message())); ++errors; }
            else { AppendLog(L"  Renamed " + f.path().filename().wstring() + L" -> " + newPath.filename().wstring()); targetCollisionCheck[newPath.filename().wstring()] = sdf_id; ++renamed; }
        }
        AppendLog(L"  Finished for " + floPath.filename().wstring() + L". Renamed: " + std::to_wstring(renamed) + L", Errors: " + std::to_wstring(errors));
    }
    AppendLog(L"Renaming and analysis pass complete.");
}

void RepackAudio(HWND hwnd) {
    AppendLog(L"--- Starting Audio Repack ---");
    std::wstring wavDir = BrowseForFolderModern(hwnd, L"Select folder containing .WAV files to repack");
    if (wavDir.empty()) { AppendLog(L"Repack cancelled: No source folder selected."); return; }
    wchar_t xbbPath[MAX_PATH] = { 0 }, xsbPath[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(ofn) }; ofn.hwndOwner = hwnd; ofn.lpstrFile = xbbPath; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"XBB Files\0*.xbb\0All Files\0*.*\0"; ofn.Flags = OFN_OVERWRITEPROMPT; ofn.lpstrTitle = L"Select output .XBB file";
    if (!GetSaveFileNameW(&ofn)) { AppendLog(L"Repack cancelled: No output file selected."); return; }
    wcscpy_s(xsbPath, xbbPath); PathRemoveExtensionW(xsbPath); wcscat_s(xsbPath, MAX_PATH, L".xsb");
    std::vector<std::wstring> wavFiles;
    for (const auto& entry : fs::directory_iterator(wavDir)) {
        if (entry.is_regular_file() && LowerExt(entry.path()) == L".wav") { wavFiles.push_back(entry.path().wstring()); }
    }
    std::sort(wavFiles.begin(), wavFiles.end());
    if (wavFiles.empty()) { AppendLog(L"Error: No .wav files found in the selected directory."); return; }
    AppendLog(L"Found " + std::to_wstring(wavFiles.size()) + L" .wav files to repack.");
    uint32_t entryCount = (uint32_t)wavFiles.size();
    FILE* fXBB = _wfopen(xbbPath, L"wb"); FILE* fXSB = _wfopen(xsbPath, L"wb");
    if (!fXBB || !fXSB) { AppendLog(L"Error: Failed to create output files."); if (fXBB) fclose(fXBB); if (fXSB) fclose(fXSB); return; }
    fwrite("\0\0\0\0", 4, 1, fXBB); fwrite(&entryCount, 4, 1, fXBB);
    uint32_t entryStart = 8, currentOffset = 0;
    for (uint32_t i = 0; i < entryCount; i++) {
        FILE* w = _wfopen(wavFiles[i].c_str(), L"rb");
        if (!w) { AppendLog(L"  Error: Could not open " + wavFiles[i]); continue; }
        fseek(w, 0, SEEK_END); long fileSize = ftell(w); fseek(w, 0, SEEK_SET);
        if (fileSize < 44) { fclose(w); continue; }
        std::vector<uint8_t> wavBuf(fileSize); fread(wavBuf.data(), 1, fileSize, w); fclose(w);
        int dataPos = find_fourcc(wavBuf.data(), wavBuf.size(), "data");
        if (dataPos == -1) { AppendLog(L"  Warning: Could not find 'data' chunk in " + fs::path(wavFiles[i]).filename().wstring()); continue; }
        uint32_t dataLength = 0; memcpy(&dataLength, &wavBuf[dataPos + 4], 4);
        uint32_t headerLen = dataPos + 8;
        if (headerLen + dataLength > wavBuf.size()) { AppendLog(L"  Warning: Corrupt WAV header in " + fs::path(wavFiles[i]).filename().wstring()); continue; }
        fseek(fXBB, entryStart, SEEK_SET); fwrite(wavBuf.data(), 1, headerLen, fXBB);
        entryStart += headerLen;
        fwrite(&currentOffset, 4, 1, fXBB); fwrite(&dataLength, 4, 1, fXBB);
        entryStart += 8;
        fseek(fXSB, currentOffset, SEEK_SET); fwrite(wavBuf.data() + headerLen, 1, dataLength, fXSB);
        currentOffset += dataLength;
    }
    uint32_t finalSize = entryStart; fseek(fXBB, 0, SEEK_SET); fwrite(&finalSize, 4, 1, fXBB);
    fclose(fXBB); fclose(fXSB);
    MessageBoxW(hwnd, L"Repack complete!", L"Done", MB_OK);
}

// **NEW**: Orchestrator for the unified batch process.
void DoUnifiedBatchProcess(HWND hwnd) {
    wchar_t buf[MAX_PATH];
    GetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_PATH), buf, MAX_PATH);
    std::wstring rootPath(buf);
    if (rootPath.empty()) { MessageBoxW(hwnd, L"Select a root Audio folder first using the 'Browse' button.", L"Error", MB_OK | MB_ICONERROR); return; }
    if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) { MessageBoxW(hwnd, L"The selected path is not a valid directory.", L"Error", MB_OK | MB_ICONERROR); return; }

    SetWindowTextW(g_hLog, L"");

    // First Pass: Extract all audio throughout the entire directory tree.
    BatchExtractAll(rootPath);
    AppendLog(L"");

    // Second Pass: Analyze all .flo files and rename the newly extracted audio.
    AnalyzeAndRenameWavs(rootPath);
    AppendLog(L"");

    AppendLog(L"--- All tasks complete! ---");
    MessageBoxW(hwnd, L"Batch processing complete for all subdirectories!", L"Done", MB_OK);
}

// =================================================================================
// SECTION: WIN32 GUI AND MAIN LOOP
// =================================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"Root Folder:", WS_CHILD | WS_VISIBLE, 10, 12, 80, 20, hwnd, NULL, g_hInst, NULL);
        CreateWindowW(L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY, 90, 10, 360, 25, hwnd, (HMENU)IDC_EDIT_PATH, g_hInst, NULL);
        CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 460, 10, 80, 25, hwnd, (HMENU)IDC_BROWSE, g_hInst, NULL);
        CreateWindowW(L"BUTTON", L"Extract & Rename All", WS_CHILD | WS_VISIBLE, 10, 45, 260, 30, hwnd, (HMENU)ID_BUTTON_EXTRACT_RENAME, g_hInst, NULL);
        CreateWindowW(L"BUTTON", L"Repack Audio to XBB/XSB", WS_CHILD | WS_VISIBLE, 280, 45, 260, 30, hwnd, (HMENU)ID_BUTTON_REPACK, g_hInst, NULL);
        g_hLog = CreateWindowW(L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL, 10, 85, 530, 260, hwnd, (HMENU)IDC_LOG, g_hInst, NULL);
        SendMessageW(g_hLog, EM_LIMITTEXT, 0, 0);
    } break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BROWSE) {
            std::wstring p = BrowseForFolderModern(hwnd, L"Select the Root Audio Folder to Process");
            if (!p.empty()) SetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_PATH), p.c_str());
        }
        else if (LOWORD(wParam) == ID_BUTTON_EXTRACT_RENAME) {
            DoUnifiedBatchProcess(hwnd);
        }
        else if (LOWORD(wParam) == ID_BUTTON_REPACK) {
            RepackAudio(hwnd);
        }
        break;
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInst;
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        MessageBoxW(NULL, L"COM Initialization failed.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst; wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszClassName = L"GladiusAudioToolWindowClass"; wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) { MessageBoxW(NULL, L"Window Registration Failed!", L"Error", MB_ICONERROR | MB_OK); return 1; }
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Gladius Xbox All-in-One Audio Tool",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 565, 400, NULL, NULL, hInst, NULL);
    if (!hwnd) { MessageBoxW(NULL, L"Window Creation Failed!", L"Error", MB_ICONERROR | MB_OK); return 1; }
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    CoUninitialize();
    return (int)msg.wParam;
}