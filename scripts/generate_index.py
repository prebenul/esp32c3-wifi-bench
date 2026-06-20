# Pre-build script placeholder
# The data/ directory is flashed separately via: pio run -t uploadfs
# This script exists so PlatformIO does not error on the extra_scripts entry.
import os
Import("env")

def pre_build(source, target, env):
    pass

env.AddPreAction("buildprog", pre_build)
