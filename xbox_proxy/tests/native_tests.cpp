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
  g_nativeApplied = false;
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
            r.shiftPendingStarted, r.shiftStartRPM, r.shiftPending, r.shiftAwaitingConfirmation);
}
static void test_startup_baseline_and_starter() {
  clear_telemetry_state(g_runtime);
  auto p = packet(); p.engineRunning = 1; p.startupIntensity = 1;
  compute(p, 1000);
  check(g_runtime.startupUntil == 0 && g_shiftBodyLeft == 0 && g_shiftBodyRight == 0,
        "loading a running engine does not invent a startup");
  clear_telemetry_state(g_runtime); // Simulate timeout/pause recovery.
  compute(p, 2000);
  check(g_runtime.startupUntil == 0, "telemetry recovery does not replay startup");
  p.engineRunning = 0; p.starterActive = 1; compute(p, 2100);
  check(g_shiftBodyLeft > 0 || g_shiftBodyRight > 0, "starter vibrates before engine runs");
  p.starterActive = 0; p.engineRunning = 1; compute(p, 2200);
  check(g_runtime.startupUntil == 3400, "real off-to-running transition starts sequence");
  p.vehicle = 2; compute(p, 2300);
  check(g_runtime.startupUntil == 0, "vehicle change resets engine baseline");
}
static void test_shift_confirmation_does_not_replay() {
  clear_telemetry_state(g_runtime);
  auto p = packet(); p.rpm = 4000; compute(p, 1000);
  p.isShifting = 1; compute(p, 1016);
  p.rpm = 4600; compute(p, 1100);
  check(g_triggerLeft > 0 && g_runtime.shiftAwaitingConfirmation, "RPM rise infers downshift");
  const auto deadline = g_runtime.shiftUntil;
  p.gearIndex = 2; compute(p, 1300);
  check(g_runtime.shiftUntil == deadline && g_triggerLeft == 0,
        "delayed gear confirmation never replays expired pulse");
  p.isShifting = 0; compute(p, 1316);
  p.isShifting = 1; p.gearIndex = 3; compute(p, 1332);
  check(g_triggerRight > 0 && g_runtime.shiftUntil == 1512, "next actual shift still fires");

  clear_telemetry_state(g_runtime);
  p = packet(); compute(p, 2000);
  p.isShifting = 1; compute(p, 2016);
  compute(p, 2600); // No RPM or gear information: timeout fallback.
  const auto fallbackDeadline = g_runtime.shiftUntil;
  p.gearIndex = 4; compute(p, 2730);
  check(g_runtime.shiftUntil == fallbackDeadline && g_triggerRight == 0,
        "fallback pulse also consumes delayed confirmation");
  clear_telemetry_state(g_runtime);
  check(!g_runtime.shiftAwaitingConfirmation, "stop clears deduplication history");
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
  x = XINPUT_GAMEPAD{};
  pads[0] = PadInput{}; pads[0].valid = true;
  check(unique_matching_pad(x, pads, 1, true) == 0, "sole controller pairs while idle");
  check(unique_matching_pad(x, pads, 1, false) == -1, "multiple XInput pads require active input");
  check(unique_matching_pad(x, pads, 2, true) == -1, "multiple WGI pads require active input");
  pads[0].valid = false;
  check(unique_matching_pad(x, pads, 1, true) == -1, "sole unreadable pad is rejected");
  pads[0].valid = true; pads[0].buttons = 4;
  check(unique_matching_pad(x, pads, 1, true) == -1, "sole mismatched reading is rejected");
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
  const char query[] = "BCQ1 7 1 1 0.5 0.8";
  check(sendto(sender, query, sizeof(query)-1, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == sizeof(query)-1,
        "send diagnostic query over actual loopback");
  pump_telemetry(15360);
  check(g_diagnosticPending && g_mixMode == 1 && g_nativeGain == .5f, "query controls mixer");
  reply_diagnostics(15360);
  u_long nonblocking = 1; ioctlsocket(sender, FIONBIO, &nonblocking);
  char reply[1400]{};
  const int replySize = recv(sender, reply, sizeof(reply)-1, 0);
  check(replySize > 0 && !memcmp(reply,"BCD1 ",5) && strstr(reply,"\"seq\":7") &&
        strstr(reply,"\"telemetry\":0"), "diagnostic round trip includes sequence and stale telemetry");
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
static void test_mixer_and_diagnostic_validation() {
  reset_output_state();
  g_bodyLeft=30000; g_bodyRight=30000; g_shiftBodyRight=20000;
  g_triggerRight=10000; g_directionalShiftBody=true; g_directionalShiftDirection=1;
  g_nativeGain=g_customGain=1; g_mixMode=0;
  auto v=compose_vibration();
  check(v.LeftMotor==30000/65535.0 && v.RightMotor==30000/65535.0, "maximum preserves native road signal on both motors");
  g_mixMode=1; v=compose_vibration();
  check(v.RightMotor==50000/65535.0, "addition mixes signals");
  g_mixMode=2; v=compose_vibration();
  check(v.RightMotor==20000/65535.0 && v.LeftMotor==30000/65535.0, "priority affects only active custom motor");
  g_mixMode=3; v=compose_vibration();
  check(v.LeftMotor==0 && v.RightMotor==50000/65535.0, "legacy keeps directional suppression");
  g_customGain=0; g_mixMode=2; v=compose_vibration();
  check(v.RightMotor==30000/65535.0 && v.RightTrigger==0, "zero custom gain restores native and mutes triggers");
  g_nativeGain=.5f; g_customGain=.5f; g_mixMode=0; v=compose_vibration();
  check(std::fabs(v.LeftMotor-15000/65535.0)<1e-9 && std::fabs(v.RightTrigger-5000/65535.0)<1e-9,
        "independent gains scale native body and custom triggers");
  g_mixMode=1; g_nativeGain=g_customGain=1; g_bodyRight=g_shiftBodyRight=50000;
  check(compose_vibration().RightMotor==1, "addition clamps at full strength");
  sockaddr_in peer{}; peer.sin_family=AF_INET; peer.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  check(!diagnostic_query("BCQ1 1 1 4 1 1",peer,1000), "unknown mixer mode rejected");
  check(!diagnostic_query("BCQ1 1 1 0 nan 1",peer,1000), "nonfinite gain rejected");
  check(!diagnostic_query("BCQ1 1 1 0 1 2",peer,1000), "excessive gain rejected");
  check(!diagnostic_query("BCQ1 1 1 0 1 1 extra",peer,1000), "trailing input rejected");
  peer.sin_addr.s_addr=htonl(0xC0A80101);
  check(!diagnostic_query("BCQ1 1 1 0 1 1",peer,1000), "nonloopback diagnostic rejected");
  g_mixMode=0; g_nativeGain=g_customGain=1; reset_output_state();
}
static void test_timing_records_slow_calls() {
  const UINT32 previous = g_timing[0].slow;
  { CallTimer timer(TimedCall::Initialize); Sleep(8); }
  check(g_timing[0].last >= 5 && g_timing[0].peak >= g_timing[0].last &&
        g_timing[0].slow == previous + 1, "slow calls are measured and counted");
}
int main() {
  try {
    test_routing_and_disable();
    test_idle_startup_defers_wgi();
    test_packet_state();
    test_startup_baseline_and_starter();
    test_shift_confirmation_does_not_replay();
    test_controller_pairing();
    test_mixer_and_diagnostic_validation();
    test_timing_records_slow_calls();
    test_socket_recovery();
    puts("PASS: native regression suite (mock WGI/XInput, real ephemeral UDP)");
    return 0;
  } catch (const std::exception& e) {
    fprintf(stderr, "FAIL: %s\n", e.what());
    close_socket();
    return 1;
  }
}
