# -*- mode:python -*-

import os

Import('env')

current_dir = os.path.dirname(File('SConscript').abspath)
CMSpec_path = os.path.join(current_dir, 'CMSpec')


env.Prepend(CPPPATH=Dir('.'))
# a littel hacky but can get a shared library working
env.Append(LIBS=['CMSpec'],
            LIBPATH=[CMSpec_path+'/build/'],  # compile-time lookup
            RPATH=[CMSpec_path+'/build/'],  # runtime lookup
            CPPPATH=[CMSpec_path+'/src/'])
env.Append(LIBS=['mcsim'],
            LIBPATH=[CMSpec_path+'/MCsim/src'],  # compile-time lookup
            RPATH=[CMSpec_path+'/MCsim/src'],  # runtime lookup
            CPPPATH=[CMSpec_path+'/MCsim/src'])
