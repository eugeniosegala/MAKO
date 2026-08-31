"""Read MAKO Renderer's atomic requested-versus-applied runtime records."""

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
from typing import Any, Optional, cast

from .base_service import BaseService
from .types import (
    RuntimeApplicationPhase,
    RuntimeContextState,
    RuntimePendingState,
    RuntimeProfileSnapshot,
    RuntimeScalingMethod,
    RuntimeScalingPipeline,
    RuntimeSpatialScalingState,
    RuntimeStatusResponse,
)


_SCHEMA_VERSION = 4
_MAXIMUM_STATUS_BYTES = 64 * 1024
_VALID_ROLES = frozenset(("frame-generation", "spatial-scaling"))
_VALID_SCALING_METHODS = frozenset((
    "native", "mako", "ls1", "ls1-performance",
))
_VALID_SPATIAL_PIPELINES = frozenset((
    "inactive", "pre-frame-generation", "post-frame-generation",
))
_VALID_PHASES = frozenset((
    "active",
    "debouncing",
    "preparing",
    "draining",
    "failed",
    "swapchain-recreation",
    "process-restart",
))
_PHASE_PRIORITY = {
    "inactive": 0,
    "active": 1,
    "debouncing": 2,
    "preparing": 3,
    "draining": 4,
    "swapchain-recreation": 5,
    "process-restart": 6,
    "failed": 7,
}


