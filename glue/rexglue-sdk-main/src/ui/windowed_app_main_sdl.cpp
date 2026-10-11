/**
 * @file        ui/windowed_app_main_sdl.cpp
 * @brief       Entry point for windowed applications (SDL3 windowing)
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/diagnostics/policy.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_sdl.h>

#if REX_PLATFORM_PS4
#include <pthread.h>

#include <cstdint>
#include <orbis/Sysmodule.h>
#endif

REXCVAR_DEFINE_BOOL(diagnostics, false, "Diagnostics",
                    "Enable diagnostic logging and instrumentation");
REXCVAR_DEFINE_STRING(diagnostics_categories, "", "Diagnostics",
                      "Comma-separated diagnostic categories (empty = all; logging controls "
                      "general application logs independently of diagnostic artifacts)");

#if REX_PLATFORM_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif

namespace {

#if REX_PLATFORM_PS4
// os/ps4/runtime/ps4_crashlog.cpp: appends to /data/LibertyRecomp/boot-trace.log.
extern "C" __attribute__((weak)) void ps4_boot_trace(const char* stage);
#define REX_PS4_TRACE(stage)   do {                           if (ps4_boot_trace)            ps4_boot_trace(stage);   } while (0)
#else
#define REX_PS4_TRACE(stage)   do {                         } while (0)
#endif

int RunWindowedApp(int argc, char** argv) {
  REX_PS4_TRACE("RunWindowedApp");
  auto remaining = rex::cvar::Init(argc, argv);
  rex::cvar::ApplyEnvironment();
  // The diagnostics policy is command-line-owned and must be installed before
  // any logger sinks or worker threads exist. The REXLOG macros and the
  // native-probe instrumentation are gated on it; without this call the
  // policy stays unconfigured and every log line is suppressed.
  {
    std::string diagnostics_error;
#if REX_PLATFORM_PS4
    // No command line on PS4: always keep the application log
    // (/data/LibertyRecomp/rex.log), without the heavier instrumentation.
    const bool diagnostics_enabled = true;
    const std::string diagnostics_categories = REXCVAR_GET(diagnostics)
                                                   ? REXCVAR_GET(diagnostics_categories)
                                                   : std::string("logging");
#else
    const bool diagnostics_enabled = REXCVAR_GET(diagnostics);
    const std::string diagnostics_categories = REXCVAR_GET(diagnostics_categories);
#endif
    if (!rex::diagnostics::Configure(diagnostics_enabled, diagnostics_categories,
                                     &diagnostics_error)) {
      std::fprintf(stderr, "diagnostics: %s\n", diagnostics_error.c_str());
      return EXIT_FAILURE;
    }
  }
  rex::InitLoggingEarly();

  int result;
  {
    rex::ui::SDLWindowedAppContext app_context;
    REX_PS4_TRACE("SDL app context");
    if (!app_context.Initialize()) {
      REX_PS4_TRACE("SDL app context: Initialize failed");
      return EXIT_FAILURE;
    }

#if REX_PLATFORM_WIN32
    // Apartment-threaded COM for shell dialogs.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
      return EXIT_FAILURE;
    }
#endif

    std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(app_context);

    // Match remaining positional args to the app's expected options.
    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));

    REX_PS4_TRACE("app OnInitialize");
    if (app->OnInitialize()) {
      REX_PS4_TRACE("main message loop");
      result = app_context.RunMainMessageLoop();
    } else {
      REX_PS4_TRACE("app OnInitialize failed");
      result = EXIT_FAILURE;
    }

    REX_PS4_TRACE("app exiting");
    app->InvokeOnDestroy();
  }

#if REX_PLATFORM_WIN32
  CoUninitialize();
#endif

  return result;
}

#if REX_PLATFORM_WIN32
// Convert wide argv from CommandLineToArgvW to UTF-8 for cvar::Init.
std::vector<std::string> WideArgsToUtf8(int argc, wchar_t** wargv) {
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    std::wstring wide(wargv[i]);
    if (wide.empty()) {
      args.emplace_back();
      continue;
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr,
                                   0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), size,
                        nullptr, nullptr);
    args.push_back(std::move(utf8));
  }
  return args;
}
#endif

}  // namespace

#if REX_PLATFORM_WIN32

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hinstance_prev, LPWSTR command_line,
                    int show_cmd) {
  (void)hinstance;
  (void)hinstance_prev;
  (void)command_line;
  (void)show_cmd;

  int wargc = 0;
  wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  auto utf8_args = WideArgsToUtf8(wargc, wargv);
  LocalFree(wargv);

  std::vector<char*> argv_ptrs;
  argv_ptrs.reserve(utf8_args.size());
  for (auto& s : utf8_args) {
    argv_ptrs.push_back(s.data());
  }
  return RunWindowedApp(static_cast<int>(argv_ptrs.size()), argv_ptrs.data());
}

#elif REX_PLATFORM_PS4

extern "C" __attribute__((weak)) void ps4_install_signal_stack();
extern "C" __attribute__((weak)) void ps4_watch_thread(const char* name);

// Calling into a system library whose module isn't loaded kills the app on the
// spot (PRX_NOT_RESOLVED_FUNCTION, no signal): the first sceVideoOutOpen did.
// DolphinPS4 loads the same set (LoadSystemModules, plus Net in PS4Net.cpp).
void LoadSystemModules() {
  const struct {
    const char* name;
    OrbisSysModuleInternal id;
  } modules[] = {
      {"SystemService", ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE},
      {"UserService", ORBIS_SYSMODULE_INTERNAL_USER_SERVICE},
      {"VideoOut", ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT},
      {"AudioOut", ORBIS_SYSMODULE_INTERNAL_AUDIOOUT},
      {"Pad", ORBIS_SYSMODULE_INTERNAL_PAD},
      {"Net", ORBIS_SYSMODULE_INTERNAL_NET},
  };
  for (const auto& module : modules) {
    const uint32_t result = sceSysmoduleLoadModuleInternal(module.id);
    char line[96];
    std::snprintf(line, sizeof(line), "system module %s: %#x", module.name, result);
    REX_PS4_TRACE(line);
  }
}

// The process's main thread gets the system default stack (sce_process_param
// doesn't set one). Run the app on a thread whose stack size is explicit.
// Debug switches without a rebuild: /data/LibertyRecomp/env.txt holds NAME=value lines (e.g.
// RADV_DEBUG=hang for per-draw GPU trace ids in Mesa's hang report), set before anything reads
// the environment. There is no shell on the PS4 to set them.
void ApplyEnvironmentFile() {
  FILE* file = std::fopen("/data/LibertyRecomp/env.txt", "r");
  if (!file) {
    return;
  }
  char line[512];
  while (std::fgets(line, sizeof(line), file)) {
    std::string entry(line);
    while (!entry.empty() && std::isspace(static_cast<unsigned char>(entry.back()))) {
      entry.pop_back();
    }
    const size_t equals = entry.find('=');
    if (entry.empty() || entry[0] == '#' || equals == std::string::npos || equals == 0) {
      continue;
    }
    setenv(entry.substr(0, equals).c_str(), entry.substr(equals + 1).c_str(), 1);
    REX_PS4_TRACE(("env: " + entry).c_str());
  }
  std::fclose(file);
}

int main(int argc, char* argv[]) {
  ApplyEnvironmentFile();
  LoadSystemModules();
  constexpr size_t kAppStackSize = 16 * 1024 * 1024;
  struct Args {
    int argc;
    char** argv;
    int result;
  } args{argc, argv, EXIT_FAILURE};
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, kAppStackSize);
  pthread_t thread;
  const int error = pthread_create(
      &thread, &attr,
      [](void* data) -> void* {
        auto* args = static_cast<Args*>(data);
        if (ps4_install_signal_stack)
          ps4_install_signal_stack();
        if (ps4_watch_thread)
          ps4_watch_thread("app main");
        args->result = RunWindowedApp(args->argc, args->argv);
        return nullptr;
      },
      &args);
  pthread_attr_destroy(&attr);
  if (error != 0) {
    REX_PS4_TRACE("app thread: pthread_create failed");
    return RunWindowedApp(argc, argv);
  }
  pthread_join(thread, nullptr);
  return args.result;
}

#else

int main(int argc, char* argv[]) {
  return RunWindowedApp(argc, argv);
}

#endif
