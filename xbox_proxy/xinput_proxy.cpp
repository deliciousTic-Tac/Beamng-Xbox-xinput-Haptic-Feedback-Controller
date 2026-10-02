// BeamNG Controller Haptics proxy. Loaded as XInput1_4.dll by BeamNG.
// It forwards input to the real Microsoft DLL and merges BeamNG's ordinary
// XInput vibration with independent Xbox Impulse Trigger motors.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define XInputGetState XInputGetState_sdk
#define XInputSetState XInputSetState_sdk
#define XInputGetCapabilities XInputGetCapabilities_sdk
#define XInputEnable XInputEnable_sdk
#define XInputGetAudioDeviceIds XInputGetAudioDeviceIds_sdk
#define XInputGetBatteryInformation XInputGetBatteryInformation_sdk
#define XInputGetKeystroke XInputGetKeystroke_sdk
#include <xinput.h>
#undef XInputGetState
#undef XInputSetState
#undef XInputGetCapabilities
#undef XInputEnable
#undef XInputGetAudioDeviceIds
#undef XInputGetBatteryInformation
#undef XInputGetKeystroke
#include <roapi.h>
#include <winstring.h>
#include <windows.gaming.input.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <wrl/client.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "runtimeobject.lib")

using ABI::Windows::Gaming::Input::GamepadVibration;
using ABI::Windows::Gaming::Input::GamepadReading;
using ABI::Windows::Gaming::Input::IGamepad;
using ABI::Windows::Gaming::Input::IGamepadStatics;
using Microsoft::WRL::ComPtr;

namespace {
constexpr USHORT PORT = 26780;
constexpr DWORD ERROR_NO_GAMEPAD = ERROR_DEVICE_NOT_CONNECTED;
constexpr ULONGLONG PUMP_INTERVAL_MS = 16;
constexpr ULONGLONG PACKET_TIMEOUT_MS = 250;
constexpr ULONGLONG SOCKET_RETRY_MS = 5000;
constexpr ULONGLONG GAMEPAD_RETRY_MS = 1000;
constexpr ULONGLONG GAMEPAD_PAIR_RETRY_MS = 200;
constexpr ULONGLONG GAMEPAD_SCAN_MS = 1000;
constexpr ULONGLONG GAMEPAD_PAIR_CONFIRM_MS = 100;
constexpr UINT32 MAX_WGI_GAMEPADS = 16;
constexpr int MAX_DATAGRAMS_PER_PUMP = 16;

#pragma pack(push, 1)
struct Packet {
  char magic[4]; UINT32 vehicle; float rpm, limit, throttle, brake, absActive, hasABS, lock, speed, engineRunning, starterActive, rtIntensity, absIntensity, rtStart; INT32 gearIndex; float shiftIntensity, isShifting, shiftMode, startupIntensity, startupInterval, absSensitivity, brakeLockIntensity; UINT32 flags;
};
#pragma pack(pop)
static_assert(sizeof(Packet) == 96, "BCH1 packet layout");

using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using SetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using GetCapsFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
using EnableFn = void(WINAPI*)(BOOL);
using AudioFn = DWORD(WINAPI*)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);
using BatteryFn = DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
using KeyFn = DWORD(WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE);
using StateExFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using GuideWaitFn = DWORD(WINAPI*)(DWORD, DWORD, void*);
using CancelGuideFn = DWORD(WINAPI*)(DWORD);
using PowerOffFn = DWORD(WINAPI*)(DWORD);
using BusFn = DWORD(WINAPI*)(DWORD, void*);
using CapsExFn = DWORD(WINAPI*)(DWORD, DWORD, DWORD, void*);

HMODULE g_real = nullptr;
GetStateFn g_getState = nullptr; SetStateFn g_setState = nullptr; GetCapsFn g_getCaps = nullptr;
EnableFn g_enable = nullptr; AudioFn g_audio = nullptr; BatteryFn g_battery = nullptr; KeyFn g_key = nullptr;
StateExFn g_stateEx = nullptr; GuideWaitFn g_guideWait = nullptr; CancelGuideFn g_cancelGuide = nullptr;
PowerOffFn g_powerOff = nullptr; BusFn g_bus = nullptr; CapsExFn g_capsEx = nullptr;
CapsExFn g_unknown109 = nullptr;

std::atomic<USHORT> g_bodyLeft{0}, g_bodyRight{0}, g_triggerLeft{0}, g_triggerRight{0};
std::atomic<USHORT> g_shiftBodyLeft{0}, g_shiftBodyRight{0};
std::atomic<bool> g_directionalShiftBody{false};
std::atomic<INT32> g_directionalShiftDirection{1};
std::atomic<bool> g_wgiAvailable{false};
std::atomic<bool> g_hapticsEnabled{true};
std::atomic<ULONGLONG> g_packets{0}, g_lastPacket{0};
// Updated continuously through the BeamNG vehicle telemetry packet.
std::atomic<float> g_rtIntensity{1.0f}, g_absIntensity{0.50f}, g_rtStart{0.90f}, g_shiftIntensity{0.55f};
std::atomic<float> g_absSensitivity{0.35f}, g_brakeLockIntensity{0.06f};
INIT_ONCE g_realInit = INIT_ONCE_STATIC_INIT;
SOCKET g_socket = INVALID_SOCKET;
bool g_winsockStarted = false;
ULONGLONG g_socketRetryAt = 0;
SRWLOCK g_outputLock = SRWLOCK_INIT;
SRWLOCK g_logLock = SRWLOCK_INIT;
std::atomic<ULONGLONG> g_lastPump{0}, g_lastGamepadProbe{0}, g_lastLog{0};
GamepadVibration g_lastApplied{};
bool g_hasApplied = false;
// These raw references are released only while an apartment is active and
// g_outputLock is held. A global ComPtr destructor would run under loader lock.
IGamepad* g_selectedPad = nullptr;
IGamepad* g_pairCandidate = nullptr;
ULONGLONG g_pairCandidateSince = 0, g_lastPadScan = 0;
bool g_sawGamepads = false;
ULONGLONG g_lastPairLog = 0;
bool g_pairEverLogged = false;

