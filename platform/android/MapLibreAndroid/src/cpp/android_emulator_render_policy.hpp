#pragma once

#include <string_view>

namespace mln::android::detail {

// An opt-in experiment for one verified API35 hardware Guest ANGLE stack. Unknown drivers keep the workaround.
struct GuestAngleDrawableReuseInputs {
    std::string_view optIn;
    bool emulator = false;
    int apiLevel = 0;
    bool hasCurrentContext = false;
    std::string_view hardwareEgl;
    std::string_view bootHardwareEgl;
    std::string_view vendor;
    std::string_view renderer;
    std::string_view version;
};

enum class GuestAngleDrawableReuseDecision { Disabled, NotEmulator, UnsupportedApi, NoContext, WrongEgl, WrongDriver, Enabled };

inline GuestAngleDrawableReuseDecision guestAngleDrawableReuseDecision(const GuestAngleDrawableReuseInputs& in) {
    using Decision = GuestAngleDrawableReuseDecision;
    if (in.optIn != "1") return Decision::Disabled;
    if (!in.emulator) return Decision::NotEmulator;
    if (in.apiLevel != 35) return Decision::UnsupportedApi;
    if (!in.hasCurrentContext) return Decision::NoContext;
    if (in.hardwareEgl != "angle" || in.bootHardwareEgl != "angle") return Decision::WrongEgl;
    // Explicitly pinned to the observed hardware driver; do not infer safety from an emulator or ANGLE name alone.
    if (in.vendor != "Google Inc. (Apple)" ||
        in.renderer.substr(0, 23) != "ANGLE (Apple, Vulkan 1." ||
        in.renderer.find("Apple M3 Pro (") == std::string_view::npos ||
        in.renderer.find("SwiftShader") != std::string_view::npos ||
        in.renderer.find("Translator") != std::string_view::npos ||
        in.renderer.find("llvmpipe") != std::string_view::npos ||
        in.renderer.find("lavapipe") != std::string_view::npos ||
        in.version.substr(0, 12) != "OpenGL ES 3.") return Decision::WrongDriver;
    return Decision::Enabled;
}

inline const char* guestAngleDrawableReuseReason(GuestAngleDrawableReuseDecision value) {
    using Decision = GuestAngleDrawableReuseDecision;
    switch (value) {
        case Decision::Disabled: return "disabled";
        case Decision::NotEmulator: return "not-emulator";
        case Decision::UnsupportedApi: return "unsupported-api";
        case Decision::NoContext: return "no-current-context";
        case Decision::WrongEgl: return "egl-not-angle";
        case Decision::WrongDriver: return "driver-not-verified";
        case Decision::Enabled: return "verified-guest-angle";
    }
    return "unknown";
}

} // namespace mln::android::detail
