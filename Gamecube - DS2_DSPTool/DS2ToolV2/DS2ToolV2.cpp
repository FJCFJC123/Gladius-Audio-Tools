#include <windows.h>
#include <iostream>
#define NOMINMAX 
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#include <shlobj.h> 
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm> 
#include <sstream>    

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using String = std::wstring;
String OpenFileDialog(const wchar_t* filter);
String SelectFolderDialog(HWND hwndOwner, const wchar_t* title);

// DS2 (Stereo) functions
bool DecodeDS2toWav(const String& ds2Path, const String& wavPath);
bool EncodeWavToDS2(const String& wavPath, const String& ds2Path);

// DSP (Mono) functions
// MODIFIED: Function signature now accepts a window handle for pop-up messages
bool DecodeMonoDspToWav(const String& dspPath, const String& wavPath, HWND hwndParent);
bool EncodeWavToMonoDsp(const String& wavPath, const String& dspPath);


constexpr int IDC_BTN_DEC_DS2_SINGLE = 101;
constexpr int IDC_BTN_ENC_DS2_SINGLE = 102;
constexpr int IDC_STATUS = 103;
constexpr int IDC_BTN_DEC_DS2_BATCH = 104;
constexpr int IDC_BTN_ENC_DS2_BATCH = 105;
constexpr int IDC_BTN_DEC_DSP_SINGLE = 106;
constexpr int IDC_BTN_ENC_DSP_SINGLE = 107;
constexpr int IDC_BTN_DEC_DSP_BATCH = 108;
constexpr int IDC_BTN_ENC_DSP_BATCH = 109;

HINSTANCE hInst;

