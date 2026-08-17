"""
MAKO Decky package for Decky Loader.

This package provides services for installing and managing the MAKO Renderer
Vulkan layer for Lossless Scaling frame generation.
"""

try:
    from .plugin import Plugin
    __all__ = ['Plugin']
except ImportError:
    __all__ = []
