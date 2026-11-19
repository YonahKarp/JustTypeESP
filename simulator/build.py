"""Build Leaf's client-only firmware simulator. Requires an activated emsdk."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--emsdk', type=Path, help='SDK root, if emcmake is not on PATH')
parser.add_argument('--cmake', default='cmake', help='CMake executable')
parser.add_argument('--ninja', default='ninja', help='Ninja executable')
parser.add_argument('--lvgl', type=Path, help='Optional LVGL 9.2.2 source directory')
args = parser.parse_args()
root = Path(__file__).resolve().parent
build = root / 'build'
env = os.environ.copy()
if args.emsdk:
    env['PATH'] = str(args.emsdk.resolve() / 'upstream/emscripten') + os.pathsep + env['PATH']
emcmake = shutil.which('emcmake', path=env['PATH'])
if not emcmake:
    parser.error('emcmake not found. Activate emsdk or pass --emsdk PATH.')
cmake = shutil.which(args.cmake)
ninja = shutil.which(args.ninja)
if not cmake or not ninja:
    parser.error('Install CMake and Ninja, or pass their paths via --cmake and --ninja.')
command = [emcmake, cmake, '-S', str(root), '-B', str(build), '-G', 'Ninja',
           '-DCMAKE_MAKE_PROGRAM=' + ninja.replace('\\', '/')]
if args.lvgl:
    command += ['-DLVGL_DIR=' + args.lvgl.resolve().as_posix()]
subprocess.run(command, env=env, check=True)
subprocess.run([cmake, '--build', str(build), '--parallel', '8'], env=env, check=True)
archive = shutil.make_archive(str(build / 'leaf-simulator'), 'zip', build / 'site')
print(f'\nStatic website: {build / "site"}\nDeployment archive: {archive}')
print(f'Preview: python -m http.server 4173 --directory "{build / "site"}"')
