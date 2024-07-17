@echo off

set cl_debug_flags=/Zi /DEBUG /DDEBUG /Od /MTd
set cl_release_flags=/Zi /DNDEBUG /O2 /GL

set link_debug_flags=/DEBUG:FULL
set link_release_flags=/LTCG /RELEASE

if not exist "build" mkdir "build"

pushd build

cl /nologo /FC /I..\src\ /arch:AVX2 /DTIMER /Oi %cl_release_flags% ..\src\count_lines.c /link %link_release_flags% /MANIFEST:EMBED /INCREMENTAL:NO /out:count_lines.exe
cl /nologo /FC /I..\src\ /arch:AVX2 /DTIMER /Oi %cl_release_flags% ..\src\find_line.c /link %link_release_flags% /MANIFEST:EMBED /INCREMENTAL:NO /out:find_line.exe

popd
