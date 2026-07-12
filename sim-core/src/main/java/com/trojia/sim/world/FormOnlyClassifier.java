package com.trojia.sim.world;

/**
 * The default {@link TileClassifier}: derives the blocking bits from the form
 * alone (WALL and VOID block movement and light; nothing else blocks). Used
 * until the material registry provides opacity-aware classification.
 */
enum FormOnlyClassifier implements TileClassifier {

    /** The sole, stateless instance. */
    INSTANCE;

    @Override
    public boolean blocksMove(short materialId, TileForm form) {
        return form == TileForm.WALL || form == TileForm.VOID;
    }

    @Override
    public boolean blocksLight(short materialId, TileForm form) {
        return form == TileForm.WALL || form == TileForm.VOID;
    }
}
