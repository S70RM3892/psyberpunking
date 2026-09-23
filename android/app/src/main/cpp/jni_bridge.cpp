// Kotlin（dev.zat.benchdeck.NativeBench）との橋渡し。Filament の Java API は使わず、C++ API を直接呼ぶ。
#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <jni.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "android_host.h"
#include "bench/vulkan_info.h"

using benchandroid::AndroidHost;

namespace {

AndroidHost* host(jlong h) { return reinterpret_cast<AndroidHost*>(h); }

std::string str(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c);
    env->ReleaseStringUTFChars(s, c);
    return r;
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_zat_benchdeck_NativeBench_nativeCreate(JNIEnv* env, jclass, jobject assets, jstring version) {
    AAssetManager* am = AAssetManager_fromJava(env, assets);
    return reinterpret_cast<jlong>(new AndroidHost(am, str(env, version)));
}

JNIEXPORT void JNICALL Java_dev_zat_benchdeck_NativeBench_nativeDestroy(JNIEnv*, jclass, jlong h) {
    delete host(h);
}

JNIEXPORT void JNICALL Java_dev_zat_benchdeck_NativeBench_nativeSetSurface(JNIEnv* env, jclass, jlong h, jobject surface) {
    ANativeWindow* w = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    host(h)->setWindow(w);
    if (w) ANativeWindow_release(w);  // setWindow が自分で acquire する
}

JNIEXPORT void JNICALL Java_dev_zat_benchdeck_NativeBench_nativeStart(JNIEnv* env, jclass, jlong h, jstring preset, jint laps,
                                                                      jboolean warmup, jdouble lapSeconds, jobjectArray device) {
    // device = [端末名, OS, リフレッシュレート]。GPU名・ドライバ・Vulkan版はネイティブで調べる
    bench::DeviceInfo d;
    d.host = "android";
    auto at = [&](int i) { return str(env, static_cast<jstring>(env->GetObjectArrayElement(device, i))); };
    d.device = at(0);
    d.os = at(1);
    d.refreshHz = std::atof(at(2).c_str());
    bench::VulkanInfo vk = bench::queryVulkanInfo();
    d.gpu = vk.deviceName;
    d.driver = vk.driverVersion;
    d.vulkanVersion = vk.apiVersion;
    host(h)->start(str(env, preset), laps, warmup == JNI_TRUE, lapSeconds, d);
}

JNIEXPORT void JNICALL Java_dev_zat_benchdeck_NativeBench_nativeAbort(JNIEnv* env, jclass, jlong h, jstring reason) {
    host(h)->abort(str(env, reason));
}

JNIEXPORT void JNICALL Java_dev_zat_benchdeck_NativeBench_nativeSetThermal(JNIEnv*, jclass, jlong h, jint status) {
    host(h)->setThermal(status);
}

JNIEXPORT void JNICALL Java_dev_zat_benchdeck_NativeBench_nativeSetHeadroom(JNIEnv*, jclass, jlong h, jfloat headroom) {
    host(h)->setHeadroom(headroom);
}

JNIEXPORT void JNICALL Java_dev_zat_benchdeck_NativeBench_nativeOnVsync(JNIEnv*, jclass, jlong h, jlong t) {
    host(h)->onVsync(t);
}

// [phase, progress*1000, lap, section, fps*10] と message
JNIEXPORT jintArray JNICALL Java_dev_zat_benchdeck_NativeBench_nativePoll(JNIEnv* env, jclass, jlong h) {
    auto s = host(h)->poll();
    jint v[5] = {static_cast<jint>(s.phase), static_cast<jint>(s.progress * 1000), s.lap, s.section,
                 static_cast<jint>(s.fps * 10)};
    jintArray a = env->NewIntArray(5);
    env->SetIntArrayRegion(a, 0, 5, v);
    return a;
}

JNIEXPORT jstring JNICALL Java_dev_zat_benchdeck_NativeBench_nativeMessage(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(host(h)->poll().message.c_str());
}

JNIEXPORT jstring JNICALL Java_dev_zat_benchdeck_NativeBench_nativeResultJson(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(host(h)->resultJson().c_str());
}

JNIEXPORT jstring JNICALL Java_dev_zat_benchdeck_NativeBench_nativeResultCsv(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(host(h)->resultCsv().c_str());
}

// [GPU名, ドライバ版, Vulkan版]（取れなければ空）
JNIEXPORT jobjectArray JNICALL Java_dev_zat_benchdeck_NativeBench_nativeVulkanInfo(JNIEnv* env, jclass) {
    bench::VulkanInfo vk = bench::queryVulkanInfo();
    jobjectArray a = env->NewObjectArray(3, env->FindClass("java/lang/String"), nullptr);
    env->SetObjectArrayElement(a, 0, env->NewStringUTF(vk.deviceName.c_str()));
    env->SetObjectArrayElement(a, 1, env->NewStringUTF(vk.driverVersion.c_str()));
    env->SetObjectArrayElement(a, 2, env->NewStringUTF(vk.apiVersion.c_str()));
    return a;
}

// 画像差分用のスクリーンショット。各要素は 1280×800 の RGBA8
JNIEXPORT jobjectArray JNICALL Java_dev_zat_benchdeck_NativeBench_nativeRenderShots(JNIEnv* env, jclass, jlong h,
                                                                                   jdoubleArray times) {
    std::vector<double> t(static_cast<size_t>(env->GetArrayLength(times)));
    env->GetDoubleArrayRegion(times, 0, static_cast<jsize>(t.size()), t.data());
    std::vector<std::vector<uint8_t>> shots;
    std::string err;
    if (!host(h)->renderShots(t, shots, &err)) {
        __android_log_print(ANDROID_LOG_ERROR, "BenchDeck", "shots failed: %s", err.c_str());
        return nullptr;
    }
    jobjectArray out = env->NewObjectArray(static_cast<jsize>(shots.size()), env->FindClass("[B"), nullptr);
    for (size_t i = 0; i < shots.size(); ++i) {
        jbyteArray b = env->NewByteArray(static_cast<jsize>(shots[i].size()));
        env->SetByteArrayRegion(b, 0, static_cast<jsize>(shots[i].size()), reinterpret_cast<const jbyte*>(shots[i].data()));
        env->SetObjectArrayElement(out, static_cast<jsize>(i), b);
        env->DeleteLocalRef(b);
    }
    return out;
}

}  // extern "C"