// Only XInputEnable waits: ordinary polling skips a busy pump. COM calls and
// changes to RuntimeState stay serialized even when XInput uses several threads.
class OutputGuard {
 public:
  explicit OutputGuard(bool wait) : held_(wait || TryAcquireSRWLockExclusive(&g_outputLock)) {
    if (wait) AcquireSRWLockExclusive(&g_outputLock);
  }
  ~OutputGuard() { if (held_) ReleaseSRWLockExclusive(&g_outputLock); }
  explicit operator bool() const { return held_; }
  OutputGuard(const OutputGuard&) = delete;
  OutputGuard& operator=(const OutputGuard&) = delete;
 private:
  bool held_;
};

class ApartmentScope {
 public:
  ApartmentScope() : result_(RoInitialize(RO_INIT_MULTITHREADED)) {
    if (result_ == RPC_E_CHANGED_MODE) result_ = RoInitialize(RO_INIT_SINGLETHREADED);
  }
  ~ApartmentScope() { if (SUCCEEDED(result_)) RoUninitialize(); }
  bool ready() const { return SUCCEEDED(result_); }
  ApartmentScope(const ApartmentScope&) = delete;
  ApartmentScope& operator=(const ApartmentScope&) = delete;
 private:
  HRESULT result_;
};

struct RuntimeState {
  Packet last{};
  ULONGLONG received = 0, limiterUntil = 0, absUntil = 0, shiftUntil = 0, startupUntil = 0;
  ULONGLONG shiftPendingUntil = 0, shiftPendingStarted = 0;
  UINT32 lastVehicle = 0;
  INT32 lastGear = 0, shiftDirection = 1;
  float shiftStartRPM = 0.f;
  bool gearInitialized = false, wasShifting = false, startupArmed = true, vehicleInitialized = false;
  bool engineInitialized = false, wasEngineRunning = false, shiftPending = false;
};
RuntimeState g_runtime;

void logline(const char* line) {
  wchar_t appdata[1024]{};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, _countof(appdata));
  if (!length || length >= _countof(appdata)) return;
  wchar_t dir[1100]{}, path[1150]{};
  if (_snwprintf_s(dir, _countof(dir), _TRUNCATE, L"%ls\\BeamNG-Controller-Haptics", appdata) < 0 ||
      _snwprintf_s(path, _countof(path), _TRUNCATE, L"%ls\\proxy.log", dir) < 0) return;
  if (!CreateDirectoryW(dir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
  if (!TryAcquireSRWLockExclusive(&g_logLock)) return;
  WIN32_FILE_ATTRIBUTE_DATA info{};
  const bool truncate = GetFileAttributesExW(path, GetFileExInfoStandard, &info) &&
      (info.nFileSizeHigh || info.nFileSizeLow >= 1024 * 1024);
  FILE* f = nullptr;
  if (_wfopen_s(&f, path, truncate ? L"w" : L"a") == 0 && f) {
    SYSTEMTIME time{}; GetLocalTime(&time);
    fprintf(f, "[%04u-%02u-%02u %02u:%02u:%02u] %s\n", time.wYear, time.wMonth,
            time.wDay, time.wHour, time.wMinute, time.wSecond, line);
    fclose(f);
  }
  ReleaseSRWLockExclusive(&g_logLock);
}

void log_throttled(const char* line, ULONGLONG now) {
  ULONGLONG previous = g_lastLog.load();
  if (now - previous >= 10000 && g_lastLog.compare_exchange_strong(previous, now)) logline(line);
}

BOOL CALLBACK load_real_once(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t system[MAX_PATH]{}, path[MAX_PATH]{};
  const UINT length = GetSystemDirectoryW(system, _countof(system));
  if (!length || length >= _countof(system) ||
      _snwprintf_s(path, _countof(path), _TRUNCATE, L"%ls\\XInput1_4.dll", system) < 0) {
    logline("[ERROR][Proxy] Invalid system directory; refusing ambiguous DLL path");
    return TRUE;
  }
  g_real = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!g_real) { logline("[ERROR][Proxy] Unable to load system XInput1_4.dll"); return TRUE; }
  g_getState = reinterpret_cast<GetStateFn>(GetProcAddress(g_real, "XInputGetState"));
  g_setState = reinterpret_cast<SetStateFn>(GetProcAddress(g_real, "XInputSetState"));
  g_getCaps = reinterpret_cast<GetCapsFn>(GetProcAddress(g_real, "XInputGetCapabilities"));
  g_enable = reinterpret_cast<EnableFn>(GetProcAddress(g_real, "XInputEnable"));
  g_audio = reinterpret_cast<AudioFn>(GetProcAddress(g_real, "XInputGetAudioDeviceIds"));
  g_battery = reinterpret_cast<BatteryFn>(GetProcAddress(g_real, "XInputGetBatteryInformation"));
  g_key = reinterpret_cast<KeyFn>(GetProcAddress(g_real, "XInputGetKeystroke"));
  g_stateEx = reinterpret_cast<StateExFn>(GetProcAddress(g_real, reinterpret_cast<LPCSTR>(100)));
  g_guideWait = reinterpret_cast<GuideWaitFn>(GetProcAddress(g_real, reinterpret_cast<LPCSTR>(101)));
  g_cancelGuide = reinterpret_cast<CancelGuideFn>(GetProcAddress(g_real, reinterpret_cast<LPCSTR>(102)));
  g_powerOff = reinterpret_cast<PowerOffFn>(GetProcAddress(g_real, reinterpret_cast<LPCSTR>(103)));
  g_bus = reinterpret_cast<BusFn>(GetProcAddress(g_real, reinterpret_cast<LPCSTR>(104)));
  g_capsEx = reinterpret_cast<CapsExFn>(GetProcAddress(g_real, reinterpret_cast<LPCSTR>(108)));
  g_unknown109 = reinterpret_cast<CapsExFn>(GetProcAddress(g_real, reinterpret_cast<LPCSTR>(109)));
  if (!g_getState || !g_setState || !g_enable) logline("[ERROR][Proxy] Missing required XInput export");
  return TRUE;
}

