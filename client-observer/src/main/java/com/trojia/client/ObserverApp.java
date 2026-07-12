package com.trojia.client;

import com.badlogic.gdx.ApplicationAdapter;
import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.badlogic.gdx.utils.ScreenUtils;

/**
 * The observer application. M0: an empty window in Granadad-night colors.
 * M1 adds the world renderer, camera, z-scrub, and inspector.
 */
public final class ObserverApp extends ApplicationAdapter {

    private final int smokeFrames;
    private int framesRendered;

    public ObserverApp(int smokeFrames) {
        this.smokeFrames = smokeFrames;
    }

    @Override
    public void render() {
        ScreenUtils.clear(0.055f, 0.05f, 0.08f, 1f);

        framesRendered++;
        boolean smokeDone = smokeFrames > 0 && framesRendered >= smokeFrames;
        if (smokeDone || Gdx.input.isKeyJustPressed(Input.Keys.ESCAPE)) {
            if (smokeDone) {
                System.out.println("observer smoke test: rendered " + framesRendered + " frames OK");
            }
            Gdx.app.exit();
        }
    }
}
