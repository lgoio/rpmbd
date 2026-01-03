#pragma once
#include <cstddef>
#include <cstdint>

// RPMB frame layout (512 bytes), offsets match spec / Linux kernel.

static const size_t RPMB_FRAME_SIZE = 512;

// 0x000 .. 0x0C3: reserved / "stuff" (196 bytes)
static const size_t OFF_STUFF       = 0x000;  // 196 bytes

// MAC (HMAC-SHA256), 32 bytes
static const size_t OFF_MAC         = 0x0C4;
static const size_t MAC_LEN         = 32;

// Data payload, 256 bytes
static const size_t OFF_DATA        = 0x0E4;

// Nonce, 16 bytes
static const size_t OFF_NONCE       = 0x1E4;

// Write counter, 4 bytes (big-endian)
static const size_t OFF_WCOUNTER    = 0x1F4;

// Address, 2 bytes (big-endian)
static const size_t OFF_ADDR        = 0x1F8;

// Block count, 2 bytes (big-endian)
static const size_t OFF_BLOCK_COUNT = 0x1FA;

// Result, 2 bytes (big-endian)
static const size_t OFF_RESULT      = 0x1FC;

// Request/response type, 2 bytes (big-endian)
static const size_t OFF_REQRESP     = 0x1FE;

// Request types
static const uint16_t RPMB_REQ_PROGRAM_KEY   = 0x0001;
static const uint16_t RPMB_REQ_GET_COUNTER   = 0x0002;
static const uint16_t RPMB_REQ_DATA_WRITE    = 0x0003;
static const uint16_t RPMB_REQ_DATA_READ     = 0x0004;
static const uint16_t RPMB_REQ_RESULT_READ   = 0x0005;

// Response types
static const uint16_t RPMB_RESP_PROGRAM_KEY  = 0x0100;
static const uint16_t RPMB_RESP_GET_COUNTER  = 0x0200;
static const uint16_t RPMB_RESP_DATA_WRITE   = 0x0300;
static const uint16_t RPMB_RESP_DATA_READ    = 0x0400;
static const uint16_t RPMB_RESP_RESULT_READ  = 0x0500;

// Result codes
static const uint16_t RPMB_RES_OK            = 0x0000;
static const uint16_t RPMB_RES_GENERAL_FAIL  = 0x0001;
static const uint16_t RPMB_RES_AUTH_FAIL     = 0x0002;
static const uint16_t RPMB_RES_COUNTER_FAIL  = 0x0003;
static const uint16_t RPMB_RES_ADDR_FAIL     = 0x0004;
static const uint16_t RPMB_RES_WRITE_FAIL    = 0x0005;
static const uint16_t RPMB_RES_READ_FAIL     = 0x0006;
static const uint16_t RPMB_RES_NO_KEY        = 0x0007;