void load_real() {
#ifndef HAPTICS_TEST
  InitOnceExecuteOnce(&g_realInit, load_real_once, nullptr, nullptr);
#endif
}

bool finite_unit(float v) { return std::isfinite(v) && v >= 0.f && v <= 1.f; }

bool valid_packet(const Packet& p) {
  return !memcmp(p.magic, "BCH1", 4) &&
    std::isfinite(p.rpm) && p.rpm >= 0.f && std::isfinite(p.limit) && p.limit >= 0.f &&
    finite_unit(p.throttle) && finite_unit(p.brake) && finite_unit(p.absActive) &&
    finite_unit(p.hasABS) && finite_unit(p.lock) && std::isfinite(p.speed) && p.speed >= 0.f &&
    finite_unit(p.engineRunning) && finite_unit(p.starterActive) && finite_unit(p.rtIntensity) &&
    finite_unit(p.absIntensity) && std::isfinite(p.rtStart) && p.rtStart >= .75f && p.rtStart <= .98f &&
    finite_unit(p.shiftIntensity) && finite_unit(p.isShifting) && finite_unit(p.shiftMode) &&
    finite_unit(p.startupIntensity) && finite_unit(p.startupInterval) &&
    finite_unit(p.absSensitivity) && finite_unit(p.brakeLockIntensity);
}

