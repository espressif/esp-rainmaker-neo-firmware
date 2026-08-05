# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Helpers for the bridge concurrent-activity stress test.

A cycle is a single ``run_cycle`` call that drives state, automation and
schedule provisioning across the *same* random subset of the child pool,
in one interleaved fan-out, then drains in bulk, then fires + verifies
each scenario. Same-subset reuse multiplies the per-child stress: every
selected child sees a state publish, a trigger install + fire, AND a
schedule install + fire in a single cycle.

Cycle shape:

  DRIVE (interleaved fan-out, no per-child waits):
    1. Subset: push ``Power=True`` (state drive).
    2. Subset: cloud automation create + update per child.
    3. Pin parent clock just before schedule trigger time.
    4. Subset: ``set_node_trigger`` per child.
    5. Subset: ``set_node_schedule`` per child.

  DRAIN:
    1. Wait on parent's ``state_reported`` + sleep so state publishes flush.
    2. Per-child ``wait_on_trigger_details``.
    3. Per-child ``wait_on_sched_details``.

  VERIFY:
    * State: each child's named shadow shows ``Power=True``.
    * Auto: reset ``Power=False`` per child (clears the state-drive
      residue so the rule's action is observable), fire ``Brightness=60``
      fan-out, assert ``Power=True`` per child.
    * Sched: reset ``Power=False`` per child (clears the auto-fire
      residue), single ``time_control_set_time`` past trigger, assert
      ``Power=True`` per child.

  CLEANUP: delete cloud automations + per-child triggers + per-child
  schedules (best-effort).
"""

import json
import random
from datetime import datetime as dt
from time import monotonic, sleep
from zoneinfo import ZoneInfo

from .automation import wire_actions, wire_triggers
from .bridge_host_ctrl import (
    assert_child_named_shadow_matches_config,
)


def stress_child_node_config() -> dict:
    """Minimal child node config. One ``light`` device with a bool
    ``Power`` + int ``Brightness``. Power is the state/schedule knob;
    Brightness is the automation trigger surface."""
    return {
        "devices": [
            {
                "id": "light",
                "type": "esp.device.light",
                "params": [
                    {
                        "id": "Power",
                        "type": "esp.param.power",
                        "data_type": "bool",
                        "value": False,
                        "properties": ["read", "write"],
                    },
                    {
                        "id": "Brightness",
                        "type": "esp.param.brightness",
                        "data_type": "int",
                        "value": 0,
                        "bounds": {"min": 0, "max": 100, "step": 1},
                        "properties": ["read", "write"],
                    },
                ],
            }
        ],
        "services": [],
        "tags": {},
    }


def select_subset(pool: list, frac: float, rng: random.Random, min_size: int) -> list:
    """Pick ``max(min_size, int(len(pool) * frac))`` random children from
    ``pool``. Same subset is used by all scenarios in the cycle."""
    size = min(len(pool), max(min_size, int(len(pool) * frac)))
    return rng.sample(pool, size)


# ---------- cloud-side scenario primitives ----------------------------------


def _create_child_automation_cloud(
    user, group_id, child, automation_name: str
) -> tuple[str, list[dict]]:
    """Cloud-only: create empty automation + update with full rule
    (``Brightness > 50 → Power = True``)."""
    thing_name = child.thing_name
    automation_id = user.create_automation(
        group_id=group_id,
        automation_data={
            "name": automation_name,
            "conditions": {"and": []},
            "actions": {"targets": []},
        },
    ).get("automation_id")
    assert automation_id is not None, (
        f"create_automation returned no id for {thing_name}"
    )

    w_triggers = wire_triggers(
        [{"device": "light", "param": "Brightness", "operator": "gt", "value": 50}],
        id_prefix=f"{thing_name}~{automation_id}",
    )
    actions = [{"node": thing_name, "device": "light", "param": "Power", "value": True}]
    assert user.update_automation(
        group_id=group_id,
        automation_id=automation_id,
        automation_data={
            "name": automation_name,
            "description": "Bridge stress automation",
            "conditions": {"and": [w_triggers[0]["id"]]},
            "actions": {"targets": wire_actions(actions)},
        },
    ), f"update_automation failed for {thing_name}"
    return automation_id, w_triggers


def _build_stress_schedule(minute_of_day: int, schedule_id: str) -> dict:
    """Cyclical schedule firing every day at ``minute_of_day``."""
    return {
        "name": schedule_id,
        "id": schedule_id,
        "triggers": [{"m": minute_of_day, "d": 0x7F}],
        "action": {"light": {"Power": True}},
    }


def _reset_params(
    parent_node_host_ctrl,
    subset: list,
    targets: dict,
    sleep_s: float = 0.5,
) -> None:
    """Fan-out the given ``{(device, param): value}`` targets on every
    subset child whose current value differs, then drain
    ``state_reported`` once + sleep. Skips no-op writes (firmware drops
    same-value publishes, which would also skip trigger evaluation —
    callers rely on that distinction to drive real transitions)."""
    parent_node_host_ctrl.clear_on_state_reported()
    pushed = False
    for child in subset:
        for (device_id, param_id), target_value in targets.items():
            current = child.get_param(device_id, param_id)
            assert current is not None, (
                f"reset_params: {device_id}::{param_id} not present on "
                f"{child.thing_name}"
            )
            same = (
                (current.value is target_value)
                if isinstance(target_value, bool)
                else (current.value == target_value)
            )
            if same:
                continue
            assert child.update_param(device_id, param_id, target_value), (
                f"reset_params: update_param failed on "
                f"{child.thing_name} {device_id}::{param_id}"
            )
            pushed = True
    if pushed:
        parent_node_host_ctrl.wait_on_state_reported(20000)
        sleep(sleep_s)


# Spacing between a prior cycle's trigger DELETE (cleanup) and this cycle's
# trigger ADD. The cloud sends only a bumped "version" (no "triggers" key)
# on delete; a race between that delete and a fresh add can make the new
# trigger fire against the stale pre-delete payload. A short settle window
# between the two avoids the race.
TRIGGER_DELETE_TO_ADD_SPACING_S = 3.0

# Pacing between consecutive per-child set_node_trigger calls.
#
# MITIGATION (not a fix): set_node_trigger writes the trigger row + bumps its
# version, then the backend immediately reads it back (non-consistent,
# split data/version GetItems) to push details to the device. Under load that
# read-after-write can return the new version with empty triggers, so the
# device commits an empty trigger list and the automation never fires. The
# real fix is backend-side (consistent/single-snapshot read or no read-back).
# Until then, pacing the fan-out cuts node_details table contention, which
# shrinks DynamoDB read-replica lag and makes the empty-read far less likely.
TRIGGER_ADD_PER_CHILD_DELAY_S = 0.1


# ---------- single concurrent cycle -----------------------------------------


def run_cycle(
    parent_node_host_ctrl,
    user,
    group_id,
    subset: list,
    cycle_index: int,
) -> None:
    """One concurrent stress cycle on ``subset``. Same subset drives
    state, automation and schedule scenarios. See module docstring."""
    timezone_str = parent_node_host_ctrl.get_current_timezone()
    assert timezone_str is not None, "stress cycle: no device timezone"
    zone_info = ZoneInfo(timezone_str)

    # Monotonic forward across cycles so a previous cycle's day-of-week
    # schedule doesn't pre-fire when we pin the new pre_time.
    day = 1 + cycle_index
    pre_time = dt(2026, 1, day, 7, 0, 0, tzinfo=zone_info)
    trigger_time = dt(2026, 1, day, 7, 30, 0, tzinfo=zone_info)
    schedule_id = f"stress_sched_c{cycle_index}"
    schedule = _build_stress_schedule(7 * 60 + 30, schedule_id)

    # Cloud-side artifacts for cleanup.
    installed_autos: list[tuple[str, object]] = []  # (automation_id, child)
    auto_triggers: list[tuple[object, list[dict]]] = []  # (child, w_triggers)
    sched_provisioned: list[object] = []

    try:
        # ============================ DRIVE ============================

        # 1. State drive: fan-out Power=True.
        parent_node_host_ctrl.clear_on_state_reported()
        for child in subset:
            assert child.update_param("light", "Power", True), (
                f"state drive: update_param failed on {child.thing_name}"
            )

        # 2. Auto cloud-only create + update per child.
        for i, child in enumerate(subset):
            automation_id, w_triggers = _create_child_automation_cloud(
                user, group_id, child, f"stress-auto-c{cycle_index}-{i}"
            )
            installed_autos.append((automation_id, child))
            auto_triggers.append((child, w_triggers))

        # 3. Pin clock just before schedule trigger.
        assert parent_node_host_ctrl.time_control_set_time(pre_time)

        # 4. set_node_trigger fan-out (clear flags first).
        # Settle gap so a prior cycle's trigger DELETE has fully landed
        # cloud-side before we ADD — avoids the new trigger firing the
        # stale pre-delete payload (delete only bumps version, no
        # "triggers" key, so the add can race the delete commit).
        if cycle_index > 0:
            sleep(TRIGGER_DELETE_TO_ADD_SPACING_S)
        for child, _ in auto_triggers:
            child.clear_on_trigger_details()
        for child, w_triggers in auto_triggers:
            assert user.set_node_trigger(
                group_id=group_id,
                node_id=child.thing_name,
                trigger_data=json.dumps({"triggers": w_triggers}),
            ), f"set_node_trigger failed for {child.thing_name}"
            # Pace the fan-out to reduce backend read-after-write contention
            # (see TRIGGER_ADD_PER_CHILD_DELAY_S).
            sleep(TRIGGER_ADD_PER_CHILD_DELAY_S)

        # 5. set_node_schedule fan-out (clear flags first).
        for child in subset:
            child.clear_on_sched_details()
        schedule_data = {
            "schedule": {
                "Schedules": [
                    {
                        "name": schedule["name"],
                        "id": schedule["id"],
                        "triggers": schedule["triggers"],
                        "action": schedule["action"],
                    }
                ]
            }
        }
        for child in subset:
            assert user.set_node_schedule(
                group_id=group_id,
                subgroup_id=None,
                node_id=child.thing_name,
                schedule_data=schedule_data,
            ), f"set_node_schedule failed for {child.thing_name}"
            sched_provisioned.append(child)

        # ============================ DRAIN ============================

        parent_node_host_ctrl.wait_on_state_reported(20000)
        sleep(1.0)

        for child, _ in auto_triggers:
            assert child.wait_on_trigger_details(30000), (
                f"trigger_details not received by {child.thing_name}"
            )

        for child in sched_provisioned:
            assert child.wait_on_sched_details(30000), (
                f"sched_details not received by {child.thing_name}"
            )

        # =========================== VERIFY ============================

        # State verify: each child's shadow shows Power=True.
        state_expected = {
            "devices": [{"id": "light", "params": [{"id": "Power", "value": True}]}]
        }
        for child in subset:
            assert_child_named_shadow_matches_config(
                user, child, group_id, state_expected
            )

        # Auto fire: reset Power=False so rule's action is observable
        # AND Brightness=0 so the next push to 60 is a real transition
        # (firmware skips same-value publishes — trigger evaluator only
        # sees actual edges).
        _reset_params(
            parent_node_host_ctrl,
            subset,
            {("light", "Power"): False, ("light", "Brightness"): 0},
        )
        parent_node_host_ctrl.clear_on_state_reported()
        parent_node_host_ctrl.clear_on_notification_sent()
        for child in subset:
            assert child.update_param("light", "Brightness", 60), (
                f"auto fire: update_param failed on {child.thing_name}"
            )
        parent_node_host_ctrl.wait_on_state_reported(20000)
        parent_node_host_ctrl.wait_on_notification_sent(20000)
        # Cloud-side automation latency dominates here. Under stress, some
        # children's action publishes lag the rest by tens of seconds, and
        # the earliest-installed automations can lose their first fire
        # entirely if cloud rule provisioning hasn't caught up. Poll-with-
        # retry per child rather than a single-shot read.
        auto_verify_deadline = monotonic() + 60.0
        pending = list(subset)
        while pending and monotonic() < auto_verify_deadline:
            still_pending = []
            for child in pending:
                if child.get_param("light", "Power").value is True:
                    continue
                still_pending.append(child)
            pending = still_pending
            if pending:
                sleep(1.0)
        if pending:
            raise AssertionError(
                "auto verify: Power did not flip on "
                + ", ".join(c.thing_name for c in pending)
            )

        # Sched fire: reset Power=False so schedule action is observable.
        _reset_params(parent_node_host_ctrl, subset, {("light", "Power"): False})
        parent_node_host_ctrl.clear_on_state_reported()
        assert parent_node_host_ctrl.time_control_set_time(trigger_time)
        parent_node_host_ctrl.wait_on_state_reported(20000)
        sleep(2.0)
        for child in subset:
            assert child.get_param("light", "Power").value is True, (
                f"schedule verify: Power did not fire on {child.thing_name}"
            )
    finally:
        # =========================== CLEANUP ===========================

        for automation_id, _child in installed_autos:
            try:
                user.delete_automation(group_id=group_id, automation_id=automation_id)
            except Exception:
                pass
        for child, _ in auto_triggers:
            try:
                child.clear_on_trigger_details()
            except Exception:
                pass
        for child, _ in auto_triggers:
            try:
                user.delete_node_trigger(group_id=group_id, node_id=child.thing_name)
            except Exception:
                pass
        for child, _ in auto_triggers:
            try:
                child.wait_on_trigger_details(10000)
            except Exception:
                pass

        empty_sched_data = {"schedule": {"Schedules": []}}
        for child in sched_provisioned:
            try:
                child.clear_on_sched_details()
                user.set_node_schedule(
                    group_id=group_id,
                    subgroup_id=None,
                    node_id=child.thing_name,
                    schedule_data=empty_sched_data,
                )
                child.wait_on_sched_details(10000)
            except Exception:
                pass
