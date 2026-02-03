import os
import subprocess
import sys
import sysconfig
import platform
import shlex
import numpy as np

import SCons.Errors

SCons.Warnings.warningAsException(True)

Decider('MD5-timestamp')

SetOption('num_jobs', max(1, int(os.cpu_count()/2)))

AddOption('--kaitai', action='store_true', help='Regenerate kaitai struct parsers')
AddOption('--asan', action='store_true', help='turn on ASAN')
AddOption('--ubsan', action='store_true', help='turn on UBSan')
AddOption('--mutation', action='store_true', help='generate mutation-ready code')
AddOption('--ccflags', action='store', type='string', default='', help='pass arbitrary flags over the command line')
AddOption('--minimal',
          action='store_false',
          dest='extras',
          default=os.path.exists(File('#.gitattributes').abspath), # minimal by default on release branch (where there's no LFS)
          help='the minimum build to run openpilot. no tests, tools, etc.')

# Detect platform
arch = subprocess.check_output(["uname", "-m"], encoding='utf8').rstrip()
if platform.system() == "Darwin":
  arch = "Darwin"
  brew_prefix = subprocess.check_output(['brew', '--prefix'], encoding='utf8').strip()
elif arch == "aarch64" and os.path.isfile('/TICI'):
  arch = "larch64"
assert arch in [
  "larch64",  # linux tici arm64
  "aarch64",  # linux pc arm64
  "x86_64",   # linux pc x64
  "Darwin",   # macOS arm64 (x86 not supported)
]

env = Environment(
  ENV={
    "PATH": os.environ['PATH'],
    "PYTHONPATH": Dir("#").abspath + "/opendbc",
  },
  CC='clang',
  CXX='clang++',
  CCFLAGS=[
    "-g",
    "-fPIC",
    "-O2",
    "-Wunused",
    "-Werror",
    "-Wshadow",
    "-Wno-unknown-warning-option",
    "-Wno-inconsistent-missing-override",
    "-Wno-c99-designator",
    "-Wno-reorder-init-list",
    "-Wno-vla-cxx-extension",
  ],
  CFLAGS=["-std=gnu11"],
  CXXFLAGS=["-std=c++20"],
  CPPPATH=[
    "#",
    "#replay",
    "#replay/msgq",
    "#replay/include"
  ],
  LIBPATH=[
    "#replay/common",
    "#replay/msgq",
  ],
  RPATH=[],
  CYTHONCFILESUFFIX=".cpp",
  COMPILATIONDB_USE_ABSPATH=True,
  REDNOSE_ROOT="#",
  tools=["default", "cython"],
  toolpath=["#replay/msgq/site_scons/site_tools"],
)

# Arch-specific flags and paths
if arch == "larch64":
  env.Append(LIBPATH=[
    "/usr/local/lib",
    "/system/vendor/lib64",
    "/usr/lib/aarch64-linux-gnu",
  ])
  arch_flags = ["-D__TICI__", "-mcpu=cortex-a57"]
  env.Append(CCFLAGS=arch_flags)
  env.Append(CXXFLAGS=arch_flags)
elif arch == "Darwin":
  env.Append(LIBPATH=[
    f"{brew_prefix}/lib",
    f"{brew_prefix}/opt/openssl@3.0/lib",
    f"{brew_prefix}/opt/llvm/lib/c++",
    "/System/Library/Frameworks/OpenGL.framework/Libraries",
  ])
  env.Append(CCFLAGS=["-DGL_SILENCE_DEPRECATION"])
  env.Append(CXXFLAGS=["-DGL_SILENCE_DEPRECATION"])
  env.Append(CPPPATH=[
    f"{brew_prefix}/include",
    f"{brew_prefix}/opt/openssl@3.0/include",
  ])
else:
  env.Append(LIBPATH=[
    "/usr/lib",
    "/usr/local/lib",
  ])


_extra_cc = shlex.split(GetOption('ccflags') or '')
if _extra_cc:
  env.Append(CCFLAGS=_extra_cc)

# no --as-needed on mac linker
if arch != "Darwin":
  env.Append(LINKFLAGS=["-Wl,--as-needed", "-Wl,--no-undefined"])

# progress output
node_interval = 5
node_count = 0
def progress_function(node):
  global node_count
  node_count += node_interval
  sys.stderr.write("progress: %d\n" % node_count)
if os.environ.get('SCONS_PROGRESS'):
  Progress(progress_function, interval=node_interval)

# ********** Cython build environment **********
py_include = sysconfig.get_paths()['include']
envCython = env.Clone()
envCython["CPPPATH"] += [py_include, np.get_include()]
envCython["CCFLAGS"] += ["-Wno-#warnings", "-Wno-shadow", "-Wno-deprecated-declarations"]
envCython["CCFLAGS"].remove("-Werror")

envCython["LIBS"] = []
if arch == "Darwin":
  envCython["LINKFLAGS"] = env["LINKFLAGS"] + ["-bundle", "-undefined", "dynamic_lookup"]
else:
  envCython["LINKFLAGS"] = ["-pthread", "-shared"]

np_version = SCons.Script.Value(np.__version__)
Export('envCython')

Export('env', 'arch')

# Setup cache dir
cache_dir = '/data/scons_cache' if arch == "larch64" else '/tmp/scons_cache'
CacheDir(cache_dir)
Clean(["."], cache_dir)

# ********** start building stuff **********
SConscript(['#replay/common/SConscript'])
Import('common')
common = [common, 'zmq']
Export('common')

# Build messaging (cereal + msgq + socketmaster + their dependencies)
# Enable swaglog include in submodules
SConscript(['#replay/msgq/SConscript'])

SConscript(['#replay/cereal/SConscript'])
Import('socketmaster', 'msgq', 'cereal')
messaging = [socketmaster, msgq, 'capnp', 'kj',]
Export('messaging')

replay_lib = SConscript(['#replay/SConscript'])
Export('replay_lib')
SConscript(['src/SConscript'])
