plugins {
    application
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(21)
    }
}

application {
    mainClass = "com.trojia.headless.HeadlessLauncher"
}

dependencies {
    implementation(project(":sim-core"))
    runtimeOnly(project(":content"))

    testImplementation(platform(libs.junit.bom))
    testImplementation(libs.junit.jupiter)
    testRuntimeOnly(platform(libs.junit.bom))
    testRuntimeOnly(libs.junit.platform.launcher)
}

tasks.test {
    useJUnitPlatform()
}

// F2.5 actors foundation demo (ACTORS-SPEC.md) — separate entry point/task so
// the existing `run` task (HeadlessLauncher) is untouched.
tasks.register<JavaExec>("runActorsDemo") {
    group = "application"
    description = "Runs the F2.5 actor-system foundation demo over the real engine."
    mainClass.set("com.trojia.headless.ActorsDemoMain")
    classpath = sourceSets["main"].runtimeClasspath
}
