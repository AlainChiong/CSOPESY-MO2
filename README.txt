CSOPESY MO2 - Multitasking OS Emulator

Group:
S07_GRP11

Members:
Alain Timothy Chiong
Jandeil Dural
Widenmar Embuscado

Entry file containing main():
src/main.cpp

Instructions:

Remove-Item -Recurse -Force build , Do this if there is a build folder
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
.\os_emulator.exe