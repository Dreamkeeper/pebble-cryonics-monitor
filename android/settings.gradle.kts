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
        // Two Maven Central mirrors: repo.maven.apache.org 403s on some
        // networks, and each mirror can lag individual files — together
        // they cover each other's gaps.
        maven("https://maven-central.storage-download.googleapis.com/maven2")
        maven("https://maven.aliyun.com/repository/central")
        mavenCentral()
    }
}
rootProject.name = "cryonics-monitor-companion"
include(":app")
