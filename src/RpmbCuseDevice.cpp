#define FUSE_USE_VERSION 31
#include "RpmbCuseDevice.h"

#include <fuse3/cuse_lowlevel.h>

#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <linux/mmc/ioctl.h>

#include <sys/uio.h>   // process_vm_readv / process_vm_writev
#include <unistd.h>

#include <cstring>
#include <cerrno>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <ctime>

#include "Rpmbd.h"
#include "RpmbFrame.h"

// ------------------------------------------------------------
// Debug helpers
// ------------------------------------------------------------
static bool gRpmbDebug = true;

static void DbgTs() {
    if (!gRpmbDebug) return;
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::fprintf(stderr, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

#define DBG(fmt, ...) do { \
    if (gRpmbDebug) { \
        DbgTs(); \
        std::fprintf(stderr, "[rpmb-cuse] " fmt "\n", ##__VA_ARGS__); \
        std::fflush(stderr); \
    } \
} while(0)

static void HexDump(const char* title, const void* data, size_t len, size_t maxLen = 256) {
    if (!gRpmbDebug) return;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    size_t n = std::min(len, maxLen);

    DbgTs();
    std::fprintf(stderr, "[rpmb-cuse] %s (%zu bytes, showing %zu)\n", title, len, n);
    for (size_t i = 0; i < n; i += 16) {
        DbgTs();
        std::fprintf(stderr, "  %04zx: ", i);
        for (size_t j = 0; j < 16 && (i + j) < n; ++j)
            std::fprintf(stderr, "%02x ", p[i + j]);
        std::fprintf(stderr, "\n");
    }
    std::fflush(stderr);
}

static const char* ErrStr() {
    return std::strerror(errno);
}

// ------------------------------------------------------------
// process_vm helpers (read/write caller buffers)
// ------------------------------------------------------------
static bool ReadFromPid(pid_t pid, uint64_t remoteAddr, void* local, size_t len) {
    struct iovec liov { local, len };
    struct iovec riov { (void*)(uintptr_t)remoteAddr, len };
    ssize_t n = process_vm_readv(pid, &liov, 1, &riov, 1, 0);
    return (n == (ssize_t)len);
}

static bool WriteToPid(pid_t pid, uint64_t remoteAddr, const void* local, size_t len) {
    struct iovec liov { (void*)local, len };
    struct iovec riov { (void*)(uintptr_t)remoteAddr, len };
    ssize_t n = process_vm_writev(pid, &liov, 1, &riov, 1, 0);
    return (n == (ssize_t)len);
}

// ------------------------------------------------------------
// Internal implementation
// ------------------------------------------------------------
class RpmbCuseDevice::Impl {
public:
    Impl(Rpmbd& core, const Options& opt)
        : core_(core), opt_(opt) {}

    Rpmbd& core_;
    Options opt_;

    static void cb_open(fuse_req_t req, struct fuse_file_info* fi);
    static void cb_read(fuse_req_t req, size_t size, off_t off, struct fuse_file_info* fi);
    static void cb_write(fuse_req_t req, const char* buf, size_t size, off_t off, struct fuse_file_info* fi);
    static void cb_ioctl(fuse_req_t req, int cmd, void* arg,
                         struct fuse_file_info* fi, unsigned flags,
                         const void* in_buf, size_t in_bufsz, size_t out_bufsz);

    static const struct cuse_lowlevel_ops ops;

    static Impl* self(fuse_req_t req) {
        return static_cast<Impl*>(fuse_req_userdata(req));
    }

    static size_t CmdDataLen(const mmc_ioc_cmd& c) {
        return size_t(c.blocks) * size_t(c.blksz);
    }

    static void LogFuseCtx(fuse_req_t req) {
        const fuse_ctx* ctx = fuse_req_ctx(req);
        if (!ctx) {
            DBG("fuse_ctx: <null>");
            return;
        }
        DBG("fuse_ctx: pid=%d uid=%u gid=%u umask=0%o",
            (int)ctx->pid, (unsigned)ctx->uid, (unsigned)ctx->gid, (unsigned)ctx->umask);
    }

    static void DumpMmcCmd(const char* prefix, const mmc_ioc_cmd& c) {
        DBG("%s opcode=%u arg=0x%x blocks=%u blksz=%u flags=0x%x data_ptr=0x%llx",
            prefix,
            c.opcode, c.arg, c.blocks, c.blksz, c.flags,
            (unsigned long long)c.data_ptr);
    }
};

const struct cuse_lowlevel_ops RpmbCuseDevice::Impl::ops = []{
    struct cuse_lowlevel_ops o;
    std::memset(&o, 0, sizeof(o));
    o.open  = RpmbCuseDevice::Impl::cb_open;
    o.read  = RpmbCuseDevice::Impl::cb_read;
    o.write = RpmbCuseDevice::Impl::cb_write;
    o.ioctl = RpmbCuseDevice::Impl::cb_ioctl;
    return o;
}();

// ------------------------------------------------------------
// FUSE callbacks
// ------------------------------------------------------------
void RpmbCuseDevice::Impl::cb_open(fuse_req_t req, struct fuse_file_info* fi) {
    DBG("open()");
    fuse_reply_open(req, fi);
}

void RpmbCuseDevice::Impl::cb_read(fuse_req_t req, size_t, off_t, struct fuse_file_info*) {
    DBG("read() -> EOPNOTSUPP");
    fuse_reply_err(req, EOPNOTSUPP);
}

void RpmbCuseDevice::Impl::cb_write(fuse_req_t req, const char*, size_t, off_t, struct fuse_file_info*) {
    DBG("write() -> EOPNOTSUPP");
    fuse_reply_err(req, EOPNOTSUPP);
}

// ------------------------------------------------------------
// IOCTL handler (mmc-utils uses MMC_IOC_MULTI_CMD)
// ------------------------------------------------------------
void RpmbCuseDevice::Impl::cb_ioctl(fuse_req_t req, int cmd, void* arg,
                                   struct fuse_file_info*,
                                   unsigned,
                                   const void* in_buf, size_t in_bufsz,
                                   size_t out_bufsz)
{
    Impl* impl = self(req);
    if (!impl) {
        DBG("ERROR: missing userdata -> EIO");
        fuse_reply_err(req, EIO);
        return;
    }

    LogFuseCtx(req);

    const fuse_ctx* fctx = fuse_req_ctx(req);
    pid_t pid = fctx ? fctx->pid : -1;

    const unsigned int ucmd = static_cast<unsigned int>(cmd);
    DBG("ioctl enter: cmd=%d ucmd=0x%x arg=%p in_buf=%p in_bufsz=%zu out_bufsz=%zu",
        cmd, ucmd, arg, in_buf, in_bufsz, out_bufsz);

    // Ignore in_buf (kernel only passes minimal data).
    // Read full structs from the caller process memory instead.

    if (!arg || pid <= 0) {
        DBG("ERROR: arg null or pid invalid");
        fuse_reply_err(req, EINVAL);
        return;
    }

    mmc_ioc_multi_cmd hdr{};
    if (!ReadFromPid(pid, (uint64_t)(uintptr_t)arg, &hdr, sizeof(hdr))) {
        DBG("ERROR: cannot read multi_cmd header pid=%d addr=%p (%s)", pid, arg, ErrStr());
        fuse_reply_err(req, EIO);
        return;
    }

    DBG("multi_cmd header: num_of_cmds=%llu", (unsigned long long)hdr.num_of_cmds);

    if (hdr.num_of_cmds == 0 || hdr.num_of_cmds > 16) {
        DBG("ERROR: suspicious num_of_cmds=%llu -> EINVAL", (unsigned long long)hdr.num_of_cmds);
        fuse_reply_err(req, EINVAL);
        return;
    }

    const size_t cmdlist_len =
        sizeof(mmc_ioc_multi_cmd) + hdr.num_of_cmds * sizeof(mmc_ioc_cmd);

    std::vector<uint8_t> cmdblob(cmdlist_len);
    if (!ReadFromPid(pid, (uint64_t)(uintptr_t)arg, cmdblob.data(), cmdblob.size())) {
        DBG("ERROR: cannot read full cmdlist len=%zu pid=%d (%s)", cmdlist_len, pid, ErrStr());
        fuse_reply_err(req, EIO);
        return;
    }

    const mmc_ioc_multi_cmd* full =
        reinterpret_cast<const mmc_ioc_multi_cmd*>(cmdblob.data());
    const mmc_ioc_cmd* cmds = full->cmds;

    DBG("cmdlist read OK (len=%zu)", cmdblob.size());

    for (unsigned long long i = 0; i < full->num_of_cmds; ++i) {
        DumpMmcCmd("cmd", cmds[i]);
    }

    // Expected RPMB chain:
    // CMD23 (set block count)
    // CMD25 (write request frames)
    // CMD18 (read response frames)
    // CMD12 (stop)

    bool haveRead = false;
    for (unsigned long long i = 0; i < full->num_of_cmds; ++i) {
        const mmc_ioc_cmd& c = cmds[i];
        size_t dlen = CmdDataLen(c);

        DBG("exec cmd[%llu]: opcode=%u dlen=%zu", i, c.opcode, dlen);

        if (c.opcode == 23) {
            DBG("CMD23: ignore");
            continue;
        }

        if (c.opcode == 25) {
            if (dlen == 0 || c.data_ptr == 0) {
                DBG("ERROR: CMD25 missing payload dlen=%zu data_ptr=0x%llx",
                    dlen, (unsigned long long)c.data_ptr);
                fuse_reply_err(req, EIO);
                return;
            }

            std::vector<uint8_t> payload(dlen);
            if (!ReadFromPid(pid, c.data_ptr, payload.data(), payload.size())) {
                DBG("ERROR: cannot read CMD25 payload pid=%d ptr=0x%llx len=%zu (%s)",
                    pid, (unsigned long long)c.data_ptr, dlen, ErrStr());
                fuse_reply_err(req, EIO);
                return;
            }

            auto be16 = [](const uint8_t* p) {
                return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
            };

            uint16_t reqresp = be16(payload.data() + OFF_REQRESP);
            uint16_t addr    = be16(payload.data() + OFF_ADDR);
            uint16_t cnt     = be16(payload.data() + OFF_BLOCK_COUNT);

            DBG("CMD25 decoded: reqresp=0x%04x addr=%u cnt=%u", reqresp, addr, cnt);
            HexDump("CMD25 request frames", payload.data(), payload.size(), 256);

            impl->core_.HandleWriteRequestFrames(payload.data(), payload.size());
            DBG("core write done");
            continue;
        }

        if (c.opcode == 18) {
            if (dlen == 0 || c.data_ptr == 0) {
                DBG("ERROR: CMD18 missing buffer dlen=%zu data_ptr=0x%llx",
                    dlen, (unsigned long long)c.data_ptr);
                fuse_reply_err(req, EIO);
                return;
            }

            // blkCnt = CMD18 blocks (fallback to dlen/512)
            uint16_t blkCnt = (uint16_t)c.blocks;
            if (blkCnt == 0) blkCnt = (uint16_t)(dlen / 512);
            if (blkCnt == 0) blkCnt = 1;

            // Finalize pending read before fetching responses
            if (impl->core_.HasPendingRead())
                impl->core_.FinalizePendingRead(blkCnt);

            std::vector<uint8_t> resp(dlen, 0);
            impl->core_.ReadResponseFrames(resp.data(), resp.size());

            DBG("core read -> %zu bytes", resp.size());
            HexDump("CMD18 response frames", resp.data(), resp.size(), 256);

            if (!WriteToPid(pid, c.data_ptr, resp.data(), resp.size())) {
                DBG("ERROR: cannot write resp pid=%d ptr=0x%llx len=%zu (%s)",
                    pid, (unsigned long long)c.data_ptr, resp.size(), ErrStr());
                fuse_reply_err(req, EIO);
                return;
            }

            DBG("CMD18 response written");
            haveRead = true;
            continue;
        }

        if (c.opcode == 12) {
            DBG("CMD12: ignore");
            continue;
        }

        DBG("ERROR: unsupported opcode=%u -> EIO", c.opcode);
        fuse_reply_err(req, EIO);
        return;
    }

    DBG("MULTI_CMD done haveRead=%d -> OK", haveRead ? 1 : 0);
    fuse_reply_ioctl(req, 0, nullptr, 0);
}

// ------------------------------------------------------------
// RpmbCuseDevice (public)
// ------------------------------------------------------------
RpmbCuseDevice::RpmbCuseDevice(Rpmbd& core, const Options& opt)
    : impl_(new Impl(core, opt))
{
    gRpmbDebug = opt.debug;
}

RpmbCuseDevice::~RpmbCuseDevice() {
    delete impl_;
}

int RpmbCuseDevice::Run() {
    char devarg[256];
    std::snprintf(devarg, sizeof(devarg), "DEVNAME=%s", impl_->opt_.devName.c_str());

    const char* devinfo_argv[] = { devarg, nullptr };

    struct cuse_info ci;
    std::memset(&ci, 0, sizeof(ci));
    ci.dev_info_argc = 1;
    ci.dev_info_argv = devinfo_argv;

    const bool fg = impl_->opt_.foreground;

    char arg0[] = "rpmbd";
    char arg_f[] = "-f";

    char* argv_fg[] = { arg0, arg_f, nullptr };
    char* argv_bg[] = { arg0, nullptr };

    int argc = fg ? 2 : 1;
    char** argv = fg ? argv_fg : argv_bg;

    DBG("creating /dev/%s (foreground=%d)", impl_->opt_.devName.c_str(), fg ? 1 : 0);

    return cuse_lowlevel_main(argc, argv, &ci, &Impl::ops, impl_);
}
