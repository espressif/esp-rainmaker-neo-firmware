# Tools

Command-line tools plus [`common/`](./common/) — the shared library they and the
[integration test suite](../test/README.md) both import. [`docker/`](./docker/)
holds container definitions used by CI, not tools.

## Tools

Fully standalone:

| Tool | Purpose |
|---|---|
| [`factory_nvs_gen`](./factory_nvs_gen/) | Generate a factory NVS partition from JSON input |
| [`factory_attach`](./factory_autoreg/) | Attach a factory partition to a build image (in the `factory_autoreg` folder) |
| [`local_ctrl_cli`](./local_ctrl_cli/) | Interactive local control client |
| [`esp_network_prov_nvs`](./esp_network_prov_nvs/) | Pre-provision network credentials into NVS |

Standalone, but need a deployment's stack outputs (`rmng-outputs.json` in the
[credentials store](./common/credentials_store/README.md), or `--config` where
supported) and AWS credentials:

| Tool | Purpose |
|---|---|
| [`factory_autoreg`](./factory_autoreg/) | Generate node identities and register them via the admin API |
| [`ota_helper`](./ota_helper/) | Set up OTA infrastructure and drive OTA jobs (fails at import without the outputs file) |

## Environment setup

### System prerequisites

Some Python dependencies build native extensions from source. Install these
system packages first:

- `libffi-dev` — required to build `cffi`, which `esp-matter-mfg-tool` pins to
  an old version that has no prebuilt wheel for recent Python. Without it the
  `pip install` fails with `fatal error: ffi.h: No such file or directory`.

  ```bash
  # Debian/Ubuntu
  sudo apt-get install -y libffi-dev

  # macOS
  brew install libffi
  ```

  Alternatively, after installing the requirements below, run
  `pip install -U cffi` in the virtual environment.

### Python environment

1. Create and activate a virtual environment.
2. Install dependencies from [`requirements.txt`](./requirements.txt):

   ```bash
   pip install -r tools/requirements.txt
   ```

3. For AWS or stack-specific workflows, place credentials as described in the
   [credentials store README](./common/credentials_store/README.md) (and
   [`.env.example`](../.env.example) where applicable).

`RMNG_BACKEND_DIR` is **not** needed for any of the tools above.

### Also running the integration tests

To use one environment for both, follow
[test/README.md § Prerequisites](../test/README.md#prerequisites)
instead.
