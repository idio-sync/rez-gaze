// replay mode: sends gaze to 127.0.0.1:port as Steam Link's vrlink does
// (/tracking/eye/CenterVecFull ,fff, VRChat's +Z-forward convention), so the stand-in DLL's
// gaze path can be tested without the headset. Either replays a probe CSV (rows with valid=1,
// at their recorded times) or sweeps a circle of the given radius.
#include "probes.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace {

void PutBe(std::string& out, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    char b[4] = {char(u >> 24), char(u >> 16), char(u >> 8), char(u)};
    out.append(b, 4);
}

void PutStr(std::string& out, const char* s) {
    out.append(s);
    out.push_back('\0');
    while (out.size() % 4) out.push_back('\0');
}

std::string GazePacket(Vec3 d) {
    std::string p;
    PutStr(p, "/tracking/eye/CenterVecFull");
    PutStr(p, ",fff");
    PutBe(p, static_cast<float>(d.x));
    PutBe(p, static_cast<float>(d.y));
    PutBe(p, static_cast<float>(-d.z));  // ours is -Z forward
    return p;
}

struct Row {
    double t_ms;
    Vec3 dir;
};

std::vector<Row> ReadCsv(const std::string& path) {
    std::vector<Row> rows;
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);  // header: t_ms,result,active,valid,tracked,fresh,yaw,pitch,dx,dy,dz,...
    while (std::getline(f, line)) {
        std::vector<std::string> c;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) c.push_back(cell);
        if (c.size() < 11 || c[3] != "1") continue;
        rows.push_back({std::atof(c[0].c_str()),
                        Normalize({std::atof(c[8].c_str()), std::atof(c[9].c_str()), std::atof(c[10].c_str())})});
    }
    return rows;
}

}  // namespace

int RunOscReplay(const std::string& csv, double circle_deg, int port, double seconds, int hz, Recorder& rec) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
    auto send = [&](Vec3 d) {
        std::string p = GazePacket(d);
        sendto(s, p.data(), static_cast<int>(p.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
        Sample smp;
        smp.result = "sent";
        smp.active = smp.valid = smp.tracked = true;
        smp.dir_head = d;
        rec.Add(smp);
    };

    if (!csv.empty()) {
        std::vector<Row> rows = ReadCsv(csv);
        if (rows.empty()) {
            rec.Note("no valid rows in " + csv);
            return 1;
        }
        rec.Note("replaying " + std::to_string(rows.size()) + " samples from " + csv + " to 127.0.0.1:" +
                 std::to_string(port) + (seconds > 0 ? " (looping)" : ""));
        double t0 = rows.front().t_ms, start = rec.NowMs();
        size_t i = 0;
        while (!g_stop) {
            double now = rec.NowMs();
            if (seconds > 0 && now >= seconds * 1000) break;
            double due = start + (rows[i].t_ms - t0);
            if (now < due) {
                Sleep(1);
                continue;
            }
            send(rows[i].dir);
            if (++i == rows.size()) {
                if (seconds <= 0) break;
                i = 0;
                start = rec.NowMs();
            }
        }
    } else {
        // A circle of radius circle_deg around straight ahead, one turn every 4 s.
        rec.Note("sending a " + std::to_string(circle_deg) + " deg circle at " + std::to_string(hz) +
                 " Hz to 127.0.0.1:" + std::to_string(port));
        double r = circle_deg / kRadToDeg;
        DWORD sleep_ms = static_cast<DWORD>(1000 / (hz > 0 ? hz : 1));
        while (!g_stop && (seconds <= 0 || rec.NowMs() < seconds * 1000)) {
            double a = rec.NowMs() / 4000.0 * 2 * 3.14159265358979;
            double yaw = r * std::cos(a), pitch = r * std::sin(a);
            send(Normalize({std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)}));
            Sleep(sleep_ms);
        }
    }
    closesocket(s);
    WSACleanup();
    return 0;
}