void calculate(const Packet& p, ULONGLONG now, ULONGLONG& limiterUntil, ULONGLONG& absUntil,
               ULONGLONG& shiftUntil, ULONGLONG& startupUntil, UINT32& lastVehicle,
               INT32& lastGear,
               INT32& shiftDirection, bool& gearInitialized, bool& wasShifting,
               bool& startupArmed, bool& vehicleInitialized,
               bool& engineInitialized, bool& wasEngineRunning,
               ULONGLONG& shiftPendingUntil, ULONGLONG& shiftPendingStarted,
               float& shiftStartRPM, bool& shiftPending) {
  USHORT lt = 0, rt = 0;
  const bool valid = valid_packet(p) && (p.flags & 1) && (p.flags & 4);
  if (!valid) {
    limiterUntil = absUntil = shiftUntil = startupUntil = 0;
    shiftPending = false;
    g_triggerLeft.store(0); g_triggerRight.store(0);
    g_shiftBodyLeft.store(0); g_shiftBodyRight.store(0);
    g_directionalShiftBody.store(false);
    return;
  }
  if (valid) {
    g_rtIntensity.store(p.rtIntensity);
    g_absIntensity.store(p.absIntensity);
    g_rtStart.store(p.rtStart);
    g_shiftIntensity.store(p.shiftIntensity);
    g_absSensitivity.store(p.absSensitivity);
    g_brakeLockIntensity.store(p.brakeLockIntensity);
    const bool newVehicle = startupArmed || !vehicleInitialized || p.vehicle != lastVehicle;
    if (newVehicle) {
      limiterUntil = absUntil = shiftUntil = startupUntil = 0;
      shiftPending = false;
      gearInitialized = wasShifting = false;
      lastGear = 0;
      startupArmed = false;
      vehicleInitialized = true;
      lastVehicle = p.vehicle;
      engineInitialized = false;
      wasEngineRunning = false;
    }
    const bool engineRunning = p.engineRunning > .5f;
    if (engineRunning && (!engineInitialized || !wasEngineRunning)) startupUntil = now + 1200;
    engineInitialized = true;
    wasEngineRunning = engineRunning;
    const bool gearChanged = gearInitialized && p.gearIndex != lastGear && p.gearIndex != 0 && lastGear != 0;
    const bool shiftStarted = gearInitialized && p.isShifting > .5f && !wasShifting;
    if (gearChanged) shiftDirection = p.gearIndex > lastGear ? 1 : -1;
    if (gearChanged && p.shiftIntensity > 0.f) {
      // Race/manual gearboxes can assert isShifting before gearIndex changes.
      // Use the confirmed gear transition to choose the correct side.
      shiftUntil = now + 180;
      shiftPending = false;
    } else if (shiftStarted && p.shiftIntensity > 0.f) {
      // Hold a pending event long enough for the delayed gearIndex update.
      // This prevents a downshift from being mistaken for an upshift.
      shiftPendingUntil = now + 550;
      shiftPendingStarted = now;
      shiftStartRPM = p.rpm;
      shiftPending = true;
    }
    // Keep the last engaged gear across the brief neutral phase used by some
    // manual/race boxes. This lets 3 -> N -> 2 still be recognized as a downshift.
    if (p.gearIndex != 0) lastGear = p.gearIndex;
    gearInitialized = true;
    wasShifting = p.isShifting > .5f;
  }
  if (shiftPending && now - shiftPendingStarted >= 70 && std::isfinite(shiftStartRPM)) {
    // If gearIndex is delayed or unavailable, the RPM step still identifies
    // the operation: a rise is a downshift, a drop is an upshift.
    const float delta = p.rpm - shiftStartRPM;
    const float threshold = std::max(120.f, shiftStartRPM * .04f);
    if (delta > threshold || delta < -threshold) {
      shiftDirection = delta > 0.f ? -1 : 1;
      shiftUntil = now + 180;
      shiftPending = false;
    }
  }
  if (shiftPending && now >= shiftPendingUntil) {
    // Fallback for gearboxes that expose only isShifting. Keep the last known
    // direction, but do not let the pending state suppress future shifts.
    shiftUntil = now + 115;
    shiftPending = false;
  }
  if (valid && p.throttle > .08f && p.limit > 0.f) {
    const float ratio = p.rpm / p.limit;
    const float start = g_rtStart.load();
    const float intensity = g_rtIntensity.load();
    if ((p.flags & 2) || ratio >= .99f) limiterUntil = now + 110;
    float amount = 0.f;
    if (now < limiterUntil) amount = .85f * intensity;
    else if (ratio >= start) amount = (.45f + .40f * std::min(1.f, (ratio - start) / (1.f - start))) * intensity;
    if ((now / 50) % 2 == 0) rt = static_cast<USHORT>(amount * 65535.f);
  }
  const float absThreshold = std::max(.05f, .65f * (1.f - g_absSensitivity.load()));
  const bool hardLockNoAbs = valid && p.brake > .1f && p.speed > 2.f && p.hasABS <= 0.f && p.lock > absThreshold;
  if (valid && p.brake > .1f && p.speed > 2.f && (p.absActive > 0.f || p.lock > absThreshold)) {
    absUntil = now + 90;
  }
  if (hardLockNoAbs) {
    // A wheel lock without ABS gets a continuous, low-strength LT signal.
    lt = static_cast<USHORT>(g_brakeLockIntensity.load() * 65535.f);
  } else if (now < absUntil && (now / 42) % 2 == 0) {
    lt = static_cast<USHORT>(g_absIntensity.load() * 65535.f);
  }
  USHORT shiftBodyLeft = 0, shiftBodyRight = 0;
  bool directionalShiftBody = false;
  if (now < shiftUntil) {
    const float impact = g_shiftIntensity.load();
    const int mode = p.shiftMode < .25f ? 0 : (p.shiftMode > .75f ? 2 : 1);
    const USHORT body = static_cast<USHORT>(.42f * impact * 65535.f);
    const USHORT trigger = static_cast<USHORT>(.70f * impact * 65535.f);
    if (mode == 0 || mode == 2) {
      directionalShiftBody = true;
      if (shiftDirection >= 0) shiftBodyRight = body;
      else shiftBodyLeft = body;
    }
    if (mode == 1 || mode == 2) {
      if (shiftDirection >= 0) rt = std::max(rt, trigger);
      else lt = std::max(lt, trigger);
    }
  }
  if (valid && now < startupUntil && p.startupIntensity > 0.f) {
    // BCH1: Lua encodes the 50..300 ms UI range as (value - 50) / 250.
    const ULONGLONG interval = 50 + static_cast<ULONGLONG>(p.startupInterval * 250.f);
    const ULONGLONG elapsed = 1200 - (startupUntil - now);
    const ULONGLONG phase = elapsed / interval;
    const ULONGLONG inPhase = elapsed % interval;
    if (inPhase < std::min<ULONGLONG>(80, interval * 6 / 10)) {
      const USHORT amount = static_cast<USHORT>(.80f * p.startupIntensity * 65535.f);
      if ((phase & 1) == 0) shiftBodyLeft = std::max(shiftBodyLeft, amount);
      else shiftBodyRight = std::max(shiftBodyRight, amount);
    }
  }
  if (valid && p.starterActive > .5f && p.startupIntensity > 0.f) {
    // Follow the starter signal itself, so feedback begins before the engine fires.
    const USHORT amount = static_cast<USHORT>(.80f * p.startupIntensity * 65535.f);
    if ((now / 70) % 2 == 0) shiftBodyLeft = std::max(shiftBodyLeft, amount);
    else shiftBodyRight = std::max(shiftBodyRight, amount);
  }
  g_triggerLeft.store(lt); g_triggerRight.store(rt);
  g_shiftBodyLeft.store(shiftBodyLeft); g_shiftBodyRight.store(shiftBodyRight);
  g_directionalShiftBody.store(directionalShiftBody);
  g_directionalShiftDirection.store(shiftDirection);
}

