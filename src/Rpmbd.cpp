#include "Rpmbd.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <openssl/hmac.h>

#include "RpmbFrame.h"

static inline void DBG(bool en, const char* fmt, ...) {
    if (!en) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    va_end(ap);
}

Rpmbd::Rpmbd(const Options& opt) : opt_(opt) {
    storage_.resize(opt_.maxBlocks * 256, 0);
    LoadState();
}

Rpmbd::~Rpmbd() {
    SaveState();
}

uint16_t Rpmbd::Be16(const uint8_t* p) {
    return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}
uint32_t Rpmbd::Be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) |
           (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |
           (uint32_t(p[3])      );
}
void Rpmbd::SetBe16(uint8_t* p, uint16_t v) {
    p[0] = (v >> 8) & 0xff;
    p[1] = (v) & 0xff;
}
void Rpmbd::SetBe32(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;
    p[3] = (v) & 0xff;
}

// ----------------------------------------------------------------------

bool Rpmbd::StorageAddrValid(uint16_t addr, uint16_t count) const {
    if (count == 0) return false;
    uint32_t end = uint32_t(addr) + uint32_t(count);
    return end <= opt_.maxBlocks;
}

bool Rpmbd::ReadBlock(uint16_t addr, uint8_t out256[256]) const {
    if (!StorageAddrValid(addr, 1)) return false;
    const size_t off = size_t(addr) * 256;
    std::memcpy(out256, &storage_[off], 256);
    return true;
}

void Rpmbd::WriteBlock(uint16_t addr, const uint8_t in256[256]) {
    if (!StorageAddrValid(addr, 1)) return;
    const size_t off = size_t(addr) * 256;
    std::memcpy(&storage_[off], in256, 256);
}

// ----------------------------------------------------------------------
// MAC over 284 bytes starting at OFF_DATA
void Rpmbd::ComputeMac284(const uint8_t* frame, uint8_t macOut[32]) const {
    const uint8_t* region = frame + OFF_DATA;
    const size_t regionLen = 284;

    unsigned int outLen = 0;
    HMAC(EVP_sha256(), key_, 32, region, regionLen, macOut, &outLen);
}

bool Rpmbd::VerifyMac284(const uint8_t* frame) const {
    uint8_t mac[32];
    ComputeMac284(frame, mac);
    return std::memcmp(mac, frame + OFF_MAC, 32) == 0;
}

// Multi-block MAC: concat all 284-byte regions, store MAC in last frame
void Rpmbd::ComputeMac284_Multi(const uint8_t* frames, uint16_t blkCnt, uint8_t outMac[32]) const {
    HMAC_CTX* ctx = HMAC_CTX_new();
    HMAC_Init_ex(ctx, key_, 32, EVP_sha256(), nullptr);

    for (uint16_t i = 0; i < blkCnt; ++i) {
        const uint8_t* f = frames + size_t(i) * 512;
        HMAC_Update(ctx, f + OFF_DATA, 284);
    }

    unsigned int len = 0;
    HMAC_Final(ctx, outMac, &len);
    HMAC_CTX_free(ctx);
}

// ----------------------------------------------------------------------

void Rpmbd::LoadState() {
    std::ifstream f(opt_.stateFile.c_str(), std::ios::binary);
    if (!f.good()) {
        DBG(opt_.debug, "[rpmbd] state not found -> init fresh");
        return;
    }

    char magic[8];
    f.read(magic, 8);
    if (std::memcmp(magic, "RPMBDv1", 7) != 0) {
        DBG(opt_.debug, "[rpmbd] state magic mismatch -> ignore");
        return;
    }

    uint8_t kp = 0;
    f.read(reinterpret_cast<char*>(&kp), 1);
    keyProgrammed_ = (kp != 0);

    f.read(reinterpret_cast<char*>(key_), 32);
    f.read(reinterpret_cast<char*>(&writeCounter_), 4);

    uint32_t maxBlocks = 0;
    f.read(reinterpret_cast<char*>(&maxBlocks), 4);

    if (maxBlocks != opt_.maxBlocks) {
        DBG(opt_.debug, "[rpmbd] state maxBlocks mismatch -> reset storage");
        storage_.assign(opt_.maxBlocks * 256, 0);
    } else {
        storage_.resize(opt_.maxBlocks * 256);
        f.read(reinterpret_cast<char*>(storage_.data()), storage_.size());
    }

    DBG(opt_.debug, "[rpmbd] state loaded: keyProg=%d writeCounter=%u",
        keyProgrammed_ ? 1 : 0, writeCounter_);
}

