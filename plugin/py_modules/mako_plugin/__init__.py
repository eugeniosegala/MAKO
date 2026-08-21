"""
MAKO Decky package for Decky Loader.

This package provides services for installing and managing the MAKO Renderer
Vulkan layer for Lossless Scaling frame generation.
"""

import sys

from .package_paths import PLUGIN_ROOT


# Decky Loader exposes only ``py_modules`` on sys.path. Add the containing
# plugin directory before importing services that consume shared_config.py.
plugin_root = str(PLUGIN_ROOT)
if plugin_root not in sys.path:
    sys.path.insert(0, plugin_root)

try:
    from .plugin import Plugin
    __all__ = ['Plugin']
except ImportError:
    __all__ = []
