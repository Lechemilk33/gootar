#pragma once

/**
 * Makes a crashing test fail instead of waiting for someone to click OK.
 *
 * On Windows, an access violation or a failed assert opens a dialog and blocks
 * until it is dismissed. On a CI runner nobody dismisses it, so the job hangs
 * until the whole workflow times out - which is exactly what happened here,
 * and it looks identical to "the tests are slow" from the outside.
 *
 * No-op everywhere else.
 */
#if defined(_WIN32)
  #include <windows.h>
  #include <crtdbg.h>

  inline void silenceCrashDialogs()
  {
      SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
      _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
      for (int mode : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
      {
          _CrtSetReportMode (mode, _CRTDBG_MODE_FILE);
          _CrtSetReportFile (mode, _CRTDBG_FILE_STDERR);
      }
  }
#else
  inline void silenceCrashDialogs() {}
#endif