void Rpmbd::SaveState() {
    std::ofstream f(opt_.stateFile.c_str(), std::ios::binary | std::ios::trunc);
    if (!f.good()) return;

    char magic[8] = "RPMBDv1";
    f.write(magic, 8);

    uint8_t kp = keyProgrammed_ ? 1 : 0;
    f.write(reinterpret_cast<char*>(&kp), 1);

    f.write(reinterpret_cast<char*>(key_), 32);
    f.write(reinterpret_cast<char*>(&writeCounter_), 4);

    uint32_t maxBlocks = opt_.maxBlocks;
    f.write(reinterpret_cast<char*>(&maxBlocks), 4);

    f.write(reinterpret_cast<char*>(storage_.data()), storage_.size());

    DBG(opt_.debug, "[rpmbd] SaveState writing to '%s'", opt_.stateFile.c_str());
}

// ----------------------------------------------------------------------

void Rpmbd::MakeResponse(uint16_t respType,
                         uint16_t result,
                         uint32_t writeCounter,
                         const uint8_t* data256,
                         uint16_t addr,
                         uint16_t count,
                         const uint8_t* nonce16,
                         bool addMac)
{
    uint8_t frame[512];
    std::memset(frame, 0, sizeof(frame));

    if (data256) std::memcpy(frame + OFF_DATA, data256, 256);
    if (nonce16) std::memcpy(frame + OFF_NONCE, nonce16, 16);

    SetBe32(frame + OFF_WCOUNTER, writeCounter);
    SetBe16(frame + OFF_ADDR, addr);
    SetBe16(frame + OFF_BLOCK_COUNT, count);
    SetBe16(frame + OFF_RESULT, result);
    SetBe16(frame + OFF_REQRESP, respType);

    if (addMac && keyProgrammed_) {
        uint8_t mac[32];
        ComputeMac284(frame, mac);
        std::memcpy(frame + OFF_MAC, mac, 32);
    }

    respQueue_.insert(respQueue_.end(), frame, frame + 512);
}

// ----------------------------------------------------------------------
// Request handlers

void Rpmbd::HandleProgramKey(const uint8_t* req) {
    const uint8_t* newKey = req + OFF_MAC;

    if (keyProgrammed_ && !opt_.allowRekey) {
        MakeResponse(RPMB_RESP_PROGRAM_KEY, RPMB_RES_GENERAL_FAIL,
                     writeCounter_, nullptr, 0, 0, nullptr, false);
        return;
    }

    std::memcpy(key_, newKey, 32);
    keyProgrammed_ = true;
    SaveState();

    MakeResponse(RPMB_RESP_PROGRAM_KEY, RPMB_RES_OK,
                 writeCounter_, nullptr, 0, 0, nullptr, false);
}

void Rpmbd::HandleGetCounter(const uint8_t* req) {
    const uint8_t* nonce = req + OFF_NONCE;

    if (!keyProgrammed_) {
        MakeResponse(RPMB_RESP_GET_COUNTER, RPMB_RES_NO_KEY,
                     writeCounter_, nullptr, 0, 0, nonce, false);
        return;
    }

    MakeResponse(RPMB_RESP_GET_COUNTER, RPMB_RES_OK,
                 writeCounter_, nullptr, 0, 0, nonce, true);
}

