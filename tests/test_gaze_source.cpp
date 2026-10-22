#include "test_main.h"

#include "../proxy/gaze_source.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace rezgaze;

TEST(choose_auto_prefers_fresh_tracked_helper) { EXPECT(ChooseSource(SourceMode::Auto, 10, true, 10) == Source::Helper); }
TEST(choose_auto_stale_helper_falls_to_osc) { EXPECT(ChooseSource(SourceMode::Auto, 300, true, 10) == Source::Osc); }
TEST(choose_auto_untracked_helper_falls_to_osc) { EXPECT(ChooseSource(SourceMode::Auto, 10, false, 10) == Source::Osc); }
TEST(choose_auto_nothing_is_head) { EXPECT(ChooseSource(SourceMode::Auto, -1, false, -1) == Source::Head); }
TEST(choose_auto_both_stale_is_head) { EXPECT(ChooseSource(SourceMode::Auto, 300, true, 300) == Source::Head); }
TEST(choose_forced_helper_never_osc) { EXPECT(ChooseSource(SourceMode::Helper, 300, true, 10) == Source::Head); }
TEST(choose_forced_osc_ignores_helper) { EXPECT(ChooseSource(SourceMode::Osc, 10, true, 10) == Source::Osc); }
TEST(choose_forced_osc_stale_is_head) { EXPECT(ChooseSource(SourceMode::Osc, 10, true, 260) == Source::Head); }
TEST(choose_boundary_250_is_fresh) { EXPECT(ChooseSource(SourceMode::Auto, 250, true, -1) == Source::Helper); }

namespace {

void PutStr(std::string& out, const char* s) {
    out.append(s);
    out.push_back('\0');
    while (out.size() % 4) out.push_back('\0');
}

void PutBe(std::string& out, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    char b[4] = {char(u >> 24), char(u >> 16), char(u >> 8), char(u)};
    out.append(b, 4);
}

// vrlink's message: VRChat convention, +Z forward.
std::string GazeMessage(const char* addr, float x, float y, float z) {
    std::string p;
    PutStr(p, addr);
    PutStr(p, ",fff");
    PutBe(p, x);
    PutBe(p, y);
    PutBe(p, z);
    return p;
}

bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }

}  // namespace

TEST(osc_message_sets_gaze_minus_z_forward) {
    OscSource osc;
    V3 d;
    double age;
    EXPECT(!osc.Read(d, age));
    std::string p = GazeMessage("/tracking/eye/CenterVecFull", 0.1f, 0.2f, 0.97f);
    osc.HandlePacket(reinterpret_cast<const unsigned char*>(p.data()), p.size());
    EXPECT(osc.Read(d, age));
    V3 want = Norm({0.1f, 0.2f, -0.97f});
    EXPECT(Near(d.x, want.x) && Near(d.y, want.y) && Near(d.z, want.z));
    EXPECT(age >= 0 && age < 1000);
    EXPECT(osc.TakePacketCount() == 1);
}

TEST(osc_bundle_is_unwrapped) {
    OscSource osc;
    std::string inner = GazeMessage("/tracking/eye/CenterVecFull", 0, 0, 1);
    std::string b("#bundle\0", 8);
    b.append(8, '\0');  // time tag
    uint32_t len = static_cast<uint32_t>(inner.size());
    char l[4] = {char(len >> 24), char(len >> 16), char(len >> 8), char(len)};
    b.append(l, 4);
    b += inner;
    osc.HandlePacket(reinterpret_cast<const unsigned char*>(b.data()), b.size());
    V3 d;
    double age;
    EXPECT(osc.Read(d, age));
    EXPECT(Near(d.z, -1));
}

TEST(osc_other_addresses_ignored) {
    OscSource osc;
    std::string p = GazeMessage("/sl/eyeTrackedGazePoint", 0, 0, -1);
    osc.HandlePacket(reinterpret_cast<const unsigned char*>(p.data()), p.size());
    V3 d;
    double age;
    EXPECT(!osc.Read(d, age));
}
