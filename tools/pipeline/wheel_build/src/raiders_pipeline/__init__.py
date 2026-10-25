"""Reusable Python package-building API; importing it does not launch tools."""

from importlib.metadata import version

from .build_wheels import build_wheel, load_policy

__version__ = version("raiders-pipeline")
__all__ = ["build_wheel", "load_policy", "__version__"]
