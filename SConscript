# -*- mode:python -*-
#
# Picked up by gem5 when it is built with EXTRAS=<this directory>. It makes
# the Octopus headers visible to gem5/octopus.cc and links the shared library
# that CMake builds into build/.
#
# gem5 runs this script with a variant_dir under build/ARM, so Dir('header')
# would name a directory that does not exist there. Use the source tree's
# absolute paths instead.

import os

Import('env')

octopus_root = os.path.dirname(File('SConscript').srcnode().abspath)

header_dirs = [
    'header',
    'header/Arbiters',
    'header/CacheControllers',
    'header/Interconnect',
    'header/MCsim',
    'header/Protocols',
    'header/ReplacementPolicies',
    'header/SystemConfigurations',
]

env.Append(CPPPATH=[os.path.join(octopus_root, d) for d in header_dirs])
env.Append(LIBS=['Octopus'],
           LIBPATH=[os.path.join(octopus_root, 'build')],  # compile-time lookup
           RPATH=[os.path.join(octopus_root, 'build')])    # runtime lookup
