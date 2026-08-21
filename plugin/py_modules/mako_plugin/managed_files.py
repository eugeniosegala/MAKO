"""Atomic replacement helpers for files generated and fully owned by MAKO."""

import os
from pathlib import Path
import secrets
import shutil
from typing import Any


def _create_staged_file(
        destination: Path, mode: int, logger: Any) -> tuple[int, Path]:
    destination.parent.mkdir(parents=True, exist_ok=True)
    open_flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_CLOEXEC"):
        open_flags |= os.O_CLOEXEC

    for _attempt in range(32):
        temporary_path = destination.parent / (
            f".{destination.name}.{secrets.token_hex(8)}"
        )
        try:
            file_descriptor = os.open(temporary_path, open_flags, mode)
        except FileExistsError:
            continue

        actual_mode = os.fstat(file_descriptor).st_mode & 0o777
        required_owner_mode = mode & 0o700
        if actual_mode & required_owner_mode == required_owner_mode:
            if actual_mode != mode:
                logger.debug(
                    "Host umask adjusted staging mode for %s from %03o to %03o",
                    destination,
                    mode,
                    actual_mode,
                )
            return file_descriptor, temporary_path

        try:
            os.fchmod(file_descriptor, mode)
            return file_descriptor, temporary_path
        except OSError as error:
            os.close(file_descriptor)
            temporary_path.unlink(missing_ok=True)
            raise OSError(
                "The host removed required owner permissions while creating "
                f"{destination}, and could not restore them: {error}"
            ) from error

    raise OSError(f"Could not allocate a staging file beside {destination}")


def write_managed_text_atomically(
        destination: Path, content: str, mode: int, logger: Any) -> None:
    """Atomically replace a generated MAKO text file."""
    file_descriptor, temporary_path = _create_staged_file(
        destination,
        mode,
        logger,
    )
    try:
        output = os.fdopen(file_descriptor, "w", encoding="utf-8")
        file_descriptor = -1
        with output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())

        temporary_path.replace(destination)
        logger.info("Wrote generated MAKO file to %s", destination)
    except OSError as error:
        logger.error("Failed to atomically replace %s: %s", destination, error)
        raise OSError(
            f"Could not atomically replace MAKO-managed file {destination}: {error}"
        ) from error
    finally:
        if file_descriptor >= 0:
            os.close(file_descriptor)
        temporary_path.unlink(missing_ok=True)


def copy_managed_file_atomically(
        source: Path, destination: Path, mode: int, logger: Any) -> None:
    """Atomically replace a generated MAKO file with copied bytes."""
    file_descriptor, temporary_path = _create_staged_file(
        destination,
        mode,
        logger,
    )
    try:
        with source.open("rb") as input_file:
            output = os.fdopen(file_descriptor, "wb")
            file_descriptor = -1
            with output:
                shutil.copyfileobj(input_file, output)
                output.flush()
                os.fsync(output.fileno())

        temporary_path.replace(destination)
        logger.info("Copied managed MAKO file %s to %s", source, destination)
    except OSError as error:
        logger.error(
            "Failed to atomically copy %s to %s: %s",
            source,
            destination,
            error,
        )
        raise OSError(
            f"Could not atomically replace MAKO-managed file {destination}: {error}"
        ) from error
    finally:
        if file_descriptor >= 0:
            os.close(file_descriptor)
        temporary_path.unlink(missing_ok=True)
