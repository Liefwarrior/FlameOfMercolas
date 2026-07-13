plugins {
    application
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(21)
    }
}

application {
    mainClass = "com.trojia.client.ObserverLauncher"
}

dependencies {
    implementation(project(":sim-core"))
    runtimeOnly(project(":content"))

    implementation(libs.gdx.core)
    implementation(libs.gdx.backend.lwjgl3)
    runtimeOnly(variantOf(libs.gdx.platform) { classifier("natives-desktop") })

    // Test-only: the M1 bake step (TavernFixtureBakeTest) uses the tools module's
    // TmxReader/TsxReader/TiledWorldImporter to regenerate the committed
    // content/maps/baked/tavern_fixture.trojsav. The production :run classpath never
    // carries this dependency — FixtureWorldLoader only reads the baked file via
    // sim-core's WorldLoader (see content/maps/README.md: "the client never reads
    // [Tiled] files — only the importer's baked TROJSAV output").
    testImplementation(project(":tools"))

    testImplementation(platform(libs.junit.bom))
    testImplementation(libs.junit.jupiter)
    testRuntimeOnly(platform(libs.junit.bom))
    testRuntimeOnly(libs.junit.platform.launcher)
}

tasks.test {
    useJUnitPlatform()
}
