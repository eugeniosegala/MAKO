"""Import-safe paths for the installed MAKO Decky package."""

from pathlib import Path


# This module must remain independent from shared_config: package import uses
# it to expose the plugin root before any generated/shared contract is loaded.
PLUGIN_ROOT = Path(__file__).parent.parent.parent
