# Local Control / Challenge-Response Protocol Wrappers

Python wrappers for the protobuf messages used by the SDK's local control and
challenge-response services.

## Files

- `__init__.py` - Command managers:
  - `LocalCtrlCommandManager` - the local control endpoint protocol
    (`get_params` / `set_params` / `get_config`); see
    `docs/en/specs/local_ctrl_endpoint_protocol.md`.
  - `ChalRespCommandManager` - the challenge-response protocol (`ch_resp`).
- `local_ctrl_pb2.py` - generated from
  `components/esp_rmaker_neo/src/local_ctrl/local_ctrl.proto`
  (`protoc --python_out=. local_ctrl.proto`).
- `def_pb2.py` - generated from `components/esp_rmaker_neo/src/chal_resp/def.proto`.

## Prerequisites

- **Python dependencies**: `pip install protobuf` (see `tools/requirements.txt`)
