import os
import sys


def pytest_configure(config):
    """Register external DLL directories on Windows before test collection triggers imports."""
    if sys.platform == "win32" and hasattr(os, "add_dll_directory"):
        dll_dir = os.environ.get("N3MF_DLL_DIR")
        if dll_dir and os.path.isdir(dll_dir):
            os.add_dll_directory(dll_dir)
