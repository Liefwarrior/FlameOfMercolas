plugins {
    `java-library`
    `java-test-fixtures`
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(21)
    }
}

// sim-core is ZERO-dependency (JDK 21 only) per ARCHITECTURE.md §2 —
// determinism and portability outrank library convenience here.
dependencies {
    testImplementation(platform(libs.junit.bom))
    testImplementation(libs.junit.jupiter)
    testImplementation(libs.archunit.junit5)
    testRuntimeOnly(platform(libs.junit.bom))
    testRuntimeOnly(libs.junit.platform.launcher)
}

tasks.test {
    useJUnitPlatform {
        // WritePathBenchTest is a hard wall-clock ceiling assertion — inherently
        // timing-sensitive (JIT warmup, GC pauses, CPU frequency scaling, or
        // contention from parallel Gradle test workers can flake it on a busy
        // shared runner for reasons unrelated to any write-path regression).
        // Excluded from the default build; run explicitly via `benchmarkTest`.
        excludeTags("benchmark")
    }
}

tasks.register<Test>("benchmarkTest") {
    description = "Runs timing-sensitive benchmark tests (e.g. WritePathBenchTest) excluded " +
            "from the default `test` task."
    group = "verification"
    testClassesDirs = sourceSets.test.get().output.classesDirs
    classpath = sourceSets.test.get().runtimeClasspath
    useJUnitPlatform {
        includeTags("benchmark")
    }
}
