@echo off

set build_dir=build

set cl_debug_flags=/Zi /DEBUG /DDEBUG /Od /MTd
set cl_release_flags=/Zi /DNDEBUG /O2 /Ot /GL /fp:fast

set link_debug_flags=/DEBUG:FULL
set link_release_flags=/LTCG /RELEASE

set cl_defines=/DTIMER
set cl_flags_common=/nologo /FC /I..\src\ /arch:AVX2 /Oi /W4

set link_flags_common=/MANIFEST:EMBED /INCREMENTAL:NO

if "%1" == "release" (
    set cl_flags=%cl_flags_common% %cl_defines% %cl_release_flags%
    set link_flags=%link_release_flags% %link_flags_common%
) else (
    set cl_flags=%cl_flags_common% %cl_defines% %cl_debug_flags%
    set link_flags=%link_debug_flags% %link_flags_common%
)

if not exist "%build_dir%" mkdir "%build_dir%"

pushd %build_dir%

cl %cl_flags% ..\src\read_speed_test.c /link %link_flags% /out:read_speed_test.exe
cl %cl_flags% ..\src\count_lines.c     /link %link_flags% /out:count_lines.exe
cl %cl_flags% ..\src\find_line.c       /link %link_flags% /out:find_line.exe
cl %cl_flags% ..\src\hash_file.c       /link %link_flags% /out:hash_file.exe

popd
