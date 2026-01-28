#!/usr/bin/env python3
"""
Setup script for RobStride Live Streamer Blender addon
"""
from setuptools import setup, find_packages

setup(
    name="robstride-streamer",
    version="0.1.1",
    author="OpenAI Codex",
    description="Stream real-time motion setpoints from animation curves to ESP32 over Serial",
    long_description=open("README.md", encoding="utf-8").read() if __import__("os").path.exists("README.md") else "",
    long_description_content_type="text/markdown",
    packages=find_packages(),
    install_requires=[
        "pyserial>=3.5",
    ],
    python_requires=">=3.6",
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Environment :: Plugins",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
    ],
)
