# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Shared timeseries-test helpers — node config, param specs, random value
in bounds, raw-data verification via the user REST API.
"""

import random
from time import sleep


def get_timeseries_node_config():
    """
    Standard timeseries-only node config: a humidity int param
    (``time_series``) and an energy float param (``ts_cumulative``).
    """
    return {
        "devices": [
            {
                "id": "sensor",
                "type": "esp.device.sensor",
                "params": [
                    {
                        "id": "humidity",
                        "type": "esp.param.humidity",
                        "data_type": "int",
                        "value": 65,
                        "bounds": {"min": 0, "max": 100, "step": 1},
                        "properties": ["read", "write", "time_series"],
                    }
                ],
            },
            {
                "id": "meter",
                "type": "esp.device.meter",
                "params": [
                    {
                        "id": "energy",
                        "type": "esp.param.energy",
                        "data_type": "float",
                        "value": 0.0,
                        "bounds": {"min": 0.0, "max": 1000.0, "step": 0.1},
                        "properties": ["read", "write", "ts_cumulative"],
                    }
                ],
            },
        ],
        "services": [],
        "tags": {},
    }


def timeseries_param_specs(node_config):
    """Extract (device, param_id, key, data_type, cumulative, bounds) per param."""
    specs = []
    for device in node_config.get("devices", []):
        device_id = device["id"]
        for param in device.get("params", []):
            pid = param["id"]
            data_type = param["data_type"]
            bounds = param.get("bounds", {})
            cumulative = "ts_cumulative" in param.get("properties", [])
            specs.append(
                {
                    "device": device_id,
                    "param": pid,
                    "key": f"{device_id}.{pid}",
                    "data_type": data_type,
                    "cumulative": cumulative,
                    "bounds": bounds,
                }
            )
    return specs


def timeseries_random_value_in_bounds(bounds, data_type, exclude_value):
    """Return a random value within bounds that is not equal to exclude_value."""
    min_v = bounds.get("min")
    max_v = bounds.get("max")
    step = bounds.get("step")
    if min_v is None or max_v is None:
        raise ValueError("bounds must have min and max")
    if data_type == "int":
        min_v, max_v = int(min_v), int(max_v)
        step = 1 if step is None else int(step)
        candidates = [x for x in range(min_v, max_v + 1, step) if x != exclude_value]
        if not candidates:
            raise ValueError(
                f"No value in bounds {bounds} different from exclude_value {exclude_value}"
            )
        return random.choice(candidates)
    else:
        min_v, max_v = float(min_v), float(max_v)
        step_float = float(step) if step is not None else 1e-9
        only_value_equals_exclude = min_v == max_v and (
            min_v == exclude_value
            or (data_type == "float" and abs(min_v - exclude_value) < step_float)
        )
        if only_value_equals_exclude:
            raise ValueError(
                f"No value in bounds {bounds} different from exclude_value {exclude_value}"
            )

        while True:
            v = random.uniform(min_v, max_v)
            v = round((v - min_v) / step_float) * step_float + min_v
            v = round(v, 10)
            v = max(min_v, min(max_v, v))
            same_as_exclude = (
                (v == exclude_value)
                if data_type != "float"
                else (abs(v - exclude_value) < step_float)
            )
            if not same_as_exclude:
                return v


def timeseries_verify_with_user_api(
    user,
    group_id,
    thing_name,
    expected_points_by_param,
    start_time,
    end_time,
    param_specs,
    precision=1e-4,
    ts_tolerance_ms=250,
):
    """
    Verify all timeseries data in the time period via get_node_timeseries_raw.
    Retries with backoff to absorb cloud stream-processor propagation delay.

    ``ts_tolerance_ms`` is the allowed skew between an expected timestamp and
    the one the cloud stored. The host sets a whole-second time on the node,
    but the node stamps each point from its own clock when it publishes, so
    any delay between the two shifts the point by tens of milliseconds.
    Comparing exactly makes that a permanent failure for the run — the retry
    loop only re-queries the cloud, it cannot re-publish.

    The default keeps a 5x margin over the ~50 ms skew actually observed while
    staying well inside the 2 s spacing both callers use: at half the spacing
    adjacent tolerance windows would touch, leaving a point that lands midway
    between two expected ones matchable against either.
    """

    # Everything is in milliseconds — start/end, the cloud `ts`, and the
    # expected points. Widen the query window past the tolerance so a skewed
    # first/last point still lands inside it.
    window_pad_ms = ts_tolerance_ms + 1000
    start_time_ms = start_time - window_pad_ms
    end_time_ms = end_time + window_pad_ms

    def validate_raw_timeseries_data():
        for spec in param_specs:
            key = spec["key"]
            data_type = spec["data_type"]
            expected_list = expected_points_by_param.get(key, [])
            assert expected_list, f"Expected at least one point for {key}"

            response = user.get_node_timeseries_raw(
                group_id=group_id,
                node_id=thing_name,
                key=key,
                data_type=data_type,
                start_time=start_time_ms,
                end_time=end_time_ms,
                page_size=max(len(expected_list) * 4, 100),
            )
            assert response is not None, f"Should receive response for {key}"
            assert "data" in response, f"Response should contain data for {key}"
            data_entries = [
                entry
                for entry in response["data"]
                if entry["ts"] >= start_time_ms and entry["ts"] <= end_time_ms
            ]
            assert len(data_entries) >= len(expected_list), (
                f"Raw data for {key} should have at least {len(expected_list)} entries in period, got {len(data_entries)}"
            )

            raw_list = [
                (
                    int(entry.get("ts", entry.get("t", 0))),
                    entry.get("value", entry.get("v")),
                )
                for entry in data_entries
            ]
            sorted_expected = sorted(expected_list, key=lambda p: p[0])
            sorted_raw = sorted(raw_list, key=lambda p: p[0])

            j = 0
            for exp_ts, exp_val in sorted_expected:
                while j < len(sorted_raw) and (
                    sorted_raw[j][0] < exp_ts - ts_tolerance_ms
                    or (
                        sorted_raw[j][0] <= exp_ts + ts_tolerance_ms
                        and abs(sorted_raw[j][1] - exp_val) >= precision
                    )
                ):
                    j += 1
                assert j < len(sorted_raw), (
                    f"Expected point not found for {key}: ts={exp_ts}, value={exp_val}"
                )
                raw_ts, raw_val = sorted_raw[j]
                assert abs(raw_ts - exp_ts) <= ts_tolerance_ms, (
                    f"Expected point not found for {key}: ts={exp_ts}, value={exp_val} "
                    f"(next raw ts={raw_ts}, off by {raw_ts - exp_ts} ms, tolerance {ts_tolerance_ms} ms)"
                )
                assert abs(exp_val - raw_val) < precision, (
                    f"Value mismatch for {key} at ts={exp_ts}: expected {exp_val}, got {raw_val}"
                )
                j += 1
            print(f"OK Validated {key}: all {len(expected_list)} points in period")

    def retry_validation(validation_func, max_retries=10, delay=3):
        for attempt in range(max_retries):
            try:
                validation_func()
                return True
            except Exception as e:
                if attempt < max_retries - 1:
                    print(f"Validation attempt {attempt + 1} failed: {str(e)}")
                    print(f"   Retrying in {delay} seconds...")
                    sleep(delay)
                    delay = min(delay * 1.5, 10)
                else:
                    raise e
        return False

    print("Executing raw timeseries validation with retry logic...")
    assert retry_validation(validate_raw_timeseries_data), (
        "Raw timeseries validation failed after retries"
    )
