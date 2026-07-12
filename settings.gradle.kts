pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
}

plugins {
    // Auto-provisions the Temurin 21 toolchain on machines that lack it.
    id("org.gradle.toolchains.foojay-resolver-convention") version "0.8.0"
}

rootProject.name = "flame-of-mercolas"

include("sim-core")
include("content")
include("tools")
include("headless")
include("client-observer")

dependencyResolutionManagement {
    repositories {
        mavenCentral()
    }
}