// Big-endian readers/writers and clamp16 (unchanged)
inline uint16_t read_u16_be(const uint8_t* buf) { return (uint16_t)((buf[0] << 8) | buf[1]); }
inline int16_t  read_s16_be(const uint8_t* buf) { return (int16_t)((buf[0] << 8) | buf[1]); }
inline uint32_t read_u32_be(const uint8_t* buf) { return (uint32_t)((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]); }
inline void write_u16_be(uint8_t* buf, uint16_t val) { buf[0] = static_cast<uint8_t>((val >> 8) & 0xFF); buf[1] = static_cast<uint8_t>(val & 0xFF); }
inline void write_s16_be(uint8_t* buf, int16_t val) { write_u16_be(buf, static_cast<uint16_t>(val)); }
inline void write_u32_be(uint8_t* buf, uint32_t val) { buf[0] = static_cast<uint8_t>((val >> 24) & 0xFF); buf[1] = static_cast<uint8_t>((val >> 16) & 0xFF); buf[2] = static_cast<uint8_t>((val >> 8) & 0xFF); buf[3] = static_cast<uint8_t>(val & 0xFF); }
inline int32_t clamp16(int32_t v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

// WriteWav function (unchanged)
void WriteWav(const String& path, const std::vector<int16_t>& pcm_data, int sampleRate, uint16_t num_channels_to_write) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return;
    uint16_t bits = 16;
    uint32_t dataBytes = static_cast<uint32_t>(pcm_data.size()) * sizeof(int16_t);
    uint32_t byteRate = sampleRate * num_channels_to_write * (bits / 8);
    uint16_t blockAlign = num_channels_to_write * (bits / 8);
    f.write("RIFF", 4);
    uint32_t riffSize = 36 + dataBytes;
    f.write(reinterpret_cast<char*>(&riffSize), 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    uint32_t fmtLen = 16; f.write(reinterpret_cast<char*>(&fmtLen), 4);
    uint16_t audioFmt = 1; f.write(reinterpret_cast<char*>(&audioFmt), 2);
    f.write(reinterpret_cast<char*>(&num_channels_to_write), 2);
    f.write(reinterpret_cast<char*>(&sampleRate), 4);
    f.write(reinterpret_cast<char*>(&byteRate), 4);
    f.write(reinterpret_cast<char*>(&blockAlign), 2);
    f.write(reinterpret_cast<char*>(&bits), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<char*>(&dataBytes), 4);
    f.write(reinterpret_cast<const char*>(pcm_data.data()), dataBytes);
    f.close();
}

// DspChannelHeader struct (unchanged)
struct DspChannelHeader {
    uint8_t raw[0x60];
    DspChannelHeader() { std::memset(raw, 0, sizeof(raw)); }
    void set_num_samples(uint32_t val) { write_u32_be(raw + 0x00, val); }
    uint32_t get_num_samples() const { return read_u32_be(raw + 0x00); }
    void set_num_adpcm_nibbles(uint32_t val) { write_u32_be(raw + 0x04, val); }
    uint32_t get_num_adpcm_nibbles() const { return read_u32_be(raw + 0x04); }
    void set_sample_rate(uint32_t val) { write_u32_be(raw + 0x08, val); }
    uint32_t get_sample_rate() const { return read_u32_be(raw + 0x08); }
    void set_loop_flag(uint16_t val) { write_u16_be(raw + 0x0C, val); }
    uint16_t get_loop_flag() const { return read_u16_be(raw + 0x0C); }
    void set_format_info(uint16_t val) { write_u16_be(raw + 0x0E, val); }
    uint16_t get_format_info() const { return read_u16_be(raw + 0x0E); }
    void set_loop_start_nibble_addr(uint32_t val) { write_u32_be(raw + 0x10, val); }
    void set_loop_end_sample_addr(uint32_t val) { write_u32_be(raw + 0x14, val); }
    void set_current_adpcm_addr(uint32_t val) { write_u32_be(raw + 0x18, val); }
    void set_coeffs(const int16_t coeffs_in[16]) { for (int i = 0; i < 16; ++i) write_s16_be(raw + 0x1C + i * 2, coeffs_in[i]); }
    void get_coeffs(int16_t coeffs_out[16]) const { for (int i = 0; i < 16; ++i) coeffs_out[i] = read_s16_be(raw + 0x1C + i * 2); }
    void set_gain(uint16_t val) { write_u16_be(raw + 0x3C, val); }
    void set_initial_pred_scale(uint16_t val) { write_u16_be(raw + 0x3E, val); }
    uint16_t get_initial_pred_scale() const { return read_u16_be(raw + 0x3E); }
    void set_initial_hist1(int16_t val) { write_s16_be(raw + 0x40, val); }
    int16_t get_initial_hist1() const { return read_s16_be(raw + 0x40); }
    void set_initial_hist2(int16_t val) { write_s16_be(raw + 0x42, val); }
    int16_t get_initial_hist2() const { return read_s16_be(raw + 0x42); }
    void set_loop_pred_scale(uint16_t val) { write_u16_be(raw + 0x44, val); }
    void set_loop_hist1(int16_t val) { write_s16_be(raw + 0x46, val); }
    void set_loop_hist2(int16_t val) { write_s16_be(raw + 0x48, val); }
    void set_unknown_constants() { write_u16_be(raw + 0x4A, 0x5E3D); write_u16_be(raw + 0x4C, 0x0000); }
    void set_adpcm_data_size_bytes(uint32_t val) { write_u32_be(raw + 0x58, val); }
    uint32_t get_adpcm_data_size_bytes() const { return read_u32_be(raw + 0x58); }
    void set_offset_to_adpcm_data(uint32_t val) { write_u32_be(raw + 0x5C, val); }
    uint32_t get_offset_to_adpcm_data() const { return read_u32_be(raw + 0x5C); }
};

// DecodeDS2toWav (unchanged)
bool DecodeDS2toWav(const String& ds2Path, const String& wavPath) {
    std::ifstream in(ds2Path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    size_t fileSize = static_cast<size_t>(in.tellg());
    if (fileSize < 0xC0) return false;
    in.seekg(0);
    DspChannelHeader leftHeader, rightHeader;
    if (!in.read(reinterpret_cast<char*>(leftHeader.raw), sizeof(leftHeader.raw))) return false;
    if (!in.read(reinterpret_cast<char*>(rightHeader.raw), sizeof(rightHeader.raw))) return false;
    uint32_t totalSamplesL = leftHeader.get_num_samples();
    uint32_t totalSamplesR = rightHeader.get_num_samples();
    if (totalSamplesL == 0 || totalSamplesL != totalSamplesR) { return false; }
    uint32_t sampleRateL = leftHeader.get_sample_rate();
    uint32_t sampleRateR = rightHeader.get_sample_rate();
    if (sampleRateL == 0 || sampleRateL != sampleRateR) { return false; }
    int16_t coefL[16], coefR[16];
    leftHeader.get_coeffs(coefL); rightHeader.get_coeffs(coefR);
    int16_t hist1L = leftHeader.get_initial_hist1(); int16_t hist2L = leftHeader.get_initial_hist2();
    int16_t hist1R = rightHeader.get_initial_hist1(); int16_t hist2R = rightHeader.get_initial_hist2();
    uint32_t offsetL_ADPCM = leftHeader.get_offset_to_adpcm_data();
    uint32_t offsetR_ADPCM = rightHeader.get_offset_to_adpcm_data();
    uint32_t calculatedAdpcmDataSizeBytesL = ((totalSamplesL + 13) / 14) * 8;
    uint32_t calculatedAdpcmDataSizeBytesR = ((totalSamplesR + 13) / 14) * 8;
    if (offsetL_ADPCM != 0xC0 || offsetR_ADPCM < (offsetL_ADPCM + calculatedAdpcmDataSizeBytesL) ||
        offsetL_ADPCM + calculatedAdpcmDataSizeBytesL > fileSize ||
        offsetR_ADPCM + calculatedAdpcmDataSizeBytesR > fileSize) {
    }
    std::vector<uint8_t> fileData(fileSize);
    in.seekg(0); in.read(reinterpret_cast<char*>(fileData.data()), fileSize); in.close();
    std::vector<int16_t> leftSamples(totalSamplesL), rightSamples(totalSamplesR);
    auto decodeChannel = [&](uint32_t adpcm_data_start_offset, uint32_t num_channel_samples,
        int16_t& h1, int16_t& h2, const int16_t* coefs,
        std::vector<int16_t>& out_samples, const uint8_t* all_file_data,
        size_t total_file_size) {
            size_t pcm_idx = 0; uint32_t adpcm_block_offset = adpcm_data_start_offset;
            while (pcm_idx < num_channel_samples) {
                if (adpcm_block_offset + 8 > total_file_size) break;
                uint8_t hdr = all_file_data[adpcm_block_offset];
                int predIdx = (hdr >> 4) & 0x0F; if (predIdx > 7) predIdx = 7;
                int shift = hdr & 0x0F; int scale = 1 << shift;
                int16_t c1 = coefs[predIdx * 2 + 0]; int16_t c2 = coefs[predIdx * 2 + 1];
                for (int n = 0; n < 14 && pcm_idx < num_channel_samples; ++n) {
                    if (adpcm_block_offset + 1 + (n >> 1) >= total_file_size) break;
                    uint8_t byte_val = all_file_data[adpcm_block_offset + 1 + (n >> 1)];
                    int8_t nib = (n & 1) ? (byte_val & 0x0F) : (byte_val >> 4);
                    if (nib & 0x8) nib |= 0xF0;
                    int32_t sVal = static_cast<int32_t>(nib) * scale; sVal <<= 11;
                    int32_t prediction = static_cast<int32_t>(c1) * h1 + static_cast<int32_t>(c2) * h2;
                    int32_t sum = sVal + prediction + 1024; int16_t sample = static_cast<int16_t>(clamp16(sum >> 11));
                    h2 = h1; h1 = sample; out_samples[pcm_idx++] = sample;
                }
                adpcm_block_offset += 8;
            }
        };
    decodeChannel(offsetL_ADPCM, totalSamplesL, hist1L, hist2L, coefL, leftSamples, fileData.data(), fileSize);
    decodeChannel(offsetR_ADPCM, totalSamplesR, hist1R, hist2R, coefR, rightSamples, fileData.data(), fileSize);
    std::vector<int16_t> interleaved(totalSamplesL * 2);
    for (size_t i = 0; i < totalSamplesL; ++i) {
        interleaved[i * 2] = leftSamples[i];
        interleaved[i * 2 + 1] = rightSamples[i];
    }
    WriteWav(wavPath, interleaved, sampleRateL, 2);
    return true;
}

// WavData struct and find_chunk, ReadWavFile (unchanged)
struct WavData {
    uint32_t sampleRate = 0; uint16_t numChannels = 0; uint16_t bitsPerSample = 0;
    uint32_t totalSamplesPerChannel = 0; std::vector<int16_t> pcmSamplesLeft;
    std::vector<int16_t> pcmSamplesRight; bool valid = false;
};
bool find_chunk(std::ifstream& f, const char* chunkID_target, uint32_t& chunkSize) {
    char id[4]; chunkSize = 0; if (!f.good()) return false;
    std::streampos initialPos = f.tellg();
    while (f.read(id, 4)) {
        if (!f.read(reinterpret_cast<char*>(&chunkSize), 4)) { f.clear(); f.seekg(initialPos); return false; }
        if (std::strncmp(id, chunkID_target, 4) == 0) { return true; }
        std::streampos currentPos = f.tellg();
        if (!f.seekg(chunkSize, std::ios_base::cur)) { f.clear(); f.seekg(initialPos); return false; }
        if (f.tellg() <= currentPos && chunkSize > 0) { f.clear(); f.seekg(initialPos); return false; }
    }
    f.clear(); f.seekg(initialPos); return false;
}
WavData ReadWavFile(const String& wavPath) {
    WavData wav; std::ifstream f(wavPath, std::ios::binary);
    if (!f.is_open()) return wav; char chunkID[4]; uint32_t chunkSize;
    if (!f.read(chunkID, 4) || std::strncmp(chunkID, "RIFF", 4) != 0) return wav;
    f.seekg(4, std::ios_base::cur);
    if (!f.read(chunkID, 4) || std::strncmp(chunkID, "WAVE", 4) != 0) return wav;
    if (!find_chunk(f, "fmt ", chunkSize) || chunkSize < 16) return wav;
    uint16_t audioFormat; f.read(reinterpret_cast<char*>(&audioFormat), 2);
    f.read(reinterpret_cast<char*>(&wav.numChannels), 2); f.read(reinterpret_cast<char*>(&wav.sampleRate), 4);
    f.seekg(4, std::ios_base::cur);
    uint16_t blockAlign_wav;
    f.read(reinterpret_cast<char*>(&blockAlign_wav), 2); f.read(reinterpret_cast<char*>(&wav.bitsPerSample), 2);
    if (audioFormat != 1 || wav.numChannels == 0 || wav.bitsPerSample != 16) { return wav; }
    if (chunkSize > 16) f.seekg(chunkSize - 16, std::ios_base::cur);
    if (!find_chunk(f, "data", chunkSize)) return wav;
    uint32_t bytesPerSampleSet = wav.numChannels * (wav.bitsPerSample / 8);
    if (bytesPerSampleSet == 0) return wav;
    wav.totalSamplesPerChannel = chunkSize / bytesPerSampleSet;
    wav.pcmSamplesLeft.resize(wav.totalSamplesPerChannel);
    if (wav.numChannels >= 2) {
        wav.pcmSamplesRight.resize(wav.totalSamplesPerChannel);
    }
    else if (wav.numChannels == 1) {
        wav.pcmSamplesRight.resize(wav.totalSamplesPerChannel);
    }
    for (uint32_t i = 0; i < wav.totalSamplesPerChannel; ++i) {
        int16_t sampleL; if (!f.read(reinterpret_cast<char*>(&sampleL), sizeof(int16_t))) { wav.valid = false; return wav; }
        wav.pcmSamplesLeft[i] = sampleL;
        if (wav.numChannels >= 2) {
            int16_t sampleR; if (!f.read(reinterpret_cast<char*>(&sampleR), sizeof(int16_t))) { wav.valid = false; return wav; }
            wav.pcmSamplesRight[i] = sampleR;
            if (wav.numChannels > 2) f.seekg((wav.numChannels - 2) * (wav.bitsPerSample / 8), std::ios_base::cur);
        }
        else if (wav.numChannels == 1) { wav.pcmSamplesRight[i] = sampleL; }
    }
    wav.valid = true; return wav;
}

// EncodeChannelADPCM (unchanged)
std::vector<uint8_t> EncodeChannelADPCM(
    const std::vector<int16_t>& pcmSamples, uint32_t totalSamplesToEncode,
    int16_t& io_hist1, int16_t& io_hist2, const int16_t adpcmCoefs[16],
    uint16_t& out_initial_pred_scale) {
    std::vector<uint8_t> encodedData;
    if (pcmSamples.empty() || totalSamplesToEncode == 0) { out_initial_pred_scale = 0; return encodedData; }
    size_t numBlocks = (totalSamplesToEncode + 13) / 14; encodedData.reserve(numBlocks * 8);
    int16_t currentHist1 = io_hist1; int16_t currentHist2 = io_hist2; size_t sampleIdx = 0;
    for (size_t block = 0; block < numBlocks; ++block) {
        int samplesInBlock = static_cast<int>((std::min)(static_cast<uint32_t>(14), (totalSamplesToEncode - static_cast<uint32_t>(sampleIdx))));
        if (samplesInBlock <= 0) break;
        int8_t blockNibbles[14]; std::memset(blockNibbles, 0, sizeof(blockNibbles));
        int bestPredIdx = 0; int bestShift = 0; double minErrorSumSq = -1.0;
        int16_t blockInitialHist1 = currentHist1; int16_t blockInitialHist2 = currentHist2;
        int8_t trialNibbles[14];
        for (int predIdxTry = 0; predIdxTry < 8; ++predIdxTry) {
            const int16_t c1 = adpcmCoefs[predIdxTry * 2 + 0]; const int16_t c2 = adpcmCoefs[predIdxTry * 2 + 1];
            for (int shiftTry = 0; shiftTry < 12; ++shiftTry) {
                int16_t tempHist1 = blockInitialHist1; int16_t tempHist2 = blockInitialHist2;
                double currentErrorSumSq = 0;
                for (int n = 0; n < samplesInBlock; ++n) {
                    int16_t currentSample = pcmSamples[sampleIdx + n]; int32_t scale = 1 << shiftTry;
                    int32_t predic = (static_cast<int32_t>(c1) * tempHist1 + static_cast<int32_t>(c2) * tempHist2);
                    int32_t diff = (static_cast<int32_t>(currentSample) << 11) - predic;
                    double nibble_ideal = static_cast<double>(diff) / static_cast<double>(scale << 11);
                    int8_t nib = static_cast<int8_t>(std::round(nibble_ideal));
                    nib = (std::max)(static_cast<int8_t>(-8), (std::min)(static_cast<int8_t>(7), nib));
                    trialNibbles[n] = nib; int32_t sVal = static_cast<int32_t>(nib) * scale; sVal <<= 11;
                    int32_t sum = sVal + predic + 1024; int16_t reconstructedSample = clamp16(sum >> 11);
                    currentErrorSumSq += std::pow(static_cast<double>(currentSample) - reconstructedSample, 2);
                    tempHist2 = tempHist1; tempHist1 = reconstructedSample;
                }
                if (minErrorSumSq < 0 || currentErrorSumSq < minErrorSumSq) {
                    minErrorSumSq = currentErrorSumSq; bestPredIdx = predIdxTry; bestShift = shiftTry;
                    for (int i = 0; i < samplesInBlock; ++i) blockNibbles[i] = trialNibbles[i];
                }
            }
        }
        uint8_t headerByte = static_cast<uint8_t>((bestPredIdx << 4) | (bestShift & 0x0F));
        if (block == 0) { out_initial_pred_scale = headerByte; }
        encodedData.push_back(headerByte);
        const int16_t final_c1 = adpcmCoefs[bestPredIdx * 2 + 0]; const int16_t final_c2 = adpcmCoefs[bestPredIdx * 2 + 1];
        int final_scale_val = 1 << bestShift;
        for (int n = 0; n < 14; n += 2) {
            uint8_t packedByte = (blockNibbles[n] & 0x0F) << 4;
            if (n + 1 < 14) { packedByte |= (blockNibbles[n + 1] & 0x0F); }
            encodedData.push_back(packedByte);
        }
        for (int n = 0; n < samplesInBlock; ++n) {
            int8_t nib = blockNibbles[n]; int32_t sVal = static_cast<int32_t>(nib) * final_scale_val; sVal <<= 11;
            int32_t predic = (static_cast<int32_t>(final_c1) * currentHist1 + static_cast<int32_t>(final_c2) * currentHist2);
            int32_t sum = sVal + predic + 1024; int16_t sampleOut = clamp16(sum >> 11);
            currentHist2 = currentHist1; currentHist1 = sampleOut;
        }
        sampleIdx += samplesInBlock;
    }
    io_hist1 = currentHist1; io_hist2 = currentHist2; return encodedData;
}

// EncodeWavToDS2 (unchanged)
bool EncodeWavToDS2(const String& wavPath, const String& ds2Path) {
    WavData wav = ReadWavFile(wavPath);
    if (!wav.valid) { MessageBox(NULL, (L"DS2 Encode: Failed to read WAV: " + wavPath).c_str(), L"Error", MB_OK | MB_ICONERROR); return false; }
    if (wav.totalSamplesPerChannel == 0) { MessageBox(NULL, (L"DS2 Encode: WAV has zero samples: " + wavPath).c_str(), L"Error", MB_OK | MB_ICONERROR); return false; }
    if (wav.pcmSamplesLeft.empty() || wav.pcmSamplesRight.empty()) {
        MessageBox(NULL, (L"DS2 Encode: WAV has missing L/R channel data (after read): " + wavPath).c_str(), L"Error", MB_OK | MB_ICONERROR); return false;
    }
    uint32_t totalSamples = wav.totalSamplesPerChannel;
    const int16_t dsp_coefs[16] = { 2048, 0, 0, 0, 4096, -2048, 2048, -2048, 3072, -1024, 1024, 512, 512, 256, 2048, 1024 };
    int16_t initialHist1L = 0, initialHist2L = 0; int16_t initialHist1R = 0, initialHist2R = 0;
    uint16_t predScaleL_first = 0, predScaleR_first = 0;
    std::vector<uint8_t> adpcm_L_data = EncodeChannelADPCM(wav.pcmSamplesLeft, totalSamples, initialHist1L, initialHist2L, dsp_coefs, predScaleL_first);
    std::vector<uint8_t> adpcm_R_data = EncodeChannelADPCM(wav.pcmSamplesRight, totalSamples, initialHist1R, initialHist2R, dsp_coefs, predScaleR_first);
    uint32_t adpcm_L_data_bytes = static_cast<uint32_t>(adpcm_L_data.size());
    uint32_t adpcm_R_data_bytes = static_cast<uint32_t>(adpcm_R_data.size());
    uint32_t expected_adpcm_bytes = ((totalSamples + 13) / 14) * 8;
    if (adpcm_L_data_bytes != expected_adpcm_bytes || adpcm_R_data_bytes != expected_adpcm_bytes) { MessageBox(NULL, (L"DS2 Encode: ADPCM size mismatch for " + wavPath).c_str(), L"Error", MB_OK | MB_ICONERROR); return false; }
    DspChannelHeader headerL, headerR;
    headerL.set_num_samples(totalSamples); headerL.set_num_adpcm_nibbles(((totalSamples + 13) / 14) * 16);
    headerL.set_sample_rate(wav.sampleRate); headerL.set_loop_flag(0); headerL.set_format_info(0x0000);
    headerL.set_loop_start_nibble_addr(0); headerL.set_loop_end_sample_addr(totalSamples);
    headerL.set_current_adpcm_addr(0); headerL.set_coeffs(dsp_coefs); headerL.set_gain(0);
    headerL.set_initial_pred_scale(predScaleL_first); headerL.set_initial_hist1(0); headerL.set_initial_hist2(0);
    headerL.set_loop_pred_scale(0); headerL.set_loop_hist1(0); headerL.set_loop_hist2(0);
    headerL.set_unknown_constants(); headerL.set_adpcm_data_size_bytes(adpcm_L_data_bytes);
    headerL.set_offset_to_adpcm_data(0xC0);
    headerR.set_num_samples(totalSamples); headerR.set_num_adpcm_nibbles(((totalSamples + 13) / 14) * 16);
    headerR.set_sample_rate(wav.sampleRate); headerR.set_loop_flag(0); headerR.set_format_info(0x0000);
    headerR.set_loop_start_nibble_addr(0); headerR.set_loop_end_sample_addr(totalSamples);
    headerR.set_current_adpcm_addr(0); headerR.set_coeffs(dsp_coefs); headerR.set_gain(0);
    headerR.set_initial_pred_scale(predScaleR_first); headerR.set_initial_hist1(0); headerR.set_initial_hist2(0);
    headerR.set_loop_pred_scale(0); headerR.set_loop_hist1(0); headerR.set_loop_hist2(0);
    headerR.set_unknown_constants(); headerR.set_adpcm_data_size_bytes(adpcm_R_data_bytes);
    headerR.set_offset_to_adpcm_data(0xC0 + adpcm_L_data_bytes);
    std::ofstream out(ds2Path, std::ios::binary);
    if (!out.is_open()) { MessageBox(NULL, (L"DS2 Encode: Failed to create output file: " + ds2Path).c_str(), L"Error", MB_OK | MB_ICONERROR); return false; }
    out.write(reinterpret_cast<const char*>(headerL.raw), sizeof(headerL.raw));
    out.write(reinterpret_cast<const char*>(headerR.raw), sizeof(headerR.raw));
    out.write(reinterpret_cast<const char*>(adpcm_L_data.data()), adpcm_L_data.size());
    out.write(reinterpret_cast<const char*>(adpcm_R_data.data()), adpcm_R_data.size());
    out.close(); return true;
}

// EncodeWavToMonoDsp (unchanged)
bool EncodeWavToMonoDsp(const String& wavPath, const String& dspPath) {
    WavData wav = ReadWavFile(wavPath);
    if (!wav.valid) { MessageBox(NULL, (L"DSP Encode: Failed to read WAV: " + wavPath).c_str(), L"Error", MB_OK | MB_ICONERROR); return false; }
    if (wav.totalSamplesPerChannel == 0) { MessageBox(NULL, (L"DSP Encode: WAV has zero samples: " + wavPath).c_str(), L"Error", MB_OK | MB_ICONERROR); return false; }
    if (wav.pcmSamplesLeft.empty()) {
        MessageBox(NULL, (L"DSP Encode: WAV has missing L channel data (after read): " + wavPath).c_str(), L"Error", MB_OK | MB_ICONERROR); return false;
    }
    const std::vector<int16_t>& monoPcmData = wav.pcmSamplesLeft;
    uint32_t totalSamples = wav.totalSamplesPerChannel;
    const int16_t dsp_coefs[16] = { 2048, 0, 0, 0, 4096, -2048, 2048, -2048, 3072, -1024, 1024, 512, 512, 256, 2048, 1024 };
    int16_t initialHist1 = 0, initialHist2 = 0;
    uint16_t predScale_first = 0;
    std::vector<uint8_t> adpcm_data = EncodeChannelADPCM(monoPcmData, totalSamples, initialHist1, initialHist2, dsp_coefs, predScale_first);
    uint32_t adpcm_data_bytes = static_cast<uint32_t>(adpcm_data.size());
    uint32_t expected_adpcm_bytes = ((totalSamples + 13) / 14) * 8;
    if (adpcm_data_bytes != expected_adpcm_bytes) { MessageBox(NULL, (L"DSP Encode: ADPCM size mismatch for " + wavPath).c_str(), L"Error", MB_OK | MB_ICONERROR); return false; }
    DspChannelHeader header;
    header.set_num_samples(totalSamples);
    header.set_num_adpcm_nibbles(((totalSamples + 13) / 14) * 16);
    header.set_sample_rate(wav.sampleRate);
    header.set_loop_flag(0); header.set_format_info(0x0000);
    header.set_loop_start_nibble_addr(0); header.set_loop_end_sample_addr(totalSamples);
    header.set_current_adpcm_addr(0); header.set_coeffs(dsp_coefs); header.set_gain(0);
    header.set_initial_pred_scale(predScale_first); header.set_initial_hist1(0); header.set_initial_hist2(0);
    header.set_loop_pred_scale(0); header.set_loop_hist1(0); header.set_loop_hist2(0);
    header.set_unknown_constants(); header.set_adpcm_data_size_bytes(adpcm_data_bytes);
    header.set_offset_to_adpcm_data(0x60);
    std::ofstream out(dspPath, std::ios::binary);
    if (!out.is_open()) { MessageBox(NULL, (L"DSP Encode: Failed to create output file: " + dspPath).c_str(), L"Error", MB_OK | MB_ICONERROR); return false; }
    out.write(reinterpret_cast<const char*>(header.raw), sizeof(header.raw));
    out.write(reinterpret_cast<const char*>(adpcm_data.data()), adpcm_data.size());
    out.close(); return true;
}

// *** MODIFIED: DecodeMonoDspToWav with MessageBox error pop-ups ***
// *** MODIFIED: DecodeMonoDspToWav with a 6-byte size cushion ***
bool DecodeMonoDspToWav(const String& dspPath, const String& wavPath, HWND hwndParent) {
    std::ifstream in(dspPath, std::ios::binary | std::ios::ate);
    if (!in) {
        MessageBoxW(hwndParent, (L"Could not open the DSP file for reading:\n\n" + dspPath).c_str(), L"File Open Error", MB_OK | MB_ICONERROR);
        return false;
    }

    size_t fileSize = static_cast<size_t>(in.tellg());
    if (fileSize < 0x60) {
        std::wstringstream ss;
        ss << L"The file is too small to be a valid DSP file.\n\n"
            << L"File: " << dspPath << L"\n"
            << L"Size: " << fileSize << L" bytes (requires at least 96 bytes for the header).";
        MessageBoxW(hwndParent, ss.str().c_str(), L"DSP Decode Error", MB_OK | MB_ICONERROR);
        return false;
    }

    in.seekg(0);
    DspChannelHeader header;
    if (!in.read(reinterpret_cast<char*>(header.raw), sizeof(header.raw))) {
        MessageBoxW(hwndParent, (L"Failed to read the 96-byte header from the DSP file:\n\n" + dspPath).c_str(), L"File Read Error", MB_OK | MB_ICONERROR);
        return false;
    }

    uint32_t totalSamples = header.get_num_samples();
    if (totalSamples == 0) {
        MessageBoxW(hwndParent, (L"The file header reports zero audio samples, so there is nothing to decode.\n\nFile: " + dspPath).c_str(), L"DSP Decode Error", MB_OK | MB_ICONWARNING);
        return false;
    }

    uint32_t sampleRate = header.get_sample_rate();
    if (sampleRate == 0) {
        MessageBoxW(hwndParent, (L"The file header reports a sample rate of 0 Hz, which is invalid.\n\nFile: " + dspPath).c_str(), L"DSP Decode Error", MB_OK | MB_ICONERROR);
        return false;
    }

    uint32_t offset_ADPCM = header.get_offset_to_adpcm_data();
    uint32_t calculatedAdpcmDataSizeBytes = ((totalSamples + 13) / 14) * 8;

    // MODIFIED CHECK: Allow the file to be up to 6 bytes smaller than the calculated size.
    // This accounts for encoders that truncate the final, partially-used ADPCM block.
    if (offset_ADPCM + calculatedAdpcmDataSizeBytes > fileSize + 6) {
        std::wstringstream ss;
        ss << L"The file is corrupt or severely truncated.\n"
            << L"It is more than 6 bytes smaller than required by the header.\n\n"
            << L"File: " << dspPath << L"\n\n"
            << L"File Size: " << fileSize << L" bytes\n"
            << L"Required Total Size: " << (offset_ADPCM + calculatedAdpcmDataSizeBytes) << L" bytes\n\n"
            << L"The file is " << ((offset_ADPCM + calculatedAdpcmDataSizeBytes) - fileSize) << L" bytes too small.";
        MessageBoxW(hwndParent, ss.str().c_str(), L"DSP Decode Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // --- The rest of the function remains the same ---
    int16_t coefs[16];
    header.get_coeffs(coefs);
    int16_t hist1 = header.get_initial_hist1();
    int16_t hist2 = header.get_initial_hist2();

    std::vector<uint8_t> fileData(fileSize);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(fileData.data()), fileSize);
    in.close();

    std::vector<int16_t> monoSamples(totalSamples);

    auto decodeChannel = [&](uint32_t adpcm_data_start_offset, uint32_t num_channel_samples,
        int16_t& h1, int16_t& h2, const int16_t* channel_coefs,
        std::vector<int16_t>& out_samples, const uint8_t* all_file_data,
        size_t total_file_size) {
            size_t pcm_idx = 0; uint32_t adpcm_block_offset = adpcm_data_start_offset;
            while (pcm_idx < num_channel_samples) {
                if (adpcm_block_offset + 8 > total_file_size) break; // This check prevents reading past the actual end of the file
                uint8_t hdr_byte = all_file_data[adpcm_block_offset];
                int predIdx = (hdr_byte >> 4) & 0x0F; if (predIdx > 7) predIdx = 7;
                int shift = hdr_byte & 0x0F; int scale = 1 << shift;
                int16_t c1 = channel_coefs[predIdx * 2 + 0]; int16_t c2 = channel_coefs[predIdx * 2 + 1];
                for (int n = 0; n < 14 && pcm_idx < num_channel_samples; ++n) {
                    if (adpcm_block_offset + 1 + (n >> 1) >= total_file_size) break; // This check prevents reading nibbles that don't exist
                    uint8_t byte_val = all_file_data[adpcm_block_offset + 1 + (n >> 1)];
                    int8_t nib = (n & 1) ? (byte_val & 0x0F) : (byte_val >> 4);
                    if (nib & 0x8) nib |= 0xF0;
                    int32_t sVal = static_cast<int32_t>(nib) * scale; sVal <<= 11;
                    int32_t prediction = static_cast<int32_t>(c1) * h1 + static_cast<int32_t>(c2) * h2;
                    int32_t sum = sVal + prediction + 1024;
                    int16_t sample_out = static_cast<int16_t>(clamp16(sum >> 11));
                    h2 = h1; h1 = sample_out; out_samples[pcm_idx++] = sample_out;
                }
                adpcm_block_offset += 8;
            }
        };
    decodeChannel(offset_ADPCM, totalSamples, hist1, hist2, coefs, monoSamples, fileData.data(), fileSize);
    WriteWav(wavPath, monoSamples, sampleRate, 1);
    return true;
}

// SelectFolderDialog (unchanged)
String SelectFolderDialog(HWND hwndOwner, const wchar_t* title) {
    wchar_t path[MAX_PATH]; BROWSEINFOW bi = { 0 }; bi.hwndOwner = hwndOwner;
    bi.lpszTitle = title; bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != NULL) {
        if (SHGetPathFromIDListW(pidl, path)) { CoTaskMemFree(pidl); return String(path); }
        CoTaskMemFree(pidl);
    }
    return L"";
}

// GetFileName and GetFileExtension (unchanged)
String GetFileName(const String& filePath) {
    size_t lastSlash = filePath.find_last_of(L"\\/");
    return (lastSlash != String::npos) ? filePath.substr(lastSlash + 1) : filePath;
}
String GetFileExtension(const String& fileName) {
    size_t dotPos = fileName.find_last_of(L".");
    if (dotPos != String::npos) {
        String ext = fileName.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        return ext;
    }
    return L"";
}


// *** MODIFIED FUNCTION for Recursive Batch Processing with HWND ***
void RecursiveBatchProcess(
    const String& currentDirPath,
    const String& operationDesc,
    const String& outputSubfolderName,
    const String& targetInputExtensionNoDot,
    const String& outputExtensionWithDot,
    // MODIFICATION: The function pointer can now accept an HWND
    void* conversion_function_ptr, // Use void* for generic pointer
    bool takes_hwnd, // Flag to know which function type we have
    HWND hStatusLabel,
    HWND hMainWindow,
    int& totalFilesProcessed,
    int& totalFilesSucceeded,
    int& totalFilesFailed
) {
    String searchPattern = currentDirPath + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    std::vector<String> subDirectoriesToScan;
    bool outputDirCreatedForThisLevel = false;

    do {
        String itemName = findData.cFileName;
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (itemName != L"." && itemName != L".." && itemName != outputSubfolderName) {
                subDirectoriesToScan.push_back(currentDirPath + L"\\" + itemName);
            }
        }
        else {
            String fileExt = GetFileExtension(itemName);
            if (fileExt == targetInputExtensionNoDot) {

                String outDirForThisLevel = currentDirPath + L"\\" + outputSubfolderName;
                if (!outputDirCreatedForThisLevel) {
                    if (!CreateDirectoryW(outDirForThisLevel.c_str(), NULL)) {
                        if (GetLastError() != ERROR_ALREADY_EXISTS) {
                            // Error handling
                        }
                    }
                    outputDirCreatedForThisLevel = true;
                }

                totalFilesProcessed++;

                String inFile = currentDirPath + L"\\" + itemName;
                String baseName = itemName;
                size_t dotPos = baseName.find_last_of(L'.');
                if (dotPos != String::npos) {
                    baseName = baseName.substr(0, dotPos);
                }
                String outFile = outDirForThisLevel + L"\\" + baseName + outputExtensionWithDot;

                std::wstringstream ss;
                String displayFileName = GetFileName(inFile);
                if (displayFileName.length() > 30) {
                    displayFileName = displayFileName.substr(0, 27) + L"...";
                }
                ss << operationDesc << L" (" << totalFilesProcessed << "): " << displayFileName;
                SetWindowText(hStatusLabel, ss.str().c_str());
                UpdateWindow(hMainWindow);
                PeekMessage(NULL, NULL, 0, 0, PM_REMOVE);

                bool success = false;
                if (takes_hwnd) {
                    // Cast to the function type that takes an HWND
                    auto func = reinterpret_cast<bool(*)(const String&, const String&, HWND)>(conversion_function_ptr);
                    success = func(inFile, outFile, hMainWindow);
                }
                else {
                    // Cast to the function type that does NOT take an HWND
                    auto func = reinterpret_cast<bool(*)(const String&, const String&)>(conversion_function_ptr);
                    success = func(inFile, outFile);
                }

                if (!success) {
                    totalFilesFailed++;
                }
                else {
                    totalFilesSucceeded++;
                }
            }
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    for (const auto& subDir : subDirectoriesToScan) {
        RecursiveBatchProcess(subDir, operationDesc, outputSubfolderName, targetInputExtensionNoDot, outputExtensionWithDot,
            conversion_function_ptr, takes_hwnd, hStatusLabel, hMainWindow, totalFilesProcessed, totalFilesSucceeded, totalFilesFailed);
    }
}


// *** MODIFIED WndProc to handle new function calls ***
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND btnDecDs2S, btnEncDs2S, stat,
        btnDecDs2B, btnEncDs2B,
        btnDecDspS, btnEncDspS,
        btnDecDspB, btnEncDspB;

    int btnWidth = 200;
    int btnHeight = 30;
    int x1 = 10;
    int x2 = x1 + btnWidth + 15;
    int y_row1 = 30;
    int y_row2 = y_row1 + btnHeight + 10;
    int y_row3 = y_row2 + btnHeight + 40;
    int y_row4 = y_row3 + btnHeight + 10;
    int y_status = y_row4 + btnHeight + 20;

    switch (msg) {
    case WM_CREATE:
        CreateWindow(L"STATIC", L"DS2 (Stereo)", WS_VISIBLE | WS_CHILD | SS_LEFT, x1, 5, btnWidth * 2 + 15, 20, hwnd, (HMENU)(INT_PTR)-1, hInst, NULL);
        btnDecDs2S = CreateWindow(L"BUTTON", L"DS2 → WAV (Single)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x1, y_row1, btnWidth, btnHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_DEC_DS2_SINGLE, hInst, NULL);
        btnEncDs2S = CreateWindow(L"BUTTON", L"WAV → DS2 (Single)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x2, y_row1, btnWidth, btnHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_ENC_DS2_SINGLE, hInst, NULL);
        btnDecDs2B = CreateWindow(L"BUTTON", L"DS2 → WAV (Batch)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x1, y_row2, btnWidth, btnHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_DEC_DS2_BATCH, hInst, NULL);
        btnEncDs2B = CreateWindow(L"BUTTON", L"WAV → DS2 (Batch)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x2, y_row2, btnWidth, btnHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_ENC_DS2_BATCH, hInst, NULL);

        CreateWindow(L"STATIC", L"DSP (Mono)", WS_VISIBLE | WS_CHILD | SS_LEFT, x1, y_row2 + btnHeight + 15, btnWidth * 2 + 15, 20, hwnd, (HMENU)(INT_PTR)-1, hInst, NULL);
        btnDecDspS = CreateWindow(L"BUTTON", L"DSP → WAV (Single)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x1, y_row3, btnWidth, btnHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_DEC_DSP_SINGLE, hInst, NULL);
        btnEncDspS = CreateWindow(L"BUTTON", L"WAV → DSP (Single)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x2, y_row3, btnWidth, btnHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_ENC_DSP_SINGLE, hInst, NULL);
        btnDecDspB = CreateWindow(L"BUTTON", L"DSP → WAV (Batch)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x1, y_row4, btnWidth, btnHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_DEC_DSP_BATCH, hInst, NULL);
        btnEncDspB = CreateWindow(L"BUTTON", L"WAV → DSP (Batch)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x2, y_row4, btnWidth, btnHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_ENC_DSP_BATCH, hInst, NULL);

        stat = CreateWindow(L"STATIC", L"Ready", WS_VISIBLE | WS_CHILD | SS_LEFTNOWORDWRAP,
            10, y_status, btnWidth * 2 + 15, 40, hwnd, (HMENU)(INT_PTR)IDC_STATUS, hInst, NULL);
        break;

    case WM_COMMAND:
        // Single file operations
        if (LOWORD(wp) == IDC_BTN_DEC_DS2_SINGLE) {
            String in = OpenFileDialog(L"Stereo DS2 Files\0*.ds2\0All Files\0*.*\0");
            if (!in.empty()) {
                String out = in.substr(0, in.find_last_of(L".")) + L".wav"; SetWindowText(stat, L"DS2→WAV: Decoding...");
                if (DecodeDS2toWav(in, out)) SetWindowText(stat, (L"DS2→WAV Done: " + GetFileName(out)).c_str());
                else SetWindowText(stat, L"DS2→WAV: Failed.");
            }
        }
        else if (LOWORD(wp) == IDC_BTN_ENC_DS2_SINGLE) {
            String in = OpenFileDialog(L"WAV Files (Stereo/Mono)\0*.wav\0All Files\0*.*\0");
            if (!in.empty()) {
                String out = in.substr(0, in.find_last_of(L".")) + L".ds2"; SetWindowText(stat, L"WAV→DS2: Encoding...");
                if (EncodeWavToDS2(in, out)) SetWindowText(stat, (L"WAV→DS2 Done: " + GetFileName(out)).c_str());
            }
        }
        else if (LOWORD(wp) == IDC_BTN_DEC_DSP_SINGLE) {
            String in = OpenFileDialog(L"Mono DSP Files\0*.dsp\0All Files\0*.*\0");
            if (!in.empty()) {
                String out = in.substr(0, in.find_last_of(L".")) + L".wav"; SetWindowText(stat, L"DSP→WAV: Decoding...");
                // MODIFIED: Pass 'hwnd' to the function
                if (DecodeMonoDspToWav(in, out, hwnd)) {
                    SetWindowText(stat, (L"DSP→WAV Done: " + GetFileName(out)).c_str());
                }
                else {
                    SetWindowText(stat, L"DSP→WAV: Failed. See pop-up for details.");
                }
            }
        }
        else if (LOWORD(wp) == IDC_BTN_ENC_DSP_SINGLE) {
            String in = OpenFileDialog(L"WAV Files (Stereo/Mono)\0*.wav\0All Files\0*.*\0");
            if (!in.empty()) {
                String out = in.substr(0, in.find_last_of(L".")) + L".dsp"; SetWindowText(stat, L"WAV→DSP: Encoding...");
                if (EncodeWavToMonoDsp(in, out)) SetWindowText(stat, (L"WAV→DSP Done: " + GetFileName(out)).c_str());
            }
        }
        // Batch operations
        else if (LOWORD(wp) == IDC_BTN_DEC_DS2_BATCH) {
            String folderPath = SelectFolderDialog(hwnd, L"Select Root Folder (Recursive DS2 → WAV)");
            if (!folderPath.empty()) {
                SetWindowText(stat, L"DS2→WAV Batch: Scanning..."); UpdateWindow(hwnd);
                int successCount = 0, failCount = 0, processedCount = 0;
                RecursiveBatchProcess(folderPath, L"DS2→WAV", L"converted_stereo_wav", L"ds2", L".wav",
                    (void*)DecodeDS2toWav, false, stat, hwnd, processedCount, successCount, failCount);
                std::wstringstream summary;
                summary << L"DS2→WAV Batch Done. Processed: " << processedCount << L", OK: " << successCount << L", Failed: " << failCount;
                SetWindowText(stat, summary.str().c_str());
            }
            else { SetWindowText(stat, L"DS2→WAV Batch: Cancelled."); }
        }
        else if (LOWORD(wp) == IDC_BTN_ENC_DS2_BATCH) {
            String folderPath = SelectFolderDialog(hwnd, L"Select Root Folder (Recursive WAV → DS2)");
            if (!folderPath.empty()) {
                SetWindowText(stat, L"WAV→DS2 Batch: Scanning..."); UpdateWindow(hwnd);
                int successCount = 0, failCount = 0, processedCount = 0;
                RecursiveBatchProcess(folderPath, L"WAV→DS2", L"converted_stereo_ds2", L"wav", L".ds2",
                    (void*)EncodeWavToDS2, false, stat, hwnd, processedCount, successCount, failCount);
                std::wstringstream summary;
                summary << L"WAV→DS2 Batch Done. Processed: " << processedCount << L", OK: " << successCount << L", Failed: " << failCount;
                SetWindowText(stat, summary.str().c_str());
            }
            else { SetWindowText(stat, L"WAV→DS2 Batch: Cancelled."); }
        }
        else if (LOWORD(wp) == IDC_BTN_DEC_DSP_BATCH) {
            String folderPath = SelectFolderDialog(hwnd, L"Select Root Folder (Recursive DSP → WAV)");
            if (!folderPath.empty()) {
                SetWindowText(stat, L"DSP→WAV Batch: Scanning..."); UpdateWindow(hwnd);
                int successCount = 0, failCount = 0, processedCount = 0;
                // MODIFIED: Pass the new function pointer and set the flag to true
                RecursiveBatchProcess(folderPath, L"DSP→WAV", L"converted_mono_wav_from_dsp", L"dsp", L".wav",
                    (void*)DecodeMonoDspToWav, true, stat, hwnd, processedCount, successCount, failCount);
                std::wstringstream summary;
                summary << L"DSP→WAV Batch Done. Processed: " << processedCount << L", OK: " << successCount << L", Failed: " << failCount;
                SetWindowText(stat, summary.str().c_str());
            }
            else { SetWindowText(stat, L"DSP→WAV Batch: Cancelled."); }
        }
        else if (LOWORD(wp) == IDC_BTN_ENC_DSP_BATCH) {
            String folderPath = SelectFolderDialog(hwnd, L"Select Root Folder (Recursive WAV → DSP)");
            if (!folderPath.empty()) {
                SetWindowText(stat, L"WAV→DSP Batch: Scanning..."); UpdateWindow(hwnd);
                int successCount = 0, failCount = 0, processedCount = 0;
                RecursiveBatchProcess(folderPath, L"WAV→DSP", L"converted_mono_dsp", L"wav", L".dsp",
                    (void*)EncodeWavToMonoDsp, false, stat, hwnd, processedCount, successCount, failCount);
                std::wstringstream summary;
                summary << L"WAV→DSP Batch Done. Processed: " << processedCount << L", OK: " << successCount << L", Failed: " << failCount;
                SetWindowText(stat, summary.str().c_str());
            }
            else { SetWindowText(stat, L"WAV→DSP Batch: Cancelled."); }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

// wWinMain (unchanged)
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int cmdShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    hInst = hInstance;
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0,
        hInstance, NULL, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1),
        NULL, L"DS2DSPConvClass", NULL };
    RegisterClassEx(&wc);

    int windowWidth = 445;
    int windowHeight = 310;

    HWND hwnd = CreateWindow(L"DS2DSPConvClass", L"DS2 (Stereo) & DSP (Mono) Converter v2.2",
        WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX),
        CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
        NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, cmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CoUninitialize();
    return 0;
}

// OpenFileDialog (unchanged)
String OpenFileDialog(const wchar_t* filter) {
    OPENFILENAMEW ofn{};
    wchar_t buf[MAX_PATH]{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrDefExt = L"";
    if (GetOpenFileNameW(&ofn)) return buf;
    return L"";
}
