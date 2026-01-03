#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

class Rpmbd {
public:
    struct Options {
        std::string stateFile = "rpmb_state.bin";
        uint32_t maxBlocks = 128;
        bool allowRekey = false;
        bool debug = true;
    };

    Rpmbd(const Options& opt);
    ~Rpmbd();

    void HandleWriteRequestFrames(const uint8_t* data, size_t len);
    void ReadResponseFrames(uint8_t* out, size_t len);

    // Must be called by the CUSE layer before CMD18 reads responses
    void FinalizePendingRead(uint16_t blkCntFromCmd18);

    // True if a DATA_READ request is pending
    bool HasPendingRead() const { return pendingRead_.valid; }

private:
    Options opt_;

    bool keyProgrammed_ = false;
    uint8_t key_[32]{};
    uint32_t writeCounter_ = 0;
    std::vector<uint8_t> storage_;

    std::vector<uint8_t> respQueue_;

    struct LastResult {
        bool valid = false;
        uint16_t respType = 0;
        uint16_t result = 0;
        uint16_t addr = 0;
        uint16_t blkCnt = 0;
        uint8_t nonce[16]{};
        uint32_t wc = 0;
    } lastResult_;

    struct PendingRead {
        bool valid = false;
        uint16_t addr = 0;
        uint8_t nonce[16]{};
    } pendingRead_;

    static uint16_t Be16(const uint8_t* p);
    static uint32_t Be32(const uint8_t* p);
    static void SetBe16(uint8_t* p, uint16_t v);
    static void SetBe32(uint8_t* p, uint32_t v);

    void LoadState();
    void SaveState();

    bool StorageAddrValid(uint16_t addr, uint16_t count) const;
    bool ReadBlock(uint16_t addr, uint8_t out256[256]) const;
    void WriteBlock(uint16_t addr, const uint8_t in256[256]);

    void ComputeMac284(const uint8_t* frame, uint8_t macOut[32]) const;
    void ComputeMac284_Multi(const uint8_t* frames, uint16_t blkCnt, uint8_t outMac[32]) const;
    bool VerifyMac284(const uint8_t* frame) const;

    void MakeResponse(uint16_t respType,
                      uint16_t result,
                      uint32_t writeCounter,
                      const uint8_t* data256,
                      uint16_t addr,
                      uint16_t count,
                      const uint8_t* nonce16,
                      bool addMac);

    void ProcessRequest(const uint8_t* frame512,
                        const uint8_t* allFramesBase,
                        size_t framesTotal);

    void HandleProgramKey(const uint8_t* req);
    void HandleGetCounter(const uint8_t* req);
    void HandleDataWrite(const uint8_t* firstFrame,
                         const uint8_t* allFramesBase,
                         size_t framesTotal);

    // DATA_READ: only store request parameters, response is generated later
    void StartPendingRead(const uint8_t* req);

    void HandleResultRead(const uint8_t* req);
};
