// Resources-only module: raws, maps, placeholder art. No source code, ever —
// keeping it code-free means content can never grow accidental logic.
plugins {
    `java-library`
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(21)
    }
}

// Raws stay at content/raws/** as the editable source of truth (tools read the
// disk path); this copySpec ships them on the classpath in the content jar
// under trojia/raws/** so the runtime loads them via
// MaterialRawsLoader.load(ClassLoader, "trojia/raws").
tasks.processResources {
    from(layout.projectDirectory.dir("raws")) {
        include("**/*.json")
        into("trojia/raws")
    }
}
