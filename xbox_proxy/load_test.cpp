#include <windows.h>
#include <xinput.h>
#include <roapi.h>
#include <atomic>
#include <stdio.h>
#include <thread>

using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using EnableFn = void(WINAPI*)(BOOL);

int wmain(int argc, wchar_t** argv) {
  // Explicit path is required: never load an arbitrary DLL from the current directory.
  if (argc != 2) { fputs("Usage: load_test.exe <absolute path to candidate DLL>\n", stderr); return 1; }
  wchar_t fullPath[32768]{};
  const DWORD size = GetFullPathNameW(argv[1], _countof(fullPath), fullPath, nullptr);
  if (!size || size >= _countof(fullPath)) return 1;
  HMODULE module = LoadLibraryExW(fullPath, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!module) return 1;
  auto getState = reinterpret_cast<GetStateFn>(GetProcAddress(module, reinterpret_cast<LPCSTR>(2)));
  if (!getState) { FreeLibrary(module); return 2; }
  auto enable = reinterpret_cast<EnableFn>(GetProcAddress(module, "XInputEnable"));
  if (!enable) { FreeLibrary(module); return 2; }
  std::atomic<int> calls{0};
  // BeamNG invokes XInput from a COM-initialized thread. Exercise the STA
  // fallback before the concurrent calls below.
  const HRESULT staResult = RoInitialize(RO_INIT_SINGLETHREADED);
  for (int i = 0; i < 20; ++i) {
    XINPUT_STATE state{};
    getState(0, &state);
    ++calls;
    Sleep(2);
  }
  if (SUCCEEDED(staResult)) RoUninitialize();
  auto callState = [&] {
    for (int i = 0; i < 20; ++i) {
      XINPUT_STATE state{};
      getState(0, &state);
      ++calls;
      Sleep(2);
    }
  };
  std::thread first(callState), second(callState), third(callState), fourth(callState);
  first.join(); second.join(); third.join(); fourth.join();
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(module, path, MAX_PATH);
  enable(FALSE); // Release UDP ownership and request zero before an explicit unload.
  const BOOL freed = FreeLibrary(module);
  wprintf(L"%ls (%d concurrent XInputGetState calls)\n", path, calls.load());
  return freed ? 0 : 3;
}
