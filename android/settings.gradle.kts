pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
        maven("https://jitpack.io") // PebbleKitAndroid2
    }
}
rootProject.name = "cryonics-monitor-companion"
include(":app")
