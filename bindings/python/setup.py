"""
OUCS Python Package Setup
"""
from setuptools import setup, find_packages

setup(
    name="oucs",
    version="1.0.0",
    description="Open Universal Container for Sound — Python bindings",
    long_description=open("../../README.md").read(),
    long_description_content_type="text/markdown",
    author="OUCS Contributors",
    license="MIT",
    url="https://github.com/oucs-engine/oucs",
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
    ],
    entry_points={
        "console_scripts": [
            "oucs-py=oucs.cli:main",
        ]
    },
    package_data={"oucs": ["*.so", "*.dylib", "*.dll"]},
)