void close_socket() {
  if (g_socket != INVALID_SOCKET) { closesocket(g_socket); g_socket = INVALID_SOCKET; }
  if (g_winsockStarted) { WSACleanup(); g_winsockStarted = false; }
}

void socket_error(const char* operation, int error, ULONGLONG now) {
  char message[200]{};
  _snprintf_s(message, _countof(message), _TRUNCATE,
              "[ERROR][UDP] %s failed (%d); retry in 5 seconds", operation, error);
  log_throttled(message, now);
  close_socket();
  g_socketRetryAt = now + SOCKET_RETRY_MS;
}

// Called under g_outputLock. Initialization failures are transient, not InitOnce.
void ensure_socket(ULONGLONG now, USHORT port = PORT) {
  if (g_socket != INVALID_SOCKET || now < g_socketRetryAt) return;
  WSADATA ws{};
  const int startup = WSAStartup(MAKEWORD(2, 2), &ws);
  if (startup != 0) { socket_error("WSAStartup", startup, now); return; }
  g_winsockStarted = true;
  g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_socket == INVALID_SOCKET) { socket_error("socket", WSAGetLastError(), now); return; }
  u_long nonblocking = 1;
  if (ioctlsocket(g_socket, FIONBIO, &nonblocking) == SOCKET_ERROR) {
    socket_error("ioctlsocket(FIONBIO)", WSAGetLastError(), now); return;
  }
  const BOOL exclusive = TRUE;
  if (setsockopt(g_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                 reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
    socket_error("SO_EXCLUSIVEADDRUSE", WSAGetLastError(), now); return;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(g_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    socket_error("bind(loopback)", WSAGetLastError(), now); return;
  }
  g_socketRetryAt = 0;
  logline("[INFO][UDP] Loopback listener ready (synchronous pump)");
}

void clear_telemetry_state(RuntimeState& state) {
  g_triggerLeft.store(0);
  g_triggerRight.store(0);
  g_shiftBodyLeft.store(0);
  g_shiftBodyRight.store(0);
  g_directionalShiftBody.store(false);
  state = RuntimeState{};
}

void pump_telemetry(ULONGLONG now) {
  if (g_socket != INVALID_SOCKET) {
    // Limit one input call to 16 datagrams. Other local telemetry users cannot
    // flood an XInput call and stall BeamNG's input thread.
    char buffer[sizeof(Packet)];
    for (int i = 0; i < MAX_DATAGRAMS_PER_PUMP; ++i) {
      const int n = recv(g_socket, buffer, sizeof(buffer), 0);
      if (n == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        if (error == WSAEMSGSIZE) continue; // Datagram discarded, bounded by the loop.
        if (error != WSAEWOULDBLOCK) {
          socket_error("recv", error, now);
          clear_telemetry_state(g_runtime);
        }
        break;
      }
      if (n != sizeof(Packet)) continue;
      Packet packet{};
      memcpy(&packet, buffer, sizeof(packet));
      if (!memcmp(packet.magic, "BCH1", 4)) {
        if (!valid_packet(packet) || !(packet.flags & 1) || !(packet.flags & 4)) {
          // Invalid/stop packets must not extend an old shift or ABS pulse.
          clear_telemetry_state(g_runtime);
          continue;
        }
        g_runtime.last = packet;
        g_runtime.received = now;
        g_lastPacket.store(now);
        g_packets.fetch_add(1);
      }
    }
  }
  if (g_runtime.received && now - g_runtime.received <= PACKET_TIMEOUT_MS) {
    calculate(g_runtime.last, now, g_runtime.limiterUntil, g_runtime.absUntil,
              g_runtime.shiftUntil, g_runtime.startupUntil, g_runtime.lastVehicle,
              g_runtime.lastGear, g_runtime.shiftDirection, g_runtime.gearInitialized,
              g_runtime.wasShifting, g_runtime.startupArmed, g_runtime.vehicleInitialized,
              g_runtime.engineInitialized, g_runtime.wasEngineRunning,
              g_runtime.shiftPendingUntil, g_runtime.shiftPendingStarted,
              g_runtime.shiftStartRPM, g_runtime.shiftPending);
  } else {
    clear_telemetry_state(g_runtime);
  }
}

GamepadVibration compose_vibration() {
  GamepadVibration v{};
  if (!g_hapticsEnabled.load()) return v;
  USHORT bodyLeft = g_bodyLeft.load(), bodyRight = g_bodyRight.load();
  if (g_directionalShiftBody.load()) {
    if (g_directionalShiftDirection.load() >= 0) bodyLeft = 0;
    else bodyRight = 0;
  }
  v.LeftMotor = std::min(1.0, (bodyLeft + g_shiftBodyLeft.load()) / 65535.0);
  v.RightMotor = std::min(1.0, (bodyRight + g_shiftBodyRight.load()) / 65535.0);
  v.LeftTrigger = g_triggerLeft.load() / 65535.0;
  v.RightTrigger = g_triggerRight.load() / 65535.0;
  return v;
}

bool same_vibration(const GamepadVibration& a, const GamepadVibration& b) {
  return a.LeftMotor == b.LeftMotor && a.RightMotor == b.RightMotor &&
         a.LeftTrigger == b.LeftTrigger && a.RightTrigger == b.RightTrigger;
}

bool zero_vibration(const GamepadVibration& v) {
  return v.LeftMotor == 0 && v.RightMotor == 0 &&
         v.LeftTrigger == 0 && v.RightTrigger == 0;
}

// XInput has no documented ID that can be compared with a WGI Gamepad ID.
// Correlate a distinctive live input instead of assuming the two lists share
// an order. Ambiguous or idle inputs never establish a new pairing.
struct PadInput {
  bool valid = false;
  UINT32 buttons = 0;
  double leftTrigger = 0, rightTrigger = 0;
  double leftX = 0, leftY = 0, rightX = 0, rightY = 0;
};

UINT32 xinput_button_mask(WORD buttons) {
  UINT32 result = 0;
  if (buttons & XINPUT_GAMEPAD_START) result |= 1u;
  if (buttons & XINPUT_GAMEPAD_BACK) result |= 2u;
  if (buttons & XINPUT_GAMEPAD_A) result |= 4u;
  if (buttons & XINPUT_GAMEPAD_B) result |= 8u;
  if (buttons & XINPUT_GAMEPAD_X) result |= 16u;
  if (buttons & XINPUT_GAMEPAD_Y) result |= 32u;
  if (buttons & XINPUT_GAMEPAD_DPAD_UP) result |= 64u;
  if (buttons & XINPUT_GAMEPAD_DPAD_DOWN) result |= 128u;
  if (buttons & XINPUT_GAMEPAD_DPAD_LEFT) result |= 256u;
  if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) result |= 512u;
  if (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) result |= 1024u;
  if (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) result |= 2048u;
  if (buttons & XINPUT_GAMEPAD_LEFT_THUMB) result |= 4096u;
  if (buttons & XINPUT_GAMEPAD_RIGHT_THUMB) result |= 8192u;
  return result;
}

