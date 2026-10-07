"""Decky adapter for the shared native Remote Play owner."""
from .base_service import BaseService
from .remote_play_core import RemotePlayOverride, MARKER
from .types import RemotePlayResponse, RuntimeContextState
from typing import Sequence


class RemotePlayService(RemotePlayOverride, BaseService):
    def __init__(self, logger=None):
        BaseService.__init__(self, logger)
        RemotePlayOverride.__init__(self, self.user_home, self.mako_script_path, self.log)

    def get_status(self, contexts: Sequence[RuntimeContextState] = ()) -> RemotePlayResponse:
        status = RemotePlayResponse(**super().get_status(contexts))
        if status['managed']:
            try:
                state = self._read_state(update_paths=False)
                if state['configuration_path'] != str(self.config_dir / 'conf.toml'):
                    status.update(conflict=True, frame_generation_active=False,
                                  message='Remote Play uses another configuration. Edit it in Qt; restore the override before enabling it from MAKO Decky.')
            except (OSError, ValueError):
                status.update(conflict=True, frame_generation_active=False,
                              message='Remote Play recovery state is unavailable. Restore the override before editing its profile.')
        return status
