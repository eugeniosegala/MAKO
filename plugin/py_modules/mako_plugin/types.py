"""
Type definitions for MAKO Decky responses.
"""

from typing import Literal, TypedDict, Optional, List
from .config_schema import ConfigurationData


class BaseResponse(TypedDict):
    """Base response structure"""
    success: bool


class ErrorResponse(BaseResponse):
    """Response structure for errors"""
    error: str


class MessageResponse(BaseResponse):
    """Response structure with message"""
    message: str


class ServiceResponse(BaseResponse):
    """Response emitted by :class:`BaseService` response builders."""

    message: str
    error: Optional[str]


class InstallationResponse(ServiceResponse):
    """Response for installation operations"""
    pass


class UninstallationResponse(ServiceResponse):
    """Response for uninstallation operations"""
    removed_files: Optional[List[str]]


class InstallationResult(ServiceResponse, total=False):
    """Unified public install/uninstall RPC payload."""

    removed_files: Optional[List[str]]
    flatpak_extensions_updated: List[str]
    flatpak_refresh_error: str


class InstallationCheckResponse(TypedDict):
    """Response for installation check"""
    installed: bool
    lib_exists: bool
    json_exists: bool
    script_exists: bool
    lib_path: str
    json_path: str
    script_path: str
    installed_engine_version: Optional[str]
    expected_engine_version: Optional[str]
    engine_version_known: bool
    engine_update_required: bool
    host_architecture: Optional[str]
    host_architecture_supported: bool
    error: Optional[str]


class DllDetectionResponse(TypedDict):
    """Response for DLL detection"""
    detected: bool
    path: Optional[str]
    source: Optional[str]
    message: Optional[str]
    error: Optional[str]


class DllStatsRequiredResponse(BaseResponse):
    """Fields emitted on every DLL statistics path."""

    dll_path: Optional[str]
    dll_sha256: Optional[str]
    error: Optional[str]


class DllStatsResponse(DllStatsRequiredResponse, total=False):
    """Public hash and source details for the detected DLL."""

    dll_source: Optional[str]


class ConfigurationResponse(ServiceResponse):
    """Response for configuration operations"""
    config: Optional[ConfigurationData]


class ConfigSchemaResponse(TypedDict):
    """Generated configuration metadata exposed to the frontend."""

    field_names: List[str]
    field_types: dict[str, str]
    defaults: ConfigurationData
    profiles: List[str]
    current_profile: str


class LaunchOptionResponse(TypedDict):
    """Resolved wrapper command and its explanatory copy."""

    launch_option: str
    wrapper_path: str
    instructions: str
    explanation: str


class FileContentResponse(BaseResponse, total=False):
    """Optional content returned for a managed text file."""

    content: Optional[str]
    path: str
    error: Optional[str]


class FgmodCheckRequiredResponse(BaseResponse):
    """Fields emitted on every DeckyFG compatibility-directory check."""

    exists: bool


class FgmodCheckResponse(FgmodCheckRequiredResponse, total=False):
    """Presence of the user's DeckyFG compatibility directory."""

    path: str
    error: Optional[str]


class ProfileDetails(TypedDict):
    """Public identity and process matching for one profile."""

    profile_name: str
    display_name: str
    kind: str
    steam_app_id: Optional[str]
    processes: List[str]


class ProfilesResponse(ServiceResponse):
    """Response for profile operations"""
    profiles: Optional[List[str]]
    current_profile: Optional[str]
    profile_details: Optional[List[ProfileDetails]]


class ProfileResponse(ServiceResponse, total=False):
    """Response for single profile operations"""
    profile_name: Optional[str]
    current_profile: Optional[str]
    profile: Optional[ProfileDetails]
    changed: Optional[bool]
    game_running: Optional[bool]


RuntimeApplicationPhase = Literal[
    "inactive",
    "active",
    "debouncing",
    "preparing",
    "draining",
    "failed",
    "swapchain-recreation",
    "process-restart",
]


class RuntimeProfileSnapshot(TypedDict):
    """Renderer-owned requested or applied profile state."""

    name: str
    gpu: Optional[str]
    multiplier: int
    frame_generation_enabled: bool
    scaling_enabled: bool
    scaling_method: str
    scaling_factor: float
    scaling_sharpness: float
    frame_generation_refresh_threshold: int
    base_fps_cap: int
    adaptive: bool
    adaptive_auto_base_fps_cap: bool
    target_fps: int
    adaptive_max_multiplier: int
    adaptive_stable_cadence: bool
    dynamic_cadence_recovery: bool
    dynamic_cadence_probe_interval_seconds: float
    ultra_performance: bool
    flow_scale: float
    effective_flow_scale: float
    performance_mode: bool
    effective_performance_mode: bool
    pacing: str
    required_generated_capacity: int


class RuntimePendingState(TypedDict):
    """Outstanding runtime boundaries for one Renderer context."""

    frame_generation_private: bool
    spatial_private: bool
    swapchain_recreation: bool
    process_restart: bool


class RuntimeContextState(TypedDict):
    """One validated active Renderer context status record."""

    pid: int
    process_start_ticks: int
    context: int
    role: str
    updated_unix_ms: int
    state_revision: int
    phase: RuntimeApplicationPhase
    reason: str
    pending: RuntimePendingState
    applied_generated_capacity: int
    requested: RuntimeProfileSnapshot
    applied: RuntimeProfileSnapshot
    error: Optional[str]


class RuntimeStatusResponse(ServiceResponse):
    """Aggregate requested-versus-applied state for active contexts."""

    phase: RuntimeApplicationPhase
    contexts: List[RuntimeContextState]
