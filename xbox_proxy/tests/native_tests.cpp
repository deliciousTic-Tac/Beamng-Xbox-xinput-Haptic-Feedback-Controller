// Compiles the production implementation with a fake vibration sink and XInput.
// UDP tests use an ephemeral loopback port, never BeamNG's port 26780.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
static bool failNonblocking = false;
static int test_ioctl(SOCKET s, long command, u_long* value) {
  if (failNonblocking) { WSASetLastError(WSAEINVAL); return SOCKET_ERROR; }
  return ioctlsocket(s, command, value);
}
#define ioctlsocket test_ioctl
#define HAPTICS_TEST
#include "../xinput_proxy.cpp"
#undef ioctlsocket
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

static void check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
static int forwarded = 0;
static DWORD forwardedIndex = 0;
static std::vector<GamepadVibration> outputs;
static bool sinkAvailable = true;
static bool sink(const GamepadVibration& v) { outputs.push_back(v); return sinkAvailable; }
static DWORD WINAPI real_set(DWORD id, XINPUT_VIBRATION*) {
  ++forwarded; forwardedIndex = id;
  return id >= XUSER_MAX_COUNT ? ERROR_BAD_ARGUMENTS : 1234;
}
static void WINAPI real_enable(BOOL) {}
static bool zero(const GamepadVibration& v) {
  return v.LeftMotor == 0 && v.RightMotor == 0 && v.LeftTrigger == 0 && v.RightTrigger == 0;
}
static void reset_output_state() {
  g_bodyLeft = g_bodyRight = g_triggerLeft = g_triggerRight = 0;
  g_shiftBodyLeft = g_shiftBodyRight = 0;
  g_directionalShiftBody = false;
  g_hapticsEnabled = true;
  g_wgiAvailable = false;
  g_hasApplied = false;
  g_selectedPad = nullptr;
  g_pairCandidate = nullptr;
}
static Packet packet() {
  Packet p{}; memcpy(p.magic, "BCH1", 4); p.vehicle = 1; p.flags = 5;
  p.rtStart = .95f; p.limit = 7000; p.shiftIntensity = 1; p.shiftMode = 1;
  p.rtIntensity = 1; p.absIntensity = 1; p.brakeLockIntensity = 1;
  p.gearIndex = 3;
  return p;
}
static void compute(const Packet& p, ULONGLONG now) {
  auto& r = g_runtime;
  calculate(p, now, r.limiterUntil, r.absUntil, r.shiftUntil, r.startupUntil, r.lastVehicle,
            r.lastGear, r.shiftDirection, r.gearInitialized, r.wasShifting, r.startupArmed,
            r.vehicleInitialized, r.engineInitialized, r.wasEngineRunning, r.shiftPendingUntil,
            r.shiftPendingStarted, r.shiftStartRPM, r.shiftPending);
}
static void test_routing_and_disable() {
  g_setState = real_set; g_enable = real_enable; g_testApply = sink;
  g_socketRetryAt = (std::numeric_limits<ULONGLONG>::max)();
  g_wgiAvailable = true; g_lastPump = GetTickCount64();
  XINPUT_VIBRATION request{32000, 16000};
  check(XInputSetState(0, &request) == ERROR_SUCCESS && forwarded == 0, "index 0 intercepted");
  for (DWORD id=1; id<4; ++id) {
    check(XInputSetState(id, &request) == 1234 && forwardedIndex == id, "secondary controller forwarded");
  }
  check(XInputSetState(4, &request) == ERROR_BAD_ARGUMENTS, "invalid index delegated");
  check(XInputSetState(0, nullptr) == 1234, "null argument delegated");
  sinkAvailable = false; g_wgiAvailable = false; g_lastPump = GetTickCount64();
  check(XInputSetState(0, &request) == 1234, "WGI unavailable uses system");
  sinkAvailable = true;

  g_triggerLeft = 65000; g_shiftBodyRight = 65000;
  apply_vibration(GetTickCount64(), true);
  const auto before = outputs.size();
  XInputEnable(FALSE);
  check(outputs.size() == before+1 && zero(outputs.back()), "disable bypasses throttle");
  check(g_socket == INVALID_SOCKET && !g_winsockStarted, "disable releases socket ownership");
  request.wLeftMotorSpeed = 42000;
  XInputSetState(0, &request);
  apply_vibration(GetTickCount64()+1000, true);
  check(zero(outputs.back()), "set while disabled remains silent");
  XInputEnable(TRUE);
  check(outputs.back().LeftMotor == 42000 / 65535.0, "enable restores latest ordinary request");
  check(outputs.back().LeftTrigger == 0, "enable never replays stale telemetry");
  const auto stable = outputs.size();
  const auto last = g_lastGamepadProbe.load();
  apply_vibration(last+16);
  check(outputs.size() == stable, "unchanged output skips COM refresh");
  apply_vibration(last+1000);
  check(outputs.size() == stable+1, "unchanged output still probes reconnection");
  XInputEnable(FALSE);
  std::thread first([] { for (int i=0;i<1000;++i) pump_haptics(); });
  std::thread second([] { for (int i=0;i<100;++i) XInputEnable(FALSE); });
  first.join(); second.join();
  check(zero(outputs.back()), "concurrent pumps cannot undo disable");
}
static void test_idle_startup_defers_wgi() {
  reset_output_state();
  outputs.clear();
  sinkAvailable = true;
  g_testApply = sink;
  apply_vibration(GetTickCount64(), true);
  check(outputs.empty(), "idle startup never enters WGI discovery");

  g_triggerRight = 1;
  apply_vibration(GetTickCount64(), true);
  check(outputs.size() == 1 && outputs.back().RightTrigger > 0,
        "first non-zero effect starts WGI output immediately");
  reset_output_state();
}
static void test_packet_state() {
  clear_telemetry_state(g_runtime);
  auto p = packet();
  check(sizeof(p) == 96 && valid_packet(p), "BCH1 ABI/defaults");
  compute(p, 1000);
  p.gearIndex = 4; compute(p, 1016);
  check(g_triggerRight.load() > 0, "upshift pulse starts");
  p.rpm = (std::numeric_limits<float>::quiet_NaN)();
  check(!valid_packet(p), "NaN rejected");
  compute(p, 1032);
  check(g_triggerRight == 0 && g_shiftBodyRight == 0 && !g_runtime.shiftPending, "invalid packet cancels pending effects");
  p = packet(); compute(p, 1050);
  p.gearIndex = 4; compute(p, 1066);
  p.vehicle = 2; p.gearIndex = 1; compute(p, 1082);
  check(g_triggerRight == 0 && g_triggerLeft == 0 && g_runtime.shiftUntil == 0, "vehicle switch clears gear history");
  p.flags = 0; compute(p, 1098);
  check(g_triggerLeft == 0 && g_triggerRight == 0, "pause packet stops telemetry");
  p = packet(); p.startupInterval = 2;
  check(!valid_packet(p), "out-of-range field rejected");
}
static void test_controller_pairing() {
  XINPUT_GAMEPAD x{};
  PadInput pads[3]{};
  for (auto& pad : pads) pad.valid = true;
  x.wButtons = XINPUT_GAMEPAD_A;
  pads[0].buttons = 8; // B: WGI order deliberately differs from XInput order.
  pads[1].buttons = 4; // A: only this pad matches XInput 0.
  check(unique_matching_pad(x, pads, 3) == 1, "active input selects the matching WGI pad, not GetAt(0)");
  pads[2].buttons = 4;
  check(unique_matching_pad(x, pads, 3) == -1, "identical button states are ambiguous");
  pads[2].buttons = 0;
  x.wButtons = 0;
  check(unique_matching_pad(x, pads, 3) == -1, "idle controllers cannot be paired by order");
  pads[0].buttons = 0;
  pads[1].buttons = 0;
  x.bLeftTrigger = 180;
  pads[1].leftTrigger = 180.0 / 255.0;
  check(unique_matching_pad(x, pads, 3) == 1, "distinctive analog input can pair a controller");
  pads[0].leftTrigger = 180.0 / 255.0;
  check(unique_matching_pad(x, pads, 3) == -1, "identical analog input is ambiguous");
  x.bLeftTrigger = 0;
  x.sThumbLX = 25000;
  pads[0].leftTrigger = 0;
  pads[1].leftTrigger = 0;
  pads[1].leftX = normalized_axis(x.sThumbLX);
  check(unique_matching_pad(x, pads, 3) == 1, "a distinctive stick position can pair a controller");
  pads[1].valid = false;
  check(unique_matching_pad(x, pads, 3) == -1, "unreadable devices cannot be paired");
}
static void test_socket_recovery() {
  close_socket(); g_socketRetryAt = 0;
  WSADATA data{}; check(WSAStartup(MAKEWORD(2,2), &data) == 0, "test Winsock startup");
  SOCKET blocker = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  check(blocker != INVALID_SOCKET, "test socket created");
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  check(bind(blocker, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "bind ephemeral port");
  int length = sizeof(addr);
  check(getsockname(blocker, reinterpret_cast<sockaddr*>(&addr), &length) == 0, "get ephemeral port");
  const USHORT port = ntohs(addr.sin_port);
  ensure_socket(10000, port);
  check(g_socket == INVALID_SOCKET && !g_winsockStarted && g_socketRetryAt == 15000, "bind failure cleans up and schedules retry");
  closesocket(blocker);
  ensure_socket(14999, port); check(g_socket == INVALID_SOCKET, "retry is throttled");
  ensure_socket(15000, port); check(g_socket != INVALID_SOCKET, "port recovers without restart");
  SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  check(sender != INVALID_SOCKET, "test sender created");
  auto p = packet();
  check(sendto(sender, reinterpret_cast<const char*>(&p), sizeof(p), 0,
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == sizeof(p), "send packet");
  pump_telemetry(15100);
  check(g_runtime.received == 15100, "valid datagram received");
  pump_telemetry(15351);
  check(g_runtime.received == 0 && g_triggerRight == 0, "stale telemetry cleared");
  char oversized[200]{};
  sendto(sender, oversized, sizeof(oversized), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  pump_telemetry(15400);
  check(g_socket != INVALID_SOCKET, "oversized datagram does not kill listener");
  closesocket(sender); close_socket();
  g_socketRetryAt = 0; failNonblocking = true;
  ensure_socket(20000, port);
  check(g_socket == INVALID_SOCKET && !g_winsockStarted && g_socketRetryAt == 25000, "FIONBIO failure never publishes a blocking socket");
  failNonblocking = false;
  WSACleanup();
}
int main() {
  try {
    test_routing_and_disable();
    test_idle_startup_defers_wgi();
    test_packet_state();
    test_controller_pairing();
    test_socket_recovery();
    puts("PASS: native regression suite (mock WGI/XInput, real ephemeral UDP)");
    return 0;
  } catch (const std::exception& e) {
    fprintf(stderr, "FAIL: %s\n", e.what());
    close_socket();
    return 1;
  }
}
