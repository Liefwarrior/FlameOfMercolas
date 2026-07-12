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
