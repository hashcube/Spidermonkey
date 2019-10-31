# SpiderMonkey VM

[SpiderMonkey](https://developer.mozilla.org/en-US/docs/SpiderMonkey) is [Mozilla](http://www.mozilla.org)'s JavaScript engine.

## About this repository

 - This is NOT the official SpiderMonkey standalone project. Official repo is [here](https://developer.mozilla.org/en-US/docs/SpiderMonkey)
 - Only contains the SpiderMonkey source code and needed files to compile it
 - Contains a few patches to make it compile on iOS (device and simulator)
 - Contains build scripts for iOS, Android, Win32 and OS X
 
 
## About builds

### iOS
 - JIT is disabled
 - Device only: compiled in RELEASE mode
 - Simulator only: compiled in DEBUG mode

### Android

 - JIT is enabled
 - compiled in RELEASE mode
 

### OS X

 - JIT is enabled
 - compiled in DEBUG mode
 

### Windows

 - JIT is enabled
 - compiled in RELEASE mode

### Linux

 - JIT is enabled
 - compiled in RELEASE mode

## About the patches
 
 - Patches for [SpiderMonkey](https://github.com/ricardoquesada/Spidermonkey/wiki/)
 

## extra compiling instructions for android

make sure the ndk version is 16b
NDK_ROOT and ANDROID_SDK_ROOT system variables are set to ndk directory and sdk directory
go to js/src/build-android
edit build.sh and uncomment all the platforms you want to build library for
run ./build.sh
 
