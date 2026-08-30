"""
OUCS Python Package Setup
Open Universal Container for Sound — Python bindings
"""
from setuptools import setup, find_packages
from pathlib import Path

# Read README from repo root (2 levels up) or local fallback
try:
    readme = (Path(__file__).parent.parent.parent / "README.md").read_text(encoding="utf-8")
except FileNotFoundError:
    readme = "OUCS — Open Universal Container for Sound. Python bindings for liboucs."

setup(
    name="oucs",
    version="1.0.0",
    description="Open Universal Container for Sound — Python bindings for liboucs",
    long_description=readme,
    long_description_content_type="text/markdown",
    author="ParthHariom",
    author_email="",
    license="MIT",
    url="https://github.com/ParthHariom/oucs",
    project_urls={
        "Source":      "https://github.com/ParthHariom/oucs",
        "Bug Tracker": "https://github.com/ParthHariom/oucs/issues",
        "Spec":        "https://github.com/ParthHariom/oucs/blob/main/SPEC.md",
    },
    packages=find_packages(),
    python_requires=">=3.8",
    install_requires=[],  # zero runtime deps — only stdlib ctypes
    extras_require={
        "dev": ["pytest", "pytest-asyncio"],
    },
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Topic :: Multimedia :: Sound/Audio",
        "Topic :: Software Development :: Libraries",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Operating System :: OS Independent",
    ],
    keywords="audio music container streaming oucs mp3 flac wav ogg",
    package_data={"oucs": ["*.so", "*.dylib", "*.dll"]},
)
