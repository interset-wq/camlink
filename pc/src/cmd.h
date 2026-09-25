#pragma once

#include <cstdio>
#include <string>

namespace shell {

// Runs a shell command, capturing combined stdout+stderr.
// Returns the process exit status, or -1 if the command could not be run.
inline int run(const std::string& command, std::string* output = nullptr) {
#ifdef _WIN32
    // cmd.exe (used by _popen as `cmd /c <string>`) strips the first and last
    // quote of the operand when it starts with a quote character, which
    // corrupts commands like `"C:\path\exe" args`. Wrapping the whole command
    // in one extra pair of quotes makes cmd strip only the wrapper.
    std::string wrapped = "\"" + command + "\"";
    FILE* pipe = _popen(wrapped.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) return -1;

    std::string out;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        out += buffer;
    }

#ifdef _WIN32
    int status = _pclose(pipe);
#else
    int status = pclose(pipe);
    if (status != -1 && WIFEXITED(status)) status = WEXITSTATUS(status);
#endif

    if (output) *output = out;
    return status;
}

inline std::string quote(const std::string& s) {
    return "\"" + s + "\"";
}

}  // namespace shell
