"""RETIRED 2026-10-10 (Mike): RetroArch and wgc_libretro have their own repos now (teamgt19/RetroArch branch rave,
teamgt19/wgc-libretro), so the team repo no longer needs a copy. Kept for reference only.

Copies RAVE's own emulator-side source into the team repo (Mike 2026-10-09: "we have our own retroarch fork ...
push it, it's needed for proper function"). The working trees stay where they are built; this makes a reviewable,
rebuildable copy under F:/WORK/RAVE Standalone/EmulatorSource/ that git picks up:

  RetroArchFork/  RAVE's RetroArch build (plan #35) = upstream libretro/RetroArch at BASE_COMMIT + our patch:
                  rave.patch (git diff of the upstream files we changed) + files/ (our own new files, same paths).
                  Working tree: F:/WORK/_sources/RetroArch (787 MB clone - not copied).
  wgc_libretro/   the capture core (WindowCast) that plays .win games: its own source files, Spout + Vulkan helpers,
                  project file. Left out: third-party FFmpeg / x264 headers + libs (include/, lib/), build output
                  (bin/, x64/), NuGet packages and old / backup copies of files.
                  Working tree: F:/WORK/wgc_libretro.
Run again after changing either working tree, then commit.
  python rave_export_emulator_source.py
"""
import os
import shutil
import subprocess

OUT = 'F:/WORK/RAVE Standalone/EmulatorSource'
RA = 'F:/WORK/_sources/RetroArch'
WGC = 'F:/WORK/wgc_libretro/LibRetro-WGC'

# ---- RetroArch ------------------------------------------------------------------------------------------------------
ra_out = os.path.join(OUT, 'RetroArchFork')  # not 'Retroarch': .gitignore keeps */Retroarch/ installs out
if os.path.isdir(ra_out):
    shutil.rmtree(ra_out)  # only the export copy itself, rebuilt below
os.makedirs(os.path.join(ra_out, 'files'))
# The upstream commit our "rave" branch starts from (origin = libretro); the diff covers our commits on "rave" + any
# uncommitted work.
base = subprocess.run(['git', '-C', RA, 'merge-base', 'HEAD', 'origin/master'], capture_output=True, text=True, check=True).stdout.strip()
patch = subprocess.run(['git', '-C', RA, 'diff', '--binary', '--diff-filter=M', base], capture_output=True, check=True).stdout
open(os.path.join(ra_out, 'rave.patch'), 'wb').write(patch)
changed = subprocess.run(['git', '-C', RA, 'diff', '--name-only', '--diff-filter=M', base], capture_output=True, text=True, check=True).stdout.split()
new = subprocess.run(['git', '-C', RA, 'diff', '--name-only', '--diff-filter=A', base], capture_output=True, text=True, check=True).stdout.split('\n')
new += subprocess.run(['git', '-C', RA, 'ls-files', '--others', '--exclude-standard'], capture_output=True, text=True, check=True).stdout.split('\n')
new = sorted(set(f for f in new if f and (f.startswith('rave/') or os.path.basename(f) in ('rave_input.c', 'rave_joypad.c'))))
shutil.copy2(os.path.join(RA, 'COPYING'), os.path.join(ra_out, 'COPYING'))  # RetroArch's GPL-3.0 text
for rel in new:
    dst = os.path.join(ra_out, 'files', rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(os.path.join(RA, rel), dst)
with open(os.path.join(ra_out, 'README.md'), 'w', encoding='utf-8', newline='\n') as f:
    f.write('# RAVE RetroArch build (source)\n\n')
    f.write('Exported by `_tools/rave_export_emulator_source.py` from the working tree `F:/WORK/_sources/RetroArch`.\n\n')
    f.write('- Upstream: https://github.com/libretro/RetroArch at commit `%s`\n' % base)
    f.write('- `rave.patch`: our changes to %d upstream files (%s)\n' % (len(changed), ', '.join(changed)))
    f.write('- `files/`: our own new files, at their paths in the RetroArch tree (%d files: `rave/` = command-line switches,\n'
            '  VRCMD stdin protocol, TTL keep-alive, positional audio PAN, Spout picture sender; `input/drivers/rave_input.c`,\n'
            '  `input/drivers_joypad/rave_joypad.c` = input from the game)\n' % len(new))
    f.write('- Protocol spec: `RAVE Standalone/retroarch_rave_protocol.md`. Plan: feature plan #35.\n\n')
    f.write('## Rebuild\n\n```\ngit clone https://github.com/libretro/RetroArch.git && cd RetroArch\n'
            'git checkout %s\ngit apply ../rave.patch\ncp -r ../files/* .\n'
            '# MSYS2 MinGW64 shell (this PC: F:/WORK/_tools/msys64)\n./configure --disable-qt\nmake -j8\n```\n\n'
            'Ship it as `rave_retroarch.exe` (not retroarch.exe - NVIDIA Smooth Motion hooks that name):\n'
            '`python _tools/ra_pack_rave_retroarch.py` assembles the game\'s Retroarch folder.\n\n'
            'License: RetroArch is GPLv3; our changes are under the same licence.\n' % base)

# ---- wgc_libretro (capture core) ------------------------------------------------------------------------------------
wgc_out = os.path.join(OUT, 'wgc_libretro')
if os.path.isdir(wgc_out):
    shutil.rmtree(wgc_out)
os.makedirs(wgc_out)
KEEP_EXT = ('.cpp', '.c', '.h', '.inl', '.vcxproj', '.filters', '.info', '.bat', '.config')
SKIP_NAMES = ('libretro.bak', 'libretro.cpp.async', 'libretro.cpp.lightgun', 'libretro.cpp.prenig', 'libretro.cpp1',
              'stream_client.cpp.old')
count = 0
for rel_dir in ('', 'spout', 'vulkan'):
    src_dir = os.path.join(WGC, rel_dir)
    for base_dir, dirs, files in os.walk(src_dir):
        if rel_dir == '':
            dirs[:] = []  # top level only; the helper folders are walked on their own
        for name in files:
            if name in SKIP_NAMES or not name.endswith(KEEP_EXT):
                continue
            src = os.path.join(base_dir, name)
            dst = os.path.join(wgc_out, os.path.relpath(src, WGC))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            count += 1
for name in ('README.md',):
    src = os.path.join(os.path.dirname(WGC), name)
    if os.path.isfile(src):
        shutil.copy2(src, os.path.join(wgc_out, 'project_' + name))
with open(os.path.join(wgc_out, 'EXPORT.md'), 'w', encoding='utf-8', newline='\n') as f:
    f.write('# wgc_libretro (capture core) - source export\n\n'
            'Exported by `_tools/rave_export_emulator_source.py` from `F:/WORK/wgc_libretro/LibRetro-WGC` (%d files).\n\n'
            'NOT included (third party / build output, kept on the PC): `include/` + `lib/` (FFmpeg avcodec/avformat/... '
            'and x264 headers + import libraries, opus, rapidfuzz, Vulkan video headers), `bin/`, `x64/`, NuGet `packages/`, '
            'and the old / backup copies of libretro.cpp. Build: Visual Studio (MSBuild), `LibRetro-WGC.vcxproj`, x64 Release; '
            'the project expects those folders next to it.\n' % count)
print('RetroArch: base', base[:10], '|', len(changed), 'changed upstream files in rave.patch |', len(new), 'new files')
print('wgc_libretro:', count, 'files')
