// Kotlin（dev.zat.citydrive.NativeDrive）との橋渡し
#include <jni.h>

#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>

#include "drive_host.h"

using citydrive::DriveHost;

namespace {

DriveHost* host(jlong h) { return reinterpret_cast<DriveHost*>(h); }

std::string str(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c);
    env->ReleaseStringUTFChars(s, c);
    return r;
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_zat_citydrive_NativeDrive_nativeCreate(JNIEnv* env, jclass, jobject assets) {
    return reinterpret_cast<jlong>(new DriveHost(AAssetManager_fromJava(env, assets)));
}

JNIEXPORT void JNICALL Java_dev_zat_citydrive_NativeDrive_nativeDestroy(JNIEnv*, jclass, jlong h) { delete host(h); }

JNIEXPORT void JNICALL Java_dev_zat_citydrive_NativeDrive_nativeSetSurface(JNIEnv* env, jclass, jlong h, jobject surface) {
    ANativeWindow* w = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    host(h)->setWindow(w);
    if (w) ANativeWindow_release(w);  // setWindow が参照を持つ
}

JNIEXPORT void JNICALL Java_dev_zat_citydrive_NativeDrive_nativeStart(JNIEnv* env, jclass, jlong h, jstring preset) {
    host(h)->start(str(env, preset));
}

JNIEXPORT void JNICALL Java_dev_zat_citydrive_NativeDrive_nativeSetPaused(JNIEnv*, jclass, jlong h, jboolean paused) {
    host(h)->setPaused(paused == JNI_TRUE);
}

JNIEXPORT void JNICALL Java_dev_zat_citydrive_NativeDrive_nativeSetFrameInterval(JNIEnv*, jclass, jlong h, jdouble s) {
    host(h)->setFrameInterval(s);
}

// 軸は float[5] = steer, throttle, brake, lookX, lookY。ボタンは int[4] = handbrake, headlights, cameraPresses, resetPresses
JNIEXPORT void JNICALL Java_dev_zat_citydrive_NativeDrive_nativeSetPad(JNIEnv* env, jclass, jlong h, jfloatArray axes, jintArray buttons) {
    citydrive::PadState p;
    jfloat a[5] = {};
    jint b[4] = {};
    env->GetFloatArrayRegion(axes, 0, 5, a);
    env->GetIntArrayRegion(buttons, 0, 4, b);
    p.steer = a[0];
    p.throttle = a[1];
    p.brake = a[2];
    p.lookX = a[3];
    p.lookY = a[4];
    p.handbrake = b[0] != 0;
    p.headlights = b[1] != 0;
    p.cameraPresses = b[2];
    p.resetPresses = b[3];
    host(h)->setPad(p);
}

// 状態：float[9] = phase, progress, speedKmh, gear, rpm, fps, onDeck, bonnet, slip
JNIEXPORT void JNICALL Java_dev_zat_citydrive_NativeDrive_nativePoll(JNIEnv* env, jclass, jlong h, jfloatArray out) {
    citydrive::HostState s = host(h)->poll();
    const jfloat v[9] = {static_cast<float>(s.phase), s.progress, s.telemetry.speedKmh, static_cast<float>(s.telemetry.gear),
                         s.telemetry.rpm, s.fps, s.telemetry.onDeck ? 1.f : 0.f, s.telemetry.bonnetCamera ? 1.f : 0.f,
                         s.telemetry.slip};
    env->SetFloatArrayRegion(out, 0, 9, v);
}

JNIEXPORT jstring JNICALL Java_dev_zat_citydrive_NativeDrive_nativeMessage(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(host(h)->poll().message.c_str());
}

}  // extern "C"
