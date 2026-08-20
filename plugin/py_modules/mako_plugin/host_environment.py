"""Native-host detection shared by Renderer and Flatpak safety boundaries."""

from dataclasses import dataclass
import platform
from pathlib import Path
from typing import Any, Optional

from .constants import ARMADA_DEVICE_ENV


@dataclass(frozen=True)
class HostEnvironment:
    """Describe the plugin process and the native host beneath translation."""

    process_architecture: str
    native_architecture: str
    armada: bool

    @property
    def translated(self) -> bool:
        return self.process_architecture != self.native_architecture


def normalize_host_architecture(machine: str) -> str:
    """Return the canonical architecture spelling used by package metadata."""
    normalized = machine.strip().lower()
    if normalized in {"amd64", "x86_64"}:
        return "x86_64"
    if normalized in {"arm64", "aarch64"}:
        return "aarch64"
    return normalized or "unknown"


def _elf_host_architecture(executable: Path) -> Optional[str]:
    """Read only the ELF identity needed to identify a native host process."""
    with executable.open("rb") as host_executable:
        elf_header = host_executable.read(20)
    if elf_header[:4] != b"\x7fELF" or elf_header[5] not in (1, 2):
        return None
    byte_order = "little" if elf_header[5] == 1 else "big"
    return {
        62: "x86_64",
        183: "aarch64",
    }.get(int.from_bytes(elf_header[18:20], byte_order))


def detect_host_environment(
        logger: Optional[Any] = None,
        *,
        process_machine: Optional[str] = None,
        armada_marker: Optional[Path] = None,
        native_process: Optional[Path] = None,
) -> HostEnvironment:
    """Detect the native ISA even when Decky's Python process runs in FEX.

    Armada's root-owned device marker is the authoritative platform signal.
    PID 1's ELF identity remains a generic translated-host fallback, while an
    ordinary native process uses ``platform.machine()``.
    """
    process_architecture = normalize_host_architecture(
        process_machine if process_machine is not None else platform.machine()
    )
    marker = ARMADA_DEVICE_ENV if armada_marker is None else armada_marker
    host_process = Path("/proc/1/exe") if native_process is None else native_process

    if marker.is_file():
        if logger:
            logger.info("Detected native AArch64 Armada host through device-env")
        return HostEnvironment(
            process_architecture=process_architecture,
            native_architecture="aarch64",
            armada=True,
        )

    native_architecture = process_architecture
    try:
        detected = _elf_host_architecture(host_process)
        if detected:
            native_architecture = detected
            if logger and detected != process_architecture:
                logger.info(
                    "Detected native %s host through PID 1 (plugin process: %s)",
                    detected,
                    process_architecture,
                )
    except OSError as error:
        if logger:
            logger.debug("Could not inspect native host architecture: %s", error)

    return HostEnvironment(
        process_architecture=process_architecture,
        native_architecture=native_architecture,
        armada=False,
    )