bool distinctive_input(const XINPUT_GAMEPAD& x) {
  return xinput_button_mask(x.wButtons) || x.bLeftTrigger >= 96 || x.bRightTrigger >= 96 ||
         std::abs(static_cast<int>(x.sThumbLX)) >= 16000 ||
         std::abs(static_cast<int>(x.sThumbLY)) >= 16000 ||
         std::abs(static_cast<int>(x.sThumbRX)) >= 16000 ||
         std::abs(static_cast<int>(x.sThumbRY)) >= 16000;
}

double normalized_axis(SHORT axis) {
  return axis < 0 ? axis / 32768.0 : axis / 32767.0;
}

bool axis_matches(SHORT x, double wgi) {
  if (std::abs(static_cast<int>(x)) < 8000 && std::fabs(wgi) < .30) return true;
  return std::fabs(normalized_axis(x) - wgi) <= .25;
}

bool trigger_matches(BYTE x, double wgi) {
  if (x < 32 && wgi < .20) return true;
  return std::fabs(x / 255.0 - wgi) <= .15;
}

bool matching_input(const XINPUT_GAMEPAD& x, const PadInput& w) {
  return w.valid &&
         xinput_button_mask(x.wButtons) == (w.buttons & 0x3fffu) &&
         trigger_matches(x.bLeftTrigger, w.leftTrigger) &&
         trigger_matches(x.bRightTrigger, w.rightTrigger) &&
         axis_matches(x.sThumbLX, w.leftX) && axis_matches(x.sThumbLY, w.leftY) &&
         axis_matches(x.sThumbRX, w.rightX) && axis_matches(x.sThumbRY, w.rightY);
}

int unique_matching_pad(const XINPUT_GAMEPAD& x, const PadInput* readings, UINT32 count) {
  if (!distinctive_input(x)) return -1;
  int unique = -1;
  for (UINT32 i = 0; i < count; ++i) {
    if (!matching_input(x, readings[i])) continue;
    if (unique >= 0) return -1;
    unique = static_cast<int>(i);
  }
  return unique;
}

PadInput pad_input(const GamepadReading& reading) {
  PadInput result{};
  result.valid = true;
  result.buttons = static_cast<UINT32>(reading.Buttons);
  result.leftTrigger = reading.LeftTrigger;
  result.rightTrigger = reading.RightTrigger;
  result.leftX = reading.LeftThumbstickX;
  result.leftY = reading.LeftThumbstickY;
  result.rightX = reading.RightThumbstickX;
  result.rightY = reading.RightThumbstickY;
  return result;
}

bool same_pad(IGamepad* a, IGamepad* b) {
  if (!a || !b) return false;
  ComPtr<IUnknown> left, right;
  return SUCCEEDED(a->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(left.GetAddressOf()))) &&
         SUCCEEDED(b->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(right.GetAddressOf()))) &&
         left.Get() == right.Get();
}

void release_pad(IGamepad*& pad) {
  if (pad) { pad->Release(); pad = nullptr; }
}

void drop_selected_pad(bool silence) {
  if (silence && g_selectedPad) {
    const GamepadVibration zero{};
    g_selectedPad->put_Vibration(zero);
  }
  release_pad(g_selectedPad);
  g_hasApplied = false;
  g_wgiAvailable.store(false);
}

