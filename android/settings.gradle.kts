pluginManagement {
    repositories {
        google()
        // Central mirror first: repo.maven.apache.org 403s on some networks
        maven("https://maven.aliyun.com/repository/central")
        maven("https://maven.aliyun.com/repository/gradle-plugin")
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        google()
        maven("https://maven.aliyun.com/repository/central")
        mavenCentral()
        maven("https://jitpack.io") // PebbleKitAndroid2 (future)
    }
}
rootProject.name = "cryonics-monitor-companion"
include(":app")
