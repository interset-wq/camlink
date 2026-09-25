#include "ui.h"

#include "imgui.h"
#include "ui_logic.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace ui {

namespace {

int64_t steadyMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
}

const ImVec4 kAccent(0.23f, 0.51f, 0.96f, 1.0f);
const ImVec4 kRecordRed(0.86f, 0.24f, 0.24f, 1.0f);

std::string formatHms(int totalSeconds) {
    int h = totalSeconds / 3600;
    int m = (totalSeconds % 3600) / 60;
    int s = totalSeconds % 60;
    char buf[16];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    }
    return buf;
}

std::string shortName(const std::string& path) {
    std::error_code ec;
    std::filesystem::path p(path);
    auto name = p.filename().string();
    return name.empty() ? path : name;
}

void drawVideoArea(const ui::FrameState& s) {
    const ImGuiIO& io = ImGui::GetIO();
    const float menuH = ImGui::GetFrameHeight();
    const float statusH = ImGui::GetFrameHeight();

    ImGui::SetNextWindowPos(ImVec2(0.0f, menuH));
    ImGui::SetNextWindowSize(
        ImVec2(io.DisplaySize.x, io.DisplaySize.y - menuH - statusH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##video", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMouseInputs);
    ImGui::PopStyleVar();

    if (s.hasFrame && s.texture && s.videoW > 0 && s.videoH > 0) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float aspect =
            static_cast<float>(s.videoW) / static_cast<float>(s.videoH);
        float w = avail.x;
        float h = w / aspect;
        if (h > avail.y) {
            h = avail.y;
            w = h * aspect;
        }
        ImGui::SetCursorPos(ImVec2((avail.x - w) * 0.5f, (avail.y - h) * 0.5f));
        // glTexImage2D stores the first (top) row at v=0; ImGui maps uv0 to
        // the quad's top-left, so identity UVs display the frame upright.
        ImGui::Image(s.texture, ImVec2(w, h), ImVec2(0.0f, 0.0f),
                     ImVec2(1.0f, 1.0f));
    } else {
        const char* line1 = "Waiting for phone...";
        const char* line2 = "Connect the Android device and open the CamLink app";
        const float centerY = ImGui::GetContentRegionAvail().y * 0.5f - 24.0f;
        ImVec2 t1 = ImGui::CalcTextSize(line1);
        ImVec2 t2 = ImGui::CalcTextSize(line2);
        const float availX = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPos(ImVec2((availX - t1.x) * 0.5f, centerY));
        ImGui::TextDisabled("%s", line1);
        ImGui::SetCursorPos(ImVec2((availX - t2.x) * 0.5f, centerY + 28.0f));
        ImGui::TextDisabled("%s", line2);
    }
    ImGui::End();
}

void drawMenuBar(const ui::FrameState& s) {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Snapshot", "S")) {
            if (s.onSnapshot) s.onSnapshot();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(s.recording ? "Stop Recording" : "Start Recording",
                            "R")) {
            if (s.onRecordToggle) s.onRecordToggle();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            if (s.onQuit) s.onQuit();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fullscreen", "F", s.fullscreen)) {
            if (s.onFullscreen) s.onFullscreen();
        }
        if (ImGui::MenuItem("Burn-in OSD", "O", s.osdEnabled)) {
            if (s.onOsdToggle) s.onOsdToggle();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("CamLink " PROJECT_VERSION_STRING, nullptr, nullptr,
                        false);
        ImGui::Separator();
        ImGui::TextDisabled("Android camera -> PC over USB (adb)");
        ImGui::EndMenu();
    }

    // Right-aligned live stats in the menu bar.
    char right[160];
    {
        std::string res = s.videoW > 0
                              ? std::to_string(s.videoW) + "x" +
                                    std::to_string(s.videoH)
                              : std::string("-");
        if (s.recording) {
            std::snprintf(right, sizeof(right), "REC %s  |  %.0f fps  |  %s",
                          formatHms(s.recSeconds).c_str(), s.fps, res.c_str());
        } else {
            std::snprintf(right, sizeof(right), "%.0f fps  |  %s", s.fps,
                          res.c_str());
        }
    }
    const ImVec2 rt = ImGui::CalcTextSize(right);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - rt.x - 14.0f);
    if (s.recording) {
        ImGui::PushStyleColor(ImGuiCol_Text, kRecordRed);
        ImGui::TextUnformatted(right);
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("%s", right);
    }

    ImGui::EndMainMenuBar();
}