void Rpmbd::HandleDataWrite(const uint8_t* firstFrame,
                            const uint8_t* allFramesBase,
                            size_t framesTotal)
{
    const uint16_t addr   = Be16(firstFrame + OFF_ADDR);
    const uint16_t blkCnt = Be16(firstFrame + OFF_BLOCK_COUNT);
    const uint32_t wcReq  = Be32(firstFrame + OFF_WCOUNTER);

    if (!keyProgrammed_) {
        MakeResponse(RPMB_RESP_DATA_WRITE, RPMB_RES_NO_KEY,
                     writeCounter_, nullptr, addr, blkCnt, nullptr, false);
        return;
    }

    if (blkCnt == 0 || blkCnt != framesTotal) {
        MakeResponse(RPMB_RESP_DATA_WRITE, RPMB_RES_GENERAL_FAIL,
                     writeCounter_, nullptr, addr, blkCnt, nullptr, false);
        return;
    }

    if (!StorageAddrValid(addr, blkCnt)) {
        MakeResponse(RPMB_RESP_DATA_WRITE, RPMB_RES_ADDR_FAIL,
                     writeCounter_, nullptr, addr, blkCnt, nullptr, false);
        return;
    }

    for (size_t i = 0; i < framesTotal; ++i) {
        const uint8_t* f = allFramesBase + i * 512;
        if (!VerifyMac284(f)) {
            MakeResponse(RPMB_RESP_DATA_WRITE, RPMB_RES_AUTH_FAIL,
                         writeCounter_, nullptr, addr, blkCnt, nullptr, false);
            return;
        }
    }

    if (wcReq != writeCounter_) {
        MakeResponse(RPMB_RESP_DATA_WRITE, RPMB_RES_COUNTER_FAIL,
                     writeCounter_, nullptr, addr, blkCnt, nullptr, false);
        return;
    }

    for (uint16_t i = 0; i < blkCnt; ++i) {
        const uint8_t* f = allFramesBase + i * 512;
        WriteBlock(addr + i, f + OFF_DATA);
    }

    writeCounter_++;
    SaveState();

    MakeResponse(RPMB_RESP_DATA_WRITE, RPMB_RES_OK,
                 writeCounter_, nullptr, addr, blkCnt, nullptr, false);
}

// DATA_READ: store request only, response is generated later
void Rpmbd::StartPendingRead(const uint8_t* req) {
    respQueue_.clear(); // important: drop old responses

    pendingRead_.valid = true;
    pendingRead_.addr = Be16(req + OFF_ADDR);
    std::memcpy(pendingRead_.nonce, req + OFF_NONCE, 16);
}

// Called by CUSE layer when CMD18 block count is known
void Rpmbd::FinalizePendingRead(uint16_t blkCnt) {
    if (!pendingRead_.valid) return;
    pendingRead_.valid = false;

    if (blkCnt == 0) blkCnt = 1;

    const uint16_t addr = pendingRead_.addr;
    const uint8_t* nonce = pendingRead_.nonce;

    respQueue_.clear();

    if (!keyProgrammed_) {
        MakeResponse(RPMB_RESP_DATA_READ, RPMB_RES_NO_KEY,
                     writeCounter_, nullptr, addr, blkCnt, nonce, false);
        return;
    }

    if (!StorageAddrValid(addr, blkCnt)) {
        MakeResponse(RPMB_RESP_DATA_READ, RPMB_RES_ADDR_FAIL,
                     writeCounter_, nullptr, addr, blkCnt, nonce, false);
        return;
    }

    std::vector<uint8_t> frames;
    frames.resize(size_t(blkCnt) * 512);
    std::memset(frames.data(), 0, frames.size());

    for (uint16_t i = 0; i < blkCnt; ++i) {
        uint8_t* f = frames.data() + size_t(i) * 512;

        uint8_t data[256];
        if (!ReadBlock(addr + i, data)) {
            MakeResponse(RPMB_RESP_DATA_READ, RPMB_RES_READ_FAIL,
                         writeCounter_, nullptr, addr, blkCnt, nonce, false);
            return;
        }

        std::memcpy(f + OFF_DATA, data, 256);
        std::memcpy(f + OFF_NONCE, nonce, 16);

        SetBe32(f + OFF_WCOUNTER, writeCounter_);
        SetBe16(f + OFF_ADDR, addr + i);
        SetBe16(f + OFF_BLOCK_COUNT, blkCnt);
        SetBe16(f + OFF_RESULT, RPMB_RES_OK);
        SetBe16(f + OFF_REQRESP, RPMB_RESP_DATA_READ);
    }

    uint8_t mac[32];
    ComputeMac284_Multi(frames.data(), blkCnt, mac);
    std::memcpy(frames.data() + size_t(blkCnt - 1) * 512 + OFF_MAC, mac, 32);

    respQueue_.insert(respQueue_.end(), frames.begin(), frames.end());
}

