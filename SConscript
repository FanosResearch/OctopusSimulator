# -*- mode:python -*-

import os

Import('env')

current_dir = os.path.dirname(File('SConscript').abspath)

env.Prepend(CPPPATH=Dir('.'))
# a littel hacky but can get a shared library working
env.Append(LIBS=['CMSpec'],
            LIBPATH=[current_dir+'/build/'],  # compile-time lookup
            RPATH=[current_dir+'/build/'],  # runtime lookup
            CPPPATH=[current_dir+'/src/'])
env.Append(LIBS=['mcsim'],
            LIBPATH=[current_dir+'/MCsim/src'],  # compile-time lookup
            RPATH=[current_dir+'/MCsim/src'],  # runtime lookup
            CPPPATH=[current_dir+'/MCsim/src'])
