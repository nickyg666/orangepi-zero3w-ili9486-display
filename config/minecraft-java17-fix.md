# Minecraft crash exit code 6 -> use Java 17
Crash: JVM SIGSEGV in JNI.invokePP -> libopenal.so alcGetString, at launch.
Root cause: PrismLauncher bundled Java (Temurin 25) + LWJGL 3.4.1 arm64 JNI
incompatibility. Java 25's JNI changes crash LWJGL's invokePP.
Fix: installed openjdk-17-jre, set instance JavaPath to
/usr/lib/jvm/java-17-openjdk-arm64/bin/java.
Also: OpenAL null driver (no audio HW) + options.txt overrideWidth/Height=480x320.