def _integer(value: object, field: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ValueError(f"{field} must be a non-negative integer")
    return value


def _number(value: object, field: str) -> float:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise ValueError(f"{field} must be numeric")
    return float(value)


def _boolean(value: object, field: str) -> bool:
    if not isinstance(value, bool):
        raise ValueError(f"{field} must be boolean")
    return value


def _string(value: object, field: str) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{field} must be a string")
    return value


def _profile(value: object, field: str) -> RuntimeProfileSnapshot:
    if not isinstance(value, dict):
        raise ValueError(f"{field} must be an object")
    gpu_value = value.get("gpu")
    if gpu_value is not None and not isinstance(gpu_value, str):
        raise ValueError(f"{field}.gpu must be a string or null")
    return {
        "name": _string(value.get("name"), f"{field}.name"),
        "gpu": gpu_value,
        "multiplier": _integer(value.get("multiplier"), f"{field}.multiplier"),
        "frame_generation_enabled": _boolean(
            value.get("frame_generation_enabled"),
            f"{field}.frame_generation_enabled",
        ),
        "scaling_enabled": _boolean(
            value.get("scaling_enabled"), f"{field}.scaling_enabled"
        ),
        "scaling_method": _string(
            value.get("scaling_method"), f"{field}.scaling_method"
        ),
        "scaling_factor": _number(
            value.get("scaling_factor"), f"{field}.scaling_factor"
        ),
        "scaling_supersampling": _boolean(
            value.get("scaling_supersampling"),
            f"{field}.scaling_supersampling",
        ),
        "scaling_sharpness": _number(
            value.get("scaling_sharpness"), f"{field}.scaling_sharpness"
        ),
        "frame_generation_refresh_threshold": _integer(
            value.get("frame_generation_refresh_threshold"),
            f"{field}.frame_generation_refresh_threshold",
        ),
        "base_fps_cap": _integer(
            value.get("base_fps_cap"), f"{field}.base_fps_cap"
        ),
        "adaptive": _boolean(value.get("adaptive"), f"{field}.adaptive"),
        "adaptive_auto_base_fps_cap": _boolean(
            value.get("adaptive_auto_base_fps_cap"),
            f"{field}.adaptive_auto_base_fps_cap",
        ),
        "target_fps": _integer(value.get("target_fps"), f"{field}.target_fps"),
        "adaptive_max_multiplier": _integer(
            value.get("adaptive_max_multiplier"),
            f"{field}.adaptive_max_multiplier",
        ),
        "adaptive_stable_cadence": _boolean(
            value.get("adaptive_stable_cadence"),
            f"{field}.adaptive_stable_cadence",
        ),
        "dynamic_cadence_recovery": _boolean(
            value.get("dynamic_cadence_recovery"),
            f"{field}.dynamic_cadence_recovery",
        ),
        "dynamic_cadence_probe_interval_seconds": _number(
            value.get("dynamic_cadence_probe_interval_seconds"),
            f"{field}.dynamic_cadence_probe_interval_seconds",
        ),
        "ultra_performance": _boolean(
            value.get("ultra_performance"), f"{field}.ultra_performance"
        ),
        "flow_scale": _number(value.get("flow_scale"), f"{field}.flow_scale"),
        "effective_flow_scale": _number(
            value.get("effective_flow_scale"), f"{field}.effective_flow_scale"
        ),
        "performance_mode": _boolean(
            value.get("performance_mode"), f"{field}.performance_mode"
        ),
        "effective_performance_mode": _boolean(
            value.get("effective_performance_mode"),
            f"{field}.effective_performance_mode",
        ),
        "pacing": _string(value.get("pacing"), f"{field}.pacing"),
        "required_generated_capacity": _integer(
            value.get("required_generated_capacity"),
            f"{field}.required_generated_capacity",
        ),
    }


def _pending(value: object) -> RuntimePendingState:
    if not isinstance(value, dict):
        raise ValueError("pending must be an object")
    return {
        "frame_generation_private": _boolean(
            value.get("frame_generation_private"),
            "pending.frame_generation_private",
        ),
        "spatial_private": _boolean(
            value.get("spatial_private"), "pending.spatial_private"
        ),
        "swapchain_recreation": _boolean(
            value.get("swapchain_recreation"),
            "pending.swapchain_recreation",
        ),
        "process_restart": _boolean(
            value.get("process_restart"), "pending.process_restart"
        ),
    }


def _spatial_scaling(value: object) -> RuntimeSpatialScalingState:
    if not isinstance(value, dict):
        raise ValueError("spatial_scaling must be an object")
    inactive_reason = value.get("inactive_reason")
    if inactive_reason is not None and not isinstance(inactive_reason, str):
        raise ValueError("spatial_scaling.inactive_reason must be a string or null")
    fallback_reason = value.get("fallback_reason")
    if fallback_reason is not None and not isinstance(fallback_reason, str):
        raise ValueError("spatial_scaling.fallback_reason must be a string or null")
    ceiling = value.get("non_supersampling_factor_ceiling")
    if ceiling is not None:
        ceiling = _number(
            ceiling, "spatial_scaling.non_supersampling_factor_ceiling"
        )
    requested_method = _string(
        value.get("requested_method"),
        "spatial_scaling.requested_method",
    )
    active_method = _string(
        value.get("active_method"), "spatial_scaling.active_method"
    )
    pipeline = _string(value.get("pipeline"), "spatial_scaling.pipeline")
    if requested_method not in _VALID_SCALING_METHODS:
        raise ValueError("spatial_scaling.requested_method is invalid")
    if active_method not in _VALID_SCALING_METHODS:
        raise ValueError("spatial_scaling.active_method is invalid")
    if pipeline not in _VALID_SPATIAL_PIPELINES:
        raise ValueError("spatial_scaling.pipeline is invalid")
    validated_requested_method = cast(RuntimeScalingMethod, requested_method)
    validated_active_method = cast(RuntimeScalingMethod, active_method)
    validated_pipeline = cast(RuntimeScalingPipeline, pipeline)
    return {
        "active": _boolean(value.get("active"), "spatial_scaling.active"),
        "activation_supported": _boolean(
            value.get("activation_supported"),
            "spatial_scaling.activation_supported",
        ),
        "inactive_reason": inactive_reason,
        "source_width": _integer(
            value.get("source_width"), "spatial_scaling.source_width"
        ),
        "source_height": _integer(
            value.get("source_height"), "spatial_scaling.source_height"
        ),
        "presentation_width": _integer(
            value.get("presentation_width"),
            "spatial_scaling.presentation_width",
        ),
        "presentation_height": _integer(
            value.get("presentation_height"),
            "spatial_scaling.presentation_height",
        ),
        "gamescope_target_width": _integer(
            value.get("gamescope_target_width"),
            "spatial_scaling.gamescope_target_width",
        ),
        "gamescope_target_height": _integer(
            value.get("gamescope_target_height"),
            "spatial_scaling.gamescope_target_height",
        ),
        "requested_method": validated_requested_method,
        "active_method": validated_active_method,
        "effective_factor": _number(
            value.get("effective_factor"),
            "spatial_scaling.effective_factor",
        ),
        "pipeline": validated_pipeline,
        "supersampling_active": _boolean(
            value.get("supersampling_active"),
            "spatial_scaling.supersampling_active",
        ),
        "fallback_reason": fallback_reason,
        "non_supersampling_factor_ceiling": ceiling,
    }


def _context(value: object) -> RuntimeContextState:
    if not isinstance(value, dict):
        raise ValueError("runtime record must be an object")
    if value.get("schema_version") != _SCHEMA_VERSION:
        raise ValueError("unsupported runtime status schema")
    role = _string(value.get("role"), "role")
    if role not in _VALID_ROLES:
        raise ValueError("unsupported runtime role")
    phase_value = _string(value.get("phase"), "phase")
    if phase_value not in _VALID_PHASES:
        raise ValueError("unsupported runtime phase")
    error_value = value.get("error")
    if error_value is not None and not isinstance(error_value, str):
        raise ValueError("error must be a string or null")
    return {
        "pid": _integer(value.get("pid"), "pid"),
        "process_start_ticks": _integer(
            value.get("process_start_ticks"), "process_start_ticks"
        ),
        "context": _integer(value.get("context"), "context"),
        "role": role,
        "updated_unix_ms": _integer(
            value.get("updated_unix_ms"), "updated_unix_ms"
        ),
        "state_revision": _integer(
            value.get("state_revision"), "state_revision"
        ),
        "phase": cast(RuntimeApplicationPhase, phase_value),
        "reason": _string(value.get("reason"), "reason"),
        "pending": _pending(value.get("pending")),
        "applied_generated_capacity": _integer(
            value.get("applied_generated_capacity"),
            "applied_generated_capacity",
        ),
        "frame_generation_active": _boolean(
            value.get("frame_generation_active"),
            "frame_generation_active",
        ),
        "spatial_scaling": _spatial_scaling(value.get("spatial_scaling")),
        "requested": _profile(value.get("requested"), "requested"),
        "applied": _profile(value.get("applied"), "applied"),
        "error": error_value,
    }


class RuntimeStateService(BaseService):
    """Validate and aggregate active Renderer context state."""

    def _process_start_ticks(self, process_id: int) -> Optional[int]:
        try:
            line = (Path("/proc") / str(process_id) / "stat").read_text(
                encoding="utf-8"
            )
            command_end = line.rfind(")")
            if command_end < 0:
                return None
            fields = line[command_end + 2:].split()
            # The remaining list begins at proc field 3; starttime is field 22.
            if len(fields) < 20:
                return None
            return int(fields[19])
        except (OSError, UnicodeError, ValueError):
            return None

    def _read_file_without_following_links(self, path: Path) -> bytes:
        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        try:
            file_status = os.fstat(descriptor)
            if not stat.S_ISREG(file_status.st_mode):
                raise ValueError("runtime status is not a regular file")
            if file_status.st_size > _MAXIMUM_STATUS_BYTES:
                raise ValueError("runtime status exceeds the size limit")
            payload = os.read(descriptor, _MAXIMUM_STATUS_BYTES + 1)
            if len(payload) > _MAXIMUM_STATUS_BYTES:
                raise ValueError("runtime status exceeds the size limit")
            return payload
        finally:
            os.close(descriptor)

    def _read_context(self, path: Path) -> Optional[RuntimeContextState]:
        try:
            raw: Any = json.loads(
                self._read_file_without_following_links(path).decode("utf-8")
            )
            context = _context(raw)
            if self._process_start_ticks(context["pid"]) != (
                context["process_start_ticks"]
            ):
                try:
                    path.unlink()
                except OSError:
                    pass
                return None
            return context
        except (OSError, UnicodeError, json.JSONDecodeError, ValueError):
            return None

    def get_status(self, profile_name: str = "") -> RuntimeStatusResponse:
        contexts: list[RuntimeContextState] = []
        try:
            if self.runtime_state_dir.is_dir():
                for path in self.runtime_state_dir.glob("*.json"):
                    context = self._read_context(path)
                    if context is None:
                        continue
                    if profile_name and profile_name not in {
                        context["requested"]["name"],
                        context["applied"]["name"],
                    }:
                        continue
                    contexts.append(context)
            contexts.sort(
                key=lambda context: (
                    context["updated_unix_ms"],
                    context["state_revision"],
                    context["context"],
                ),
                reverse=True,
            )
            aggregate: RuntimeApplicationPhase = "inactive"
            for context in contexts:
                if _PHASE_PRIORITY[context["phase"]] > _PHASE_PRIORITY[aggregate]:
                    aggregate = context["phase"]
            return {
                "success": True,
                "phase": aggregate,
                "contexts": contexts,
                "message": (
                    "No active MAKO Renderer context"
                    if not contexts
                    else f"{len(contexts)} active MAKO Renderer context(s)"
                ),
                "error": None,
            }
        except OSError as error:
            self.log.warning("Could not read MAKO Renderer runtime state: %s", error)
            return {
                "success": False,
                "phase": "inactive",
                "contexts": [],
                "message": "Runtime state is unavailable",
                "error": str(error),
            }