void drawStatusBar(const ui::FrameState& s) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y), ImGuiCond_Always,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, ImGui::GetFrameHeight()));
    ImGui::Begin("##status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoInputs);

    char clock[16];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(clock, sizeof(clock), "%H:%M:%S", &tmv);

    ImGui::TextUnformatted(clock);
    if (s.hasFrame) {
        ImGui::SameLine();
        ImGui::TextDisabled("·");
        ImGui::SameLine();
        ImGui::Text("%dx%d", s.videoW, s.videoH);
        ImGui::SameLine();
        ImGui::TextDisabled("·");
        ImGui::SameLine();
        ImGui::Text("%.0f fps", s.fps);
        ImGui::SameLine();
        ImGui::TextDisabled("·");
        ImGui::SameLine();
        ImGui::Text("frames %llu",
                    static_cast<unsigned long long>(s.shownFrames));
    }
    if (s.recording) {
        ImGui::SameLine();
        ImGui::TextDisabled("·");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kRecordRed);
        ImGui::Text("REC %s  %s", formatHms(s.recSeconds).c_str(),
                    shortName(s.recTarget).c_str());
        ImGui::PopStyleColor();
    }

    // Right side: connection health.
    std::string right;
    if (!s.connected) {
        right = "disconnected";
    } else if (s.stalled) {
        right = "stalled";
    } else if (s.decodeErrors > 0) {
        right = "decode errors: " + std::to_string(s.decodeErrors);
    }
    if (!right.empty()) {
        const ImVec2 rt = ImGui::CalcTextSize(right.c_str());
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - rt.x - 14.0f);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(0.95f, 0.62f, 0.29f, 1.0f));
        ImGui::TextUnformatted(right.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

void drawToolbar(const ui::FrameState& s) {
    static double lastActivity = 0.0;
    static bool hoveredLastFrame = false;

    const ImGuiIO& io = ImGui::GetIO();
    const bool active =
        io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseDown[0] ||
        io.MouseDown[1] || io.MouseDown[2] || hoveredLastFrame;
    if (active) lastActivity = ImGui::GetTime();
    const bool visible = (ImGui::GetTime() - lastActivity) < 3.0;

    if (!visible) {
        hoveredLastFrame = false;
        return;
    }

    const float statusH = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - statusH - 10.0f),
        ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);

    if (s.recording) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.70f, 0.17f, 0.17f, 1.0f));
        if (ImGui::Button("Stop")) {
            if (s.onRecordToggle) s.onRecordToggle();
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, kRecordRed);
        if (ImGui::Button("REC")) {
            if (s.onRecordToggle) s.onRecordToggle();
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    if (ImGui::Button("Snapshot")) {
        if (s.onSnapshot) s.onSnapshot();
    }
    ImGui::SameLine();
    if (ImGui::Button(s.fullscreen ? "Windowed" : "Fullscreen")) {
        if (s.onFullscreen) s.onFullscreen();
    }

    hoveredLastFrame = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::End();
}

void drawToast(const ui::FrameState& s) {
    if (s.toastText.empty()) return;
    if (!ui_logic::toastVisible(s.toastShownAtMs, steadyMs(), 3000)) return;

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 14.0f,
                                   ImGui::GetFrameHeight() + 14.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.16f, 0.11f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.64f, 0.35f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::Begin("##toast", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoInputs);
    ImGui::TextUnformatted(s.toastText.c_str());
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

}  // namespace

void initStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding = 4.0f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.ItemSpacing = ImVec2(9.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 4.0f);
    style.CellPadding = ImVec2(7.0f, 4.0f);
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowMenuButtonPosition = ImGuiDir_Right;

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.070f, 0.078f, 0.098f, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0.070f, 0.078f, 0.098f, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.086f, 0.094f, 0.118f, 0.99f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.100f, 0.110f, 0.137f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.160f, 0.180f, 0.220f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.130f, 0.140f, 0.170f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.180f, 0.200f, 0.240f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.160f, 0.175f, 0.210f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.160f, 0.175f, 0.215f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.230f, 0.255f, 0.310f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.200f, 0.430f, 0.880f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.230f, 0.510f, 0.960f, 0.55f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.230f, 0.510f, 0.960f, 0.75f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.230f, 0.510f, 0.960f, 0.95f);
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 0.62f, 1.0f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.160f, 0.180f, 0.220f, 1.0f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.230f, 0.510f, 0.960f, 0.7f);
    c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.230f, 0.510f, 0.960f, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.230f, 0.510f, 0.960f, 0.67f);
    c[ImGuiCol_ResizeGripActive] = kAccent;
    c[ImGuiCol_Tab] = ImVec4(0.130f, 0.140f, 0.170f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.230f, 0.510f, 0.960f, 0.80f);
    c[ImGuiCol_TabActive] = ImVec4(0.180f, 0.260f, 0.400f, 1.0f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.100f, 0.110f, 0.137f, 1.0f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.130f, 0.145f, 0.180f, 1.0f);
    c[ImGuiCol_NavHighlight] = kAccent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.03f, 0.55f);

    // Slightly larger default font for crispness on modern displays.
    ImFontConfig cfg;
    cfg.SizePixels = 16.0f;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    ImGui::GetIO().Fonts->AddFontDefault(&cfg);
}

void draw(const FrameState& state) {
    drawVideoArea(state);
    drawMenuBar(state);
    drawStatusBar(state);
    drawToolbar(state);
    drawToast(state);
}

}  // namespace ui
