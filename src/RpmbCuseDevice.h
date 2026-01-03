#pragma once

#include <string>

class Rpmbd; // forward decl

class RpmbCuseDevice {
public:
    struct Options {
        std::string devName = "mmcblk2rpmb"; // creates /dev/<devName>
        bool foreground = true;              // pass -f to FUSE
        bool debug = false;                  // enable debug logs
    };

    RpmbCuseDevice(Rpmbd& core, const Options& opt);
    ~RpmbCuseDevice();

    // Blocks: runs the CUSE/FUSE main loop
    int Run();

private:
    class Impl;
    Impl* impl_;
};
