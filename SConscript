# RT-Thread building script for component

import os
import shutil

from building import *

cwd = GetCurrentDir()
src = Glob('*.c') + Glob('*.cpp') 
CPPPATH = [cwd]
CPPDEFINES = ['LFS_CONFIG=lfs_config.h']

#delate non-used files
try:
    shutil.rmtree(os.path.join(cwd,'.github'))
    shutil.rmtree(os.path.join(cwd,'bd'))
    shutil.rmtree(os.path.join(cwd,'scripts'))
    shutil.rmtree(os.path.join(cwd,'tests'))
    os.remove(os.path.join(cwd,'Makefile'))
except:
    pass

group = DefineGroup('littlefs', src, depend = ['PKG_USING_LITTLEFS', 'RT_USING_DFS'], CPPPATH = CPPPATH, CPPDEFINES = CPPDEFINES)

Return('group')
