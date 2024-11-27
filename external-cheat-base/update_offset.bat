@echo off
setlocal

echo Updating offsets.hpp...
curl -o offsets.hpp https://raw.githubusercontent.com/a2x/cs2-dumper/refs/heads/main/output/offsets.hpp

echo Updating client_dll.hpp...
curl -o client_dll.hpp https://raw.githubusercontent.com/a2x/cs2-dumper/refs/heads/main/output/client_dll.hpp

echo Updating buttons.hpp...
curl -o buttons.hpp https://raw.githubusercontent.com/a2x/cs2-dumper/refs/heads/main/output/buttons.hpp

echo Update complete.
endlocal
pause