// Called with g_outputLock held, inside an initialized apartment. The selected
// object is retained only if it advertises agility for cross-apartment calls.
bool scan_gamepads(ULONGLONG now) {
  g_lastPadScan = now;
  XINPUT_STATE state{};
  if (!g_getState || g_getState(0, &state) != ERROR_SUCCESS) {
    drop_selected_pad(true);
    release_pad(g_pairCandidate);
    return false;
  }
  HSTRING_HEADER header{};
  HSTRING name{};
  constexpr wchar_t gamepadClass[] = L"Windows.Gaming.Input.Gamepad";
  ComPtr<IGamepadStatics> statics;
  ComPtr<__FIVectorView_1_Windows__CGaming__CInput__CGamepad> pads;
  UINT32 count = 0;
  if (FAILED(WindowsCreateStringReference(gamepadClass, static_cast<UINT32>(wcslen(gamepadClass)), &header, &name)) ||
      FAILED(RoGetActivationFactory(name, __uuidof(IGamepadStatics), reinterpret_cast<void**>(statics.GetAddressOf()))) || !statics ||
      FAILED(statics->get_Gamepads(pads.GetAddressOf())) || !pads ||
      FAILED(pads->get_Size(&count)) || count > MAX_WGI_GAMEPADS) {
    drop_selected_pad(true);
    release_pad(g_pairCandidate);
    g_sawGamepads = false;
    return false;
  }
  g_sawGamepads = count != 0;
  ComPtr<IGamepad> gamepads[MAX_WGI_GAMEPADS];
  PadInput readings[MAX_WGI_GAMEPADS]{};
  bool selectedPresent = false, selectedReadable = false, selectedMatches = false;
  for (UINT32 i = 0; i < count; ++i) {
    if (FAILED(pads->GetAt(i, gamepads[i].GetAddressOf())) || !gamepads[i]) continue;
    GamepadReading reading{};
    if (SUCCEEDED(gamepads[i]->GetCurrentReading(&reading))) readings[i] = pad_input(reading);
    if (same_pad(g_selectedPad, gamepads[i].Get())) {
      selectedPresent = true;
      selectedReadable = readings[i].valid;
      selectedMatches = matching_input(state.Gamepad, readings[i]);
    }
  }
  if (g_selectedPad && (!selectedPresent || !selectedReadable ||
      (distinctive_input(state.Gamepad) && !selectedMatches))) {
    drop_selected_pad(true);
    release_pad(g_pairCandidate);
  }
  if (g_selectedPad) return true;

  const int chosen = unique_matching_pad(state.Gamepad, readings, count);
  if (chosen < 0) { release_pad(g_pairCandidate); return false; }
  IGamepad* match = gamepads[chosen].Get();
  ComPtr<IAgileObject> agile;
  if (FAILED(match->QueryInterface(__uuidof(IAgileObject),
          reinterpret_cast<void**>(agile.GetAddressOf())))) {
    release_pad(g_pairCandidate);
    return false;
  }
  if (same_pad(g_pairCandidate, match) && now - g_pairCandidateSince >= GAMEPAD_PAIR_CONFIRM_MS) {
    release_pad(g_pairCandidate);
    match->AddRef();
    g_selectedPad = match;
    if (!g_pairEverLogged || now - g_lastPairLog >= 10000) {
      g_pairEverLogged = true;
      g_lastPairLog = now;
      logline("[INFO][WGI] XInput 0 paired with a unique active Gamepad");
    }
    return true;
  }
  release_pad(g_pairCandidate);
  match->AddRef();
  g_pairCandidate = match;
  g_pairCandidateSince = now;
  return false;
}

#ifdef HAPTICS_TEST
bool (*g_testApply)(const GamepadVibration&) = nullptr;
#endif

void apply_vibration(ULONGLONG now, bool force = false) {
  // Active output can change every 16 ms; discovery and membership checks are
  // much slower. A pending pairing is sampled every 200 ms, absent pads at 1 Hz.
  const ULONGLONG lastProbe = g_lastGamepadProbe.load();
  const GamepadVibration v = compose_vibration();
  // Do not initialize WinRT or enumerate WGI devices from BeamNG's XInput
  // caller while there is nothing to output. Some driver/WGI combinations can
  // block discovery when no controller is available, freezing the game during
  // startup. A non-zero body or trigger request starts discovery immediately.
  // A selected/previously-applied pad still receives zero so motors are stopped.
  if (!g_selectedPad && !g_wgiAvailable.load() && !g_hasApplied && zero_vibration(v)) return;
  const bool unchanged = g_hasApplied && same_vibration(v, g_lastApplied);
  const ULONGLONG retryInterval = g_selectedPad
      ? (unchanged ? GAMEPAD_RETRY_MS : PUMP_INTERVAL_MS)
      : (g_sawGamepads ? GAMEPAD_PAIR_RETRY_MS : GAMEPAD_RETRY_MS);
  if (!force && now - lastProbe < retryInterval) return;
  g_lastGamepadProbe.store(now);

#ifdef HAPTICS_TEST
  if (g_testApply) {
    g_hasApplied = g_testApply(v);
    g_wgiAvailable.store(g_hasApplied);
    if (g_hasApplied) g_lastApplied = v;
    return;
  }
#endif

  // BeamNG's XInput call can arrive from a thread already initialized as STA.
  // MTA initialization then returns RPC_E_CHANGED_MODE, even though WinRT is
  // usable from that existing apartment. Match it before giving up.
  ApartmentScope apartment;
  if (!apartment.ready()) {
    g_wgiAvailable.store(false);
    g_hasApplied = false;
    log_throttled("[ERROR][WGI] RoInitialize failed", now);
    return;
  }
  if (!g_hapticsEnabled.load()) {
    drop_selected_pad(true);
    release_pad(g_pairCandidate);
    return;
  }
  if (!g_selectedPad || now - g_lastPadScan >= GAMEPAD_SCAN_MS) scan_gamepads(now);
  if (!g_selectedPad) {
    g_wgiAvailable.store(false);
    g_hasApplied = false;
    return;
  }
  const bool applied = SUCCEEDED(g_selectedPad->put_Vibration(v));
  if (!applied) {
    drop_selected_pad(false);
    release_pad(g_pairCandidate);
    log_throttled("[WARN][WGI] Gamepad output failed; pairing will retry", now);
    return;
  }
  g_hasApplied = true;
  g_lastApplied = v;
  g_wgiAvailable.store(true);
}

