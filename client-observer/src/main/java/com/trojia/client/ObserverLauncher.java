package com.trojia.client;

import com.badlogic.gdx.backends.lwjgl3.Lwjgl3Application;
import com.badlogic.gdx.backends.lwjgl3.Lwjgl3ApplicationConfiguration;

/**
 * Desktop entry point for the god-view observer.
 *
 * <p>{@code --smoke=N} renders N frames and exits cleanly — used by automated
 * verification to prove the GL pipeline works without a human closing the window.
 * {@code --fixture=tavern|compound} selects which baked world boots (default
 * {@code tavern}); {@code --fixture=compound} additionally spawns and renders the
 * Trojian-Compound population (see {@link ObserverApp}). {@code --screenshot=path.png},
 * combined with {@code --smoke=N}, dumps the final frame to disk — a verification aid,
 * never used on the shipped interactive path.
 */
public final class ObserverLauncher {

    private ObserverLauncher() {
    }

    public static void main(String[] args) {
        Lwjgl3ApplicationConfiguration configuration = new Lwjgl3ApplicationConfiguration();
        configuration.setTitle("Flame of Mercolas — Observer");
        configuration.setWindowedMode(1280, 800);
        configuration.useVsync(true);
        configuration.setForegroundFPS(60);

        new Lwjgl3Application(
                new ObserverApp(parseFixture(args), parseSmokeFrames(args), parseScreenshotPath(args),
                        parseDebugSelect(args)),
                configuration);
    }

    private static int parseDebugSelect(String[] args) {
        for (String arg : args) {
            if (arg.startsWith("--debug-select=")) {
                return Integer.parseInt(arg.substring("--debug-select=".length()));
            }
        }
        return -1;
    }

    private static String parseScreenshotPath(String[] args) {
        for (String arg : args) {
            if (arg.startsWith("--screenshot=")) {
                return arg.substring("--screenshot=".length());
            }
        }
        return null;
    }

    private static int parseSmokeFrames(String[] args) {
        for (String arg : args) {
            if (arg.startsWith("--smoke=")) {
                return Integer.parseInt(arg.substring("--smoke=".length()));
            }
        }
        return 0;
    }

    private static ObserverApp.Fixture parseFixture(String[] args) {
        for (String arg : args) {
            if (arg.startsWith("--fixture=")) {
                String value = arg.substring("--fixture=".length()).trim().toUpperCase(
                        java.util.Locale.ROOT);
                return ObserverApp.Fixture.valueOf(value);
            }
        }
        return ObserverApp.Fixture.TAVERN;
    }
}
