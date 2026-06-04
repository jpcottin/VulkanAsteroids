plugins {
  alias(libs.plugins.android.application)
}

// Push and run the native Google Test binary on the connected device/emulator.
// Usage: ./gradlew runNativeTests
tasks.register("runNativeTests") {
    dependsOn("externalNativeBuildDebug")
    // Evaluate path at configuration time so the config cache can serialise it.
    val binPath = layout.buildDirectory
        .file("intermediates/cmake/debug/obj/arm64-v8a/game_tests")
        .get().asFile.absolutePath
    doLast {
        fun adb(vararg args: String) {
            val rc = ProcessBuilder("adb", *args).inheritIO().start().waitFor()
            check(rc == 0) { "adb ${args.toList()} exited $rc" }
        }
        adb("push", binPath, "/data/local/tmp/game_tests")
        adb("shell", "chmod", "+x", "/data/local/tmp/game_tests")
        adb("shell", "/data/local/tmp/game_tests")
    }
}

android {
    namespace = "com.jpcottin.vulkanasteroids"
    compileSdk = 36
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.jpcottin.vulkanasteroids"
        // Vulkan requires API 24+.
        minSdk = 24
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += "-DANDROID_STL=c++_static"
            }
        }
        ndk {
            // Emulator is x86_64; arm64-v8a covers physical devices.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures {
        compose = false
        aidl = false
        buildConfig = false
    }
}