void pump_haptics() noexcept {
  OutputGuard guard(false);
  if (!guard) return;
  const ULONGLONG now = GetTickCount64();
  if (now - g_lastPump.load() < PUMP_INTERVAL_MS) return;
  g_lastPump.store(now);
  if (g_hapticsEnabled.load()) {
    ensure_socket(now);
    pump_telemetry(now);
  }
  apply_vibration(now);
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI XInputGetState(DWORD id, XINPUT_STATE* state) noexcept {
  load_real();
  const DWORD result = g_getState ? g_getState(id, state) : ERROR_NO_GAMEPAD;
  pump_haptics();
  return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI XInputSetState(DWORD id, XINPUT_VIBRATION* v) noexcept {
  load_real();
  // Delegate invalid arguments as well: preserve the real DLL's error behavior.
  if (id != 0 || !v) return g_setState ? g_setState(id, v) : ERROR_NO_GAMEPAD;
  if (v && id == 0) { g_bodyLeft.store(v->wLeftMotorSpeed); g_bodyRight.store(v->wRightMotorSpeed); }
  pump_haptics();
  // When WGI is ready it applies all four motors atomically; forwarding would clear RT/LT.
  return g_wgiAvailable.load() ? ERROR_SUCCESS : (g_setState ? g_setState(id,v) : ERROR_NO_GAMEPAD);
}
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD a,DWORD b,XINPUT_CAPABILITIES* c) noexcept { load_real(); return g_getCaps ? g_getCaps(a,b,c) : ERROR_NO_GAMEPAD; }
extern "C" __declspec(dllexport) void WINAPI XInputEnable(BOOL enabled) noexcept {
  load_real();
  OutputGuard guard(true);
  g_hapticsEnabled.store(enabled != FALSE);
  if (g_enable) g_enable(enabled);
  if (!enabled) {
    clear_telemetry_state(g_runtime);
    close_socket();
    g_socketRetryAt = 0;
  }
  // Keep the last ordinary motor request, per XInputEnable's documented contract.
  // Force bypasses the output throttle; subsequent pumps obey the disabled state.
  apply_vibration(GetTickCount64(), true);
}
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetAudioDeviceIds(DWORD a,LPWSTR b,UINT* c,LPWSTR d,UINT* e) noexcept { load_real(); return g_audio ? g_audio(a,b,c,d,e) : ERROR_NO_GAMEPAD; }
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetBatteryInformation(DWORD a,BYTE b,XINPUT_BATTERY_INFORMATION* c) noexcept { load_real(); return g_battery ? g_battery(a,b,c) : ERROR_NO_GAMEPAD; }
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetKeystroke(DWORD a,DWORD b,PXINPUT_KEYSTROKE c) noexcept { load_real(); return g_key ? g_key(a,b,c) : ERROR_NO_GAMEPAD; }
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetStateEx(DWORD a,XINPUT_STATE* b) { load_real(); if (!g_stateEx) return XInputGetState(a,b); const DWORD result = g_stateEx(a,b); pump_haptics(); return result; }
extern "C" __declspec(dllexport) DWORD WINAPI XInputWaitForGuideButton(DWORD a,DWORD b,void* c) { load_real(); return g_guideWait ? g_guideWait(a,b,c) : ERROR_CALL_NOT_IMPLEMENTED; }
extern "C" __declspec(dllexport) DWORD WINAPI XInputCancelGuideButtonWait(DWORD a) { load_real(); return g_cancelGuide ? g_cancelGuide(a) : ERROR_CALL_NOT_IMPLEMENTED; }
extern "C" __declspec(dllexport) DWORD WINAPI XInputPowerOffController(DWORD a) { load_real(); return g_powerOff ? g_powerOff(a) : ERROR_CALL_NOT_IMPLEMENTED; }
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetBaseBusInformation(DWORD a,void* b) { load_real(); return g_bus ? g_bus(a,b) : ERROR_CALL_NOT_IMPLEMENTED; }
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetCapabilitiesEx(DWORD a,DWORD b,DWORD c,void* d) { load_real(); return g_capsEx ? g_capsEx(a,b,c,d) : ERROR_CALL_NOT_IMPLEMENTED; }
extern "C" __declspec(dllexport) DWORD WINAPI XInputUnknown109(DWORD a,DWORD b,DWORD c,void* d) { load_real(); return g_unknown109 ? g_unknown109(a,b,c,d) : ERROR_CALL_NOT_IMPLEMENTED; }

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
  // /MT: the static CRT needs thread notifications. Do not disable them.
  // No WinRT, Winsock, synchronization, logging or FreeLibrary under loader lock.
  return TRUE;
}
