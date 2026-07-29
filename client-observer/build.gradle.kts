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
    // Golden-bless seam (FACES-SPEC T16): forwards -Dfacegen.bless=true into the test JVM
    // so `gradlew :client-observer:test -Dfacegen.bless=true` re-blesses the committed
    // golden face sheet. Defaults to false — goldens never move silently.
    systemProperty("facegen.bless", System.getProperty("facegen.bless", "false"))
}

// Compound-block actor population legibility listing (ACTORS-SPEC.md §7.2 / the "prove it"
// surface): GL-free, so it runs headless. Separate entry point/task so the GL :run
// (ObserverLauncher) is untouched. Example:
//   ./gradlew.bat :client-observer:runCompoundActors --args="--ticks 600"
tasks.register<JavaExec>("runCompoundActors") {
    group = "application"
    description = "Prints the compound-block actor population (id/type/job/home/position/goal)."
    mainClass.set("com.trojia.client.scenario.CompoundBlockActorsMain")
    classpath = sourceSets["main"].runtimeClasspath
}

// Docks-ward district population: same GL-free proof surface at ~6x the actor count, plus
// the --perf wall-clock gate for the observer's FAST budget. Example:
//   ./gradlew.bat :client-observer:runDocksActors --args="--ticks 50000 --perf"
tasks.register<JavaExec>("runDocksActors") {
    group = "application"
    description = "Prints the docks-ward actor population + daily-life movement proof."
    mainClass.set("com.trojia.client.scenario.DocksActorsMain")
    classpath = sourceSets["main"].runtimeClasspath
}

// THE TWIN-RUN GATE (S8). Runs the Docks soak twice from one seed in one JVM and fails the
// build on ANY divergence — comparing the combined WorldHasher hash AND the full report text
// byte-for-byte. Wired into `check` (so `gradlew build` cannot go green on a nondeterministic
// ward), and runnable alone while iterating:
//   ./gradlew.bat :client-observer:twinRunGate
//   ./gradlew.bat :client-observer:twinRunGate -PtwinTicks=2000
// Never `outputs.upToDateWhen { true }`-cached: the whole point is that it re-runs.
val twinRunGate = tasks.register<JavaExec>("twinRunGate") {
    group = "verification"
    description = "Runs the Docks soak twice from one seed; fails on any world-hash or report-text divergence."
    mainClass.set("com.trojia.client.scenario.TwinRunGateMain")
    classpath = sourceSets["main"].runtimeClasspath
    outputs.upToDateWhen { false }
    val twinTicks = (project.findProperty("twinTicks") as String?) ?: "15000"
    argumentProviders.add(CommandLineArgumentProvider { listOf("--ticks", twinTicks) })
}

tasks.named("check") {
    dependsOn(twinRunGate)
}
