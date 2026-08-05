# Description

<!-- What this changes and why. If it fixes an open issue, link it here. -->

Fixes #

## Checklist

- [ ] Builds on **both** platforms. Every component here builds for ESP-IDF and POSIX from one
      source tree, so a change that only builds on one is incomplete — see the build instructions
      in `examples/README.md`.
- [ ] `pre-commit run --all-files` passes. Some hooks rewrite files, so run it twice; the second
      run should be clean.
- [ ] Host tests pass: `ctest --test-dir build` from `test/apps/posix/`. If the change
      touches a component with a `test-*`/`test_*` directory, it should come with a test.
- [ ] Common components return `osal_err_t` — not `esp_err_t`, not `bool`.
- [ ] Public API changes are reflected in the relevant doxygen.
- [ ] No new hardcoded paths, credentials, or internal URLs.

## Testing

<!--
How you verified this. Which platform(s), which target chip or host, and whether you ran it on
hardware. "Builds clean" and "tested on an ESP32-C6" are different claims — please say which.
-->
