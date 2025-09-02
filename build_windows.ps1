git submodule update --init
if (-Not (Test-Path -Path vcpkg )) { New-Item -ItemType Directory -Path vcpkg }
cd vcpkg
git init
git remote add origin https://github.com/microsoft/vcpkg.git
git fetch origin
git checkout -b master --track origin/master
git checkout b27651341123a59f7187b42ef2bc476284afb310
.\bootstrap-vcpkg.bat
.\vcpkg integrate install
.\vcpkg install libusb libpcap libsodium ffmpeg qt5 sdl2 vcpkg-tool-ninja
cd ..
cmake "-DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake" -S ./ -B "build" -G "Visual Studio 17 2022"
cmake --build build --config Release --target fpv4win