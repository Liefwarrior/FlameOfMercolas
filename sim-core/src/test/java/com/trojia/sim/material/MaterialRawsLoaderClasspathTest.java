package com.trojia.sim.material;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/**
 * The classpath entry point of {@link MaterialRawsLoader}: the committed raws
 * packaged under {@code trojia/raws/**} (exactly how the content jar ships
 * them) must load from both a jar classpath entry and a directory classpath
 * entry, byte-identical to the disk load.
 */
final class MaterialRawsLoaderClasspathTest {

    private static final String RESOURCE_ROOT = "trojia/raws";

    @TempDir
    Path temp;

    @Test
    void loadsFromJarClasspathEntryIdenticalToDiskLoad() throws IOException {
        Path rawsDir = MaterialRawsLoaderCommittedTest.locateRawsDir();
        Path jar = temp.resolve("content.jar");
        writeJar(jar, rawsDir);
        RawsBundle fromDisk = MaterialRawsLoader.load(rawsDir);
        try (URLClassLoader loader =
                new URLClassLoader(new URL[] {jar.toUri().toURL()}, null)) {
            RawsBundle fromJar = MaterialRawsLoader.load(loader, RESOURCE_ROOT);
            assertEquals(22, fromJar.materials().size());
            assertEquals(fromDisk.fingerprint(), fromJar.fingerprint());
            assertEquals(fromDisk.materials().fingerprint(), fromJar.materials().fingerprint());
        }
    }

    @Test
    void loadsFromDirectoryClasspathEntryIdenticalToDiskLoad() throws IOException {
        Path rawsDir = MaterialRawsLoaderCommittedTest.locateRawsDir();
        Path classpathDir = temp.resolve("classes");
        copyRaws(rawsDir, classpathDir.resolve("trojia").resolve("raws"));
        RawsBundle fromDisk = MaterialRawsLoader.load(rawsDir);
        try (URLClassLoader loader =
                new URLClassLoader(new URL[] {classpathDir.toUri().toURL()}, null)) {
            RawsBundle fromDir = MaterialRawsLoader.load(loader, RESOURCE_ROOT);
            assertEquals(fromDisk.fingerprint(), fromDir.fingerprint());
        }
    }

    @Test
    void missingResourceRootFailsFast() throws IOException {
        try (URLClassLoader loader = new URLClassLoader(new URL[0], null)) {
            RawsValidationException e = assertThrows(RawsValidationException.class,
                    () -> MaterialRawsLoader.load(loader, RESOURCE_ROOT));
            assertEquals(RESOURCE_ROOT, e.file());
        }
    }

    /** Packages the raws json files under {@code trojia/raws/**} with directory entries. */
    private static void writeJar(Path jar, Path rawsDir) throws IOException {
        try (ZipOutputStream zip = new ZipOutputStream(Files.newOutputStream(jar))) {
            zip.putNextEntry(new ZipEntry("trojia/"));
            zip.closeEntry();
            zip.putNextEntry(new ZipEntry(RESOURCE_ROOT + "/"));
            zip.closeEntry();
            for (Path dir : rawsSubdirs(rawsDir)) {
                String dirEntry = RESOURCE_ROOT + "/" + dir.getFileName() + "/";
                zip.putNextEntry(new ZipEntry(dirEntry));
                zip.closeEntry();
                for (Path file : jsonFiles(dir)) {
                    zip.putNextEntry(new ZipEntry(dirEntry + file.getFileName()));
                    zip.write(Files.readAllBytes(file));
                    zip.closeEntry();
                }
            }
        }
    }

    private static void copyRaws(Path rawsDir, Path target) throws IOException {
        for (Path dir : rawsSubdirs(rawsDir)) {
            Path targetDir = target.resolve(dir.getFileName().toString());
            Files.createDirectories(targetDir);
            for (Path file : jsonFiles(dir)) {
                Files.copy(file, targetDir.resolve(file.getFileName().toString()));
            }
        }
    }

    private static List<Path> rawsSubdirs(Path rawsDir) throws IOException {
        try (Stream<Path> listing = Files.list(rawsDir)) {
            return sortedByName(listing.filter(Files::isDirectory));
        }
    }

    private static List<Path> jsonFiles(Path dir) throws IOException {
        try (Stream<Path> listing = Files.list(dir)) {
            return sortedByName(listing
                    .filter(Files::isRegularFile)
                    .filter(p -> p.getFileName().toString().endsWith(".json")));
        }
    }

    private static List<Path> sortedByName(Stream<Path> paths) {
        List<Path> sorted = new ArrayList<>(paths.toList());
        sorted.sort(Comparator.comparing(p -> p.getFileName().toString()));
        return sorted;
    }
}
