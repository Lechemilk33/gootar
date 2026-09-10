#pragma once

/**
 * Makes a crashing test fail instead of waiting for someone to click OK.
 *
 * On Windows, an access violation or a failed assert opens a dialog and blocks
 * until it is dismissed. On a CI runner nobody dismisses it, so the job hangs
 * until the whole workflow times out - which looks identical to "the tests are
 * slow" from the outside.
 *
 * No-op everywhere else.
 */
#if defined(_WIN32)

  // <windows.h> defines min and max as MACROS, which turns every later
  // std::max(a, b) into std::(a, b) and produces a wall of errors nowhere near
  // the actual cause. NOMINMAX stops that; WIN32_LEAN_AND_MEAN keeps the rest
  // of the header from dragging in half of Win32.
  //
  // Both must be defined BEFORE the include, and both are guarded because
  // JUCE and AudioDSPTools set them too - redefining them warns.
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif

  #include <windows.h>
  #include <crtdbg.h>

  inline void silenceCrashDialogs()
  {
      SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
      _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

      // Written out rather than looped: these are macros, and a range-for over
      // a braced list of them is more trouble than three lines are worth.
      _CrtSetReportMode (_CRT_WARN, _CRTDBG_MODE_FILE);
      _CrtSetReportFile (_CRT_WARN, _CRTDBG_FILE_STDERR);
      _CrtSetReportMode (_CRT_ERROR, _CRTDBG_MODE_FILE);
      _CrtSetReportFile (_CRT_ERROR, _CRTDBG_FILE_STDERR);
      _CrtSetReportMode (_CRT_ASSERT, _CRTDBG_MODE_FILE);
      _CrtSetReportFile (_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  }

#else

  inline void silenceCrashDialogs() {}

#endif
