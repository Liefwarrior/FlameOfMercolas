package com.trojia.client.fpv;

import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.World;
import com.trojia.sim.world.WorldConfig;

/**
 * {@link CellSight} over a real {@link World}, through its own {@link TileCursor}.
 *
 * <p><b>Its own</b> is the whole point. A {@code TileCursor} is a single-position flyweight,
 * and both {@code WorldRenderer} and {@code DepthVision} already hold one; borrowing either
 * would move its position out from under the pass that owns it, mid-frame, and the resulting
 * corruption would be intermittent and miserable to find. The cursor is also tick-stamped and
 * asserts a read on the tick it was positioned on, so it is repositioned on every read here
 * and never cached across a frame boundary.
 *
 * <p>Out-of-bounds coordinates report "not there" rather than throwing: the plan walks a
 * square window around the eye, which near the map edge inevitably steps off it.
 */
public final class WorldCellSight implements CellSight {

    private final TileCursor cursor;
    private final int widthTiles;
    private final int heightTiles;
    private final int depthTiles;

    public WorldCellSight(World world) {
        this.cursor = world.cursor();
        WorldConfig config = world.config();
        this.widthTiles = config.chunksX() * Coords.CHUNK_SIZE_X;
        this.heightTiles = config.chunksY() * Coords.CHUNK_SIZE_Y;
        this.depthTiles = config.chunksZ() * Coords.CHUNK_SIZE_Z;
    }

    @Override
    public boolean moveTo(int x, int y, int z) {
        if (x < 0 || x >= widthTiles || y < 0 || y >= heightTiles || z < 0 || z >= depthTiles) {
            return false;
        }
        cursor.moveTo(PackedPos.pack(x, y, z));
        return true;
    }

    @Override
    public int form() {
        return cursor.form().ordinal();
    }

    @Override
    public int fluidBits() {
        return cursor.form() == TileForm.VOID ? 0 : cursor.fluidBits();
    }

    @Override
    public int materialId() {
        return cursor.materialId();
    }
}
