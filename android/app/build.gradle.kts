import java.io.ByteArrayOutputStream
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

val benchRoot: File = rootDir.parentFile
val filamentVersion = "v1.77.1"

// versionName はアプリの版（タグ v1.2.0 → 1.2.0）、versionCode は単調増加の整数（CIの実行番号）。
// シーン定義の version とは別に管理し、結果JSONに両方を記録する
val appVersionName: String = (findProperty("benchdeck.versionName") as String?) ?: "1.0.0"
val appVersionCode: Int = ((findProperty("benchdeck.versionCode") as String?) ?: "1").toInt()

android {
    namespace = "dev.zat.benchdeck"
    compileSdk = 36
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "dev.zat.benchdeck"
        minSdk = 30        // Thermal API（getThermalHeadroom）が API 30 から
        targetSdk = 36     // Google Play は 2026-08-31 以降 API 36 以上を要求
        versionCode = appVersionCode
        versionName = appVersionName
        // 配布物は arm64-v8a のみ。CI の16KBページ・エミュレータ試験だけ -Pbenchdeck.abis=x86_64 で作る
        val abis = ((findProperty("benchdeck.abis") as String?) ?: "arm64-v8a").split(",").map { it.trim() }
        ndk { abiFilters += abis }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release")
                cppFlags += listOf("-std=c++20")
            }
        }
        buildConfigField("String", "FILAMENT_VERSION", "\"$filamentVersion\"")
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // 署名は CI で apksigner（APK Signature Scheme v2 以上）。ここでは未署名で出す。
            // 正式な鍵がまだ無い間は -Pbenchdeck.debugSigned=true で Android SDK の debug 鍵で署名する
            // （最適化・非 debuggable のリリース版のまま。正式な鍵の版へは上書き更新できない）
            signingConfig =
                if (providers.gradleProperty("benchdeck.debugSigned").orNull == "true") signingConfigs.getByName("debug")
                else null
        }
        debug {
            externalNativeBuild { cmake { arguments += "-DCMAKE_BUILD_TYPE=Release" } }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    packaging {
        // 共有ライブラリは非圧縮で 16KB 境界に置く（16KBページ端末で直接マップできるように）
        jniLibs { useLegacyPackaging = false }
    }

    androidResources {
        // マテリアル・カメラパスは圧縮しない（読込を速くする）
        noCompress += listOf("filamat", "bin", "ktx", "ktx2")
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
}

kotlin {
    compilerOptions { jvmTarget.set(JvmTarget.JVM_17) }
}

// ---- アセット：マテリアル（matc でビルド時にコンパイル）、シーン定義、変換済み素材 ----------------------
abstract class BenchAssetsTask : DefaultTask() {
    @get:InputFiles abstract val materials: ConfigurableFileCollection
    @get:InputFiles abstract val sceneFiles: ConfigurableFileCollection
    @get:InputFiles @get:Optional abstract val extraAssets: ConfigurableFileCollection
    @get:Input abstract val matc: Property<String>
    @get:Input abstract val extraRoot: Property<String>
    @get:OutputDirectory abstract val outputDir: DirectoryProperty

    @TaskAction
    fun run() {
        val out = outputDir.get().asFile
        out.deleteRecursively()
        val matDir = File(out, "materials").apply { mkdirs() }
        val matcFile = File(matc.get())
        if (!matcFile.canExecute()) {
            throw GradleException("matc not found at ${matcFile.path}. Run tools/fetch_filament.sh (Linux host required).")
        }
        materials.files.filter { it.extension == "mat" }.forEach { mat ->
            val target = File(matDir, mat.nameWithoutExtension + ".filamat")
            val log = ByteArrayOutputStream()
            val proc = ProcessBuilder(matcFile.path, "--api", "vulkan", "--platform", "all", "-o", target.path, mat.path)
                .redirectErrorStream(true).start()
            proc.inputStream.copyTo(log)
            if (proc.waitFor() != 0) throw GradleException("matc failed for ${mat.name}:\n$log")
        }
        val scenes = File(out, "scenes").apply { mkdirs() }
        sceneFiles.files.forEach { it.copyTo(File(scenes, it.name), overwrite = true) }
        // tools/convert_assets.py の出力（textures/*.ktx2, ibl/*.ktx）。無くても動く（実行時生成に切り替わる）
        val root = File(extraRoot.get())
        extraAssets.files.forEach { f ->
            val rel = f.relativeTo(root)
            f.copyTo(File(out, rel.path), overwrite = true)
        }
    }
}

val assetsRoot = File(benchRoot, "assets")
val benchAssets = tasks.register<BenchAssetsTask>("benchAssets") {
    materials.from(fileTree(File(benchRoot, "materials")))
    sceneFiles.from(File(benchRoot, "scenes/scene_v1.json"), File(benchRoot, "scenes/camera_path_v1.bin"))
    extraAssets.from(fileTree(assetsRoot) { include("textures/**", "ibl/**") })
    matc.set(File(benchRoot, "third_party/filament/linux/bin/matc").path)
    extraRoot.set(assetsRoot.path)
    outputDir.set(layout.buildDirectory.dir("generated/benchAssets"))
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(benchAssets, BenchAssetsTask::outputDir)
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2025.10.00")
    implementation(composeBom)
    implementation("androidx.core:core-ktx:1.17.0")
    implementation("androidx.activity:activity-compose:1.11.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.9.4")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.9.4")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
}
