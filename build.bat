@echo off

set cl_debug_flags=/Zi /DEBUG /DDEBUG /Od /MTd
set cl_release_flags=/Zi /DNDEBUG /O2 /GL

set link_debug_flags=/DEBUG:FULL
set link_release_flags=/LTCG /RELEASE

set cl_defines=/DTIMER
set cl_flags_common=/nologo /FC /I..\src\ /arch:AVX2 /Oi /W4

set link_flags_common=/MANIFEST:EMBED /INCREMENTAL:NO

if not exist "build" mkdir "build"

pushd build

cl %cl_flags_common% %cl_defines% %cl_release_flags% ..\src\count_lines.c /link %link_release_flags% %link_flags_common% /out:count_lines.exe
cl %cl_flags_common% %cl_defines% %cl_release_flags% ..\src\find_line.c   /link %link_release_flags% %link_flags_common% /out:find_line.exe

popd
