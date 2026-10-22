// OSC route: SteamVR's Steam Link driver (vrlink) can send eye data as OSC over UDP to
// 127.0.0.1 (settings "Enable OSC" and "Share eye tracking data to other apps on this PC",
// out port 9000 or 9015). The addresses are VRChat's eye-tracking ones; the two we use:
//   /tracking/eye/CenterVecFull   ,fff  gaze direction (VRChat: head space, +Z forward)
//   /tracking/eye/EyesClosedAmount ,f   0 open .. 1 closed
// Any process can listen, whichever app has focus, so this is the route for a mod beside Rez.
#include "probes.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>
#include <map>

namespace {

float BigEndianFloat(const unsigned char* p) {
    uint32_t u = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

size_t Pad4(size_t n) { return (n + 3) & ~size_t(3); }

struct OscMessage {
    std::string address;
    std::string types;
    std::vector<float> floats;
};

// Parses one OSC message (bundles are unwrapped by the caller). Only float arguments are kept;
// other argument types are skipped by size.
bool ParseMessage(const unsigned char* p, size_t n, OscMessage& m) {
    size_t a = strnlen(reinterpret_cast<const char*>(p), n);
    if (a == n) return false;
    m.address.assign(reinterpret_cast<const char*>(p), a);
    size_t off = Pad4(a + 1);
    if (off >= n || p[off] != ',') return true;
    size_t t = strnlen(reinterpret_cast<const char*>(p + off), n - off);
    m.types.assign(reinterpret_cast<const char*>(p + off + 1), t ? t - 1 : 0);
    off = Pad4(off + t + 1);
    for (char c : m.types) {
        if (c == 'f' || c == 'i') {
            if (off + 4 > n) return false;
            if (c == 'f') m.floats.push_back(BigEndianFloat(p + off));
            off += 4;
        } else if (c == 'd' || c == 'h' || c == 't') {
            off += 8;
        } else if (c == 's') {
            off = Pad4(off + strnlen(reinterpret_cast<const char*>(p + off), n - off) + 1);
        }
    }
    return true;
}

void ParsePacket(const unsigned char* p, size_t n, std::vector<OscMessage>& out) {
    if (n >= 16 && std::memcmp(p, "#bundle", 8) == 0) {
        size_t off = 16;  // "#bundle\0" + 8-byte time tag
        while (off + 4 <= n) {
            uint32_t len = (uint32_t(p[off]) << 24) | (uint32_t(p[off + 1]) << 16) |
                           (uint32_t(p[off + 2]) << 8) | p[off + 3];
            off += 4;
            if (off + len > n) break;
            ParsePacket(p + off, len, out);
            off += len;
        }
        return;
    }
    OscMessage m;
    if (ParseMessage(p, n, m)) out.push_back(std::move(m));
}

}  // namespace

int RunOscProbe(int port, double seconds, Recorder& rec) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        rec.Note("can't bind UDP 127.0.0.1:" + std::to_string(port) + " (error " +
                 std::to_string(WSAGetLastError()) + "; is VRChat or another OSC app using it?)");
        closesocket(s);
        WSACleanup();
        return 1;
    }
    DWORD timeout_ms = 100;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof timeout_ms);
    rec.Note("listening for OSC on 127.0.0.1:" + std::to_string(port));

    std::map<std::string, std::string> seen;  // every address once, with its type tag
    float closed = -1;
    unsigned char buf[4096];
    double last_any = -1;
    while (!g_stop && (seconds <= 0 || rec.NowMs() < seconds * 1000)) {
        int n = recv(s, reinterpret_cast<char*>(buf), sizeof buf, 0);
        if (n <= 0) {
            if (last_any < 0 && rec.NowMs() > 5000 && static_cast<long long>(rec.NowMs()) % 5000 < 100) {
                rec.Note("no OSC packets yet");
            }
            continue;
        }
        last_any = rec.NowMs();
        std::vector<OscMessage> msgs;
        ParsePacket(buf, static_cast<size_t>(n), msgs);
        for (auto& m : msgs) {
            if (seen.emplace(m.address, m.types).second) {
                std::string vals;
                for (float f : m.floats) vals += " " + std::to_string(f);
                rec.Note("new address " + m.address + " ," + m.types + vals);
            }
            if (m.address == "/tracking/eye/EyesClosedAmount" && !m.floats.empty()) {
                closed = m.floats[0];
            } else if (m.address == "/tracking/eye/CenterVecFull" && m.floats.size() >= 3) {
                // VRChat's convention is +Z forward; the recorder's is -Z forward.
                Sample smp;
                smp.result = closed < 0 ? "osc" : (closed > 0.5f ? "osc:closed" : "osc:open");
                smp.active = true;
                smp.valid = true;
                smp.tracked = closed < 0.5f;
                smp.dir_head = Normalize({m.floats[0], m.floats[1], -m.floats[2]});
                rec.Add(smp);
            }
        }
    }
    closesocket(s);
    WSACleanup();
    return 0;
}
