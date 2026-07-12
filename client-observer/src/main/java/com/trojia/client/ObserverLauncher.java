package com.trojia.client;

import com.badlogic.gdx.backends.lwjgl3.Lwjgl3Application;
import com.badlogic.gdx.backends.lwjgl3.Lwjgl3ApplicationConfiguration;

/**
 * Desktop entry point for the god-view observer.
 *
 * <p>{@code --smoke=N} renders N frames and exits cleanly — used by automated
 * verification to prove the GL pipeline works without a human closing the window.
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

        new Lwjgl3Application(new ObserverApp(parseSmokeFrames(args)), configuration);
    }

    private static int parseSmokeFrames(String[] args) {
        for (String arg : args) {
            if (arg.startsWith("--smoke=")) {
                return Integer.parseInt(arg.substring("--smoke=".length()));
            }
        }
        return 0;
    }
}
