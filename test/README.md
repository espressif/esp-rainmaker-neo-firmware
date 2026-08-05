# Testing

PyTest suite that exercises **real firmware** (POSIX binaries on the host, ESP
boards over UART) against a deployed RainMaker Neo backend, driving nodes
through the serial
[host control](../components/esp_rmaker_neo/src/host_ctrl/host_ctrl_python/README.md)
interface and validating cloud state from a simulated user application.

## Prerequisites

The suite **requires the RainMaker Neo backend repository.**
`conftest.py` imports backend test helpers at module level (via
`rmng_backend`), so it cannot even be *collected* without `RMNG_BACKEND_DIR`
pointing at a backend checkout, plus that deployment's credentials.

1. Install the system packages listed under
   [tools/README.md § System prerequisites](../tools/README.md#system-prerequisites).

2. Clone the RainMaker Neo backend and point `RMNG_BACKEND_DIR` at it:

   ```bash
   export RMNG_BACKEND_DIR=/path/to/rmng-backend
   ```

3. Create and activate a virtual environment, then install the backend's
   dependencies **before** this suite's — the order keeps shared version
   constraints resolving predictably:

   ```bash
   pip install -r "${RMNG_BACKEND_DIR}/requirements.txt"
   pip install -r test/requirements.txt
   ```

   `test/requirements.txt` pulls in [`tools/requirements.txt`](../tools/requirements.txt)
   (the shared [`tools/common/`](../tools/common/) library) and
   [`posix_requirements.txt`](../posix_requirements.txt) (POSIX firmware
   builds), so this one install also covers every tool under
   [`tools/`](../tools/).

4. Provide credentials: a `.env` file at the repo root (see
   [`.env.example`](../.env.example)) and the deployment's stack outputs and
   certificates as described in the
   [credentials store README](../tools/common/credentials_store/README.md).

## Running

From this directory:

```bash
pytest -n auto -v
```

- By default both **ESP** and **POSIX** firmware instances are used. With ESP
  enabled, boards are auto-detected over UART and tests are unique per **board
  target type**.
- `--no-esp` — skip board-based instances (no hardware needed).
- `--no-posix` — skip host binaries.
- `--max-concurrent-build-jobs N` — cap concurrent firmware builds (default 2,
  or `PYTEST_MAX_CONCURRENT_BUILD_JOBS`).

Firmware builds and coverage reports land under `build/` relative to the
working directory, so run from here to keep them at `test/build/`.
