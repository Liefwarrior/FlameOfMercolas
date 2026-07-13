# Material raws (classpath root)

The editable raws live at `content/raws/**` (the source of truth — tools read that
disk path). The content build copies `content/raws/**/*.json` into the jar under
`trojia/raws/**` (see `content/build.gradle.kts`), which is what the runtime loads
via `MaterialRawsLoader.load(classLoader, "trojia/raws")`. Do not add raws here —
add them under `content/raws/` instead.
