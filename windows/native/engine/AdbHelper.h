#pragma once

#include <string>
#include <vector>

namespace phonecam {

struct AdbResult {
    bool ok = false;
    std::string message;
    std::vector<std::string> devices;
};

class AdbHelper {
public:
    static std::wstring FindAdbExecutable();
    static AdbResult ForwardStreamPort(uint16_t localPort, uint16_t devicePort);
};

} // namespace phonecam