// ----------------------------------------------------------------------

void Rpmbd::HandleResultRead(const uint8_t*) {
    // Ignore RESULT_READ while a DATA_READ is still pending
    if (pendingRead_.valid) {
        DBG(opt_.debug, "[rpmbd] RESULT_READ ignored (pending DATA_READ)");
        return;
    }

    if (!respQueue_.empty()) return;

    MakeResponse(RPMB_RESP_RESULT_READ, RPMB_RES_GENERAL_FAIL,
                 writeCounter_, nullptr, 0, 0, nullptr, false);
}

// ----------------------------------------------------------------------
// Dispatcher

void Rpmbd::ProcessRequest(const uint8_t* frame512,
                           const uint8_t* allFramesBase,
                           size_t framesTotal)
{
    uint16_t reqType = Be16(frame512 + OFF_REQRESP);

    switch (reqType) {
    case RPMB_REQ_PROGRAM_KEY:
        respQueue_.clear();
        HandleProgramKey(frame512);
        break;
    case RPMB_REQ_GET_COUNTER:
        respQueue_.clear();
        HandleGetCounter(frame512);
        break;
    case RPMB_REQ_DATA_WRITE:
        respQueue_.clear();
        HandleDataWrite(frame512, allFramesBase, framesTotal);
        break;
    case RPMB_REQ_DATA_READ:
        respQueue_.clear(); // important
        StartPendingRead(frame512);
        break;
    case RPMB_REQ_RESULT_READ:
        // If a read is pending and no response exists yet, generate it now
        if (pendingRead_.valid && respQueue_.empty()) {
            FinalizePendingRead(1); // can be replaced if blkCnt is known earlier
        }
        HandleResultRead(frame512);
        break;
    default:
        respQueue_.clear();
        MakeResponse(RPMB_RESP_RESULT_READ, RPMB_RES_GENERAL_FAIL,
                     writeCounter_, nullptr, 0, 0, nullptr, false);
        break;
    }
}

// ----------------------------------------------------------------------

void Rpmbd::HandleWriteRequestFrames(const uint8_t* data, size_t len) {
    if (len % 512 != 0) return;

    size_t frames = len / 512;
    uint16_t reqType0 = Be16(data + OFF_REQRESP);

    if (reqType0 == RPMB_REQ_DATA_WRITE) {
        ProcessRequest(data, data, frames);
        return;
    }

    for (size_t i = 0; i < frames; ++i) {
        ProcessRequest(data + i * 512, data + i * 512, 1);
    }
}

void Rpmbd::ReadResponseFrames(uint8_t* out, size_t len) {
    if (respQueue_.size() < len) {
        // RPMB expects exact length -> return zeros and log
        std::memset(out, 0, len);
        DBG(opt_.debug, "[rpmbd] ERROR: not enough response data (need=%zu have=%zu)",
            len, respQueue_.size());
        return;
    }

    std::memcpy(out, respQueue_.data(), len);
    respQueue_.erase(respQueue_.begin(), respQueue_.begin() + len);
}
