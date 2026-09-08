"""Atomic replacement helpers for files generated and fully owned by MAKO."""

from contextlib import contextmanager
import os
from pathlib import Path
import secrets
import shutil
import stat
import tempfile
from collections.abc import Iterator
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
        destination: Path, content: str, mode: int, logger: Any) -> bool:
    """Atomically replace a generated MAKO text file only when needed."""
    try:
        file_mode = destination.lstat().st_mode
        actual_mode = stat.S_IMODE(file_mode)
        if (
                stat.S_ISREG(file_mode)
                and actual_mode & 0o700 == mode & 0o700
                and actual_mode & ~mode == 0
                and destination.read_text(encoding="utf-8") == content
        ):
            logger.debug("Generated MAKO file already current: %s", destination)
            return False
    except (OSError, UnicodeError):
        pass

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
        return True
    except OSError as error:
        logger.error("Failed to atomically replace %s: %s", destination, error)
        raise OSError(
            f"Could not atomically replace MAKO-managed file {destination}: {error}"
        ) from error
    finally:
        if file_descriptor >= 0:
            os.close(file_descriptor)
        temporary_path.unlink(missing_ok=True)


@contextmanager
def managed_install_transaction(paths: list[Path], logger: Any) -> Iterator[None]:
    """Restore the selected installation if a later install step fails.

    Callers must atomically replace these files, never modify their inodes.
    Adjacent hard-link backups preserve modes and symlinks without duplicating
    libraries or depending on /tmp's filesystem. Unsupported hard links fall
    back to a copy before any installation writes begin.
    """
    backups: list[tuple[Path, Path | None]] = []
    directories: list[Path] = []
    retained: set[Path] = set()
    try:
        for path in dict.fromkeys(paths):
            try:
                file_mode = path.lstat().st_mode
            except FileNotFoundError:
                backups.append((path, None))
                continue
            if not (stat.S_ISREG(file_mode) or stat.S_ISLNK(file_mode)):
                raise OSError(f"MAKO installation destination is not a file: {path}")
            directory = Path(tempfile.mkdtemp(prefix=".mako-rollback-", dir=path.parent))
            directories.append(directory)
            backup = directory / path.name
            if stat.S_ISLNK(file_mode):
                backup.symlink_to(os.readlink(path))
            else:
                try:
                    os.link(path, backup)
                except OSError:
                    shutil.copy2(path, backup)
            backups.append((path, backup))
        try:
            yield
        except BaseException as error:
            failures = []
            for path, backup in reversed(backups):
                try:
                    if backup is None:
                        path.unlink(missing_ok=True)
                    else:
                        backup.replace(path)
                except OSError as restore_error:
                    if backup is not None:
                        retained.add(backup.parent)
                    failures.append(f"{path}: {restore_error}")
            if failures:
                raise OSError(
                    f"{error}; could not fully restore the previous MAKO installation: "
                    + "; ".join(failures)
                    + f". Recovery backups retained at: {sorted(map(str, retained))}"
                ) from error
            logger.warning("Restored the previous MAKO installation after failure: %s", error)
            raise
    finally:
        for directory in directories:
            if directory not in retained:
                try:
                    shutil.rmtree(directory)
                except OSError as error:
                    logger.warning("Could not remove installation backup %s: %s", directory, error)


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
