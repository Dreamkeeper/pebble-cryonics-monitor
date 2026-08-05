plugins {
    id("com.android.application") version "8.5.0"
    id("org.jetbrains.kotlin.android") version "2.0.0"
}

android {
    namespace = "org.cryomonitor.companion"
    compileSdk = 35

    defaultConfig {
        applicationId = "org.cryomonitor.companion"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    // The gateway role (SMS/call at server command) ships only in the
    // sideload flavor: SEND_SMS/CALL_PHONE conflict with Play Store policy.
    flavorDimensions += "distribution"
    productFlavors {
        create("play") { dimension = "distribution" }
        create("sideload") { dimension = "distribution" }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    // TODO(M1): pin PebbleKitAndroid2 coordinates once published;
    // fallback: legacy com.getpebble:pebblekit:4.0.1 via intents.
    // implementation("com.github.pebble-dev:PebbleKitAndroid2:<version>")
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    testImplementation("junit:junit:4.13.2")
}
