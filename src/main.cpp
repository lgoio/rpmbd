#include "Rpmbd.h"
#include "RpmbCuseDevice.h"

#include <iostream>
#include <string>
#include <filesystem>
#include <ctime>
#include <unistd.h>   // getpid()

static void usage(const char* prog)
{
    std::cerr
        << "Usage: " << prog << " --state-file <ABSOLUTE_PATH> [options]\n"
        << "\nRequired:\n"
        << "  -s, --state-file <path>   Absolute path to rpmb_state.bin\n"
        << "\nOptions:\n"
        << "  -d, --dev <name>          Device name under /dev (default: mmcblk2rpmb)\n"
        << "      --debug               Enable debug output\n"
        << "      --quiet               Disable debug output\n"
        << "  -h, --help                Show this help\n"
        << "\nExample:\n"
        << "  " << prog << " -s /var/lib/rpmb/rpmb_state.bin --dev mmcblk2rpmb --debug\n";
}

static bool isAbsolutePath(const std::string& p)
{
    return !p.empty() && p[0] == '/';
}

int main(int argc, char** argv)
{
    std::string stateFile;
    std::string devName = "mmcblk2rpmb";
    bool debug = false;

    // --- parse CLI arguments ---
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];

        if ((a == "--state-file" || a == "-s") && i + 1 < argc)
        {
            stateFile = argv[++i];
        }
        else if ((a == "--dev" || a == "-d") && i + 1 < argc)
        {
            devName = argv[++i];
        }
        else if (a == "--debug")
        {
            debug = true;
        }
        else if (a == "--quiet")
        {
            debug = false;
        }
        else if (a == "--help" || a == "-h")
        {
            usage(argv[0]);
            return 0;
        }
        else
        {
            std::cerr << "ERROR: Unknown argument: " << a << "\n";
            usage(argv[0]);
            return 2;
        }
    }

    // --- validate state file path ---
    if (stateFile.empty())
    {
        std::cerr << "ERROR: Missing required argument --state-file <ABSOLUTE_PATH>\n";
        usage(argv[0]);
        return 2;
    }

    if (!isAbsolutePath(stateFile))
    {
        std::cerr << "ERROR: --state-file must be an absolute path, got: " << stateFile << "\n";
        return 2;
    }

    // --- ensure parent directory exists ---
    try
    {
        std::filesystem::path p(stateFile);
        auto parent = p.parent_path();
        if (parent.empty() || !std::filesystem::exists(parent))
        {
            std::cerr << "ERROR: Directory does not exist: " << parent.string() << "\n";
            return 2;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR: Invalid path: " << e.what() << "\n";
        return 2;
    }

    // --- configure core ---
    Rpmbd::Options ro;
    ro.debug = debug;
    ro.stateFile = stateFile;

    Rpmbd core(ro);

    // --- configure CUSE device ---
    RpmbCuseDevice::Options co;
    co.devName = devName;
    co.foreground = true;
    co.debug = debug;

    // --- status banner ---
    auto now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);

    std::cout
        << "[rpmbd] started at "
        << (tm.tm_year + 1900) << "-"
        << (tm.tm_mon + 1) << "-"
        << tm.tm_mday << " "
        << tm.tm_hour << ":"
        << tm.tm_min << ":"
        << tm.tm_sec
        << " (pid=" << getpid() << ")\n"
        << "[rpmbd] state-file: " << stateFile << "\n"
        << "[rpmbd] device:     /dev/" << devName << "\n"
        << "[rpmbd] debug:      " << (debug ? "on" : "off") << "\n";
    std::cout.flush();

    RpmbCuseDevice dev(core, co);
    return dev.Run();
}
