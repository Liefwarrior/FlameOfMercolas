package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

import java.util.Arrays;

/**
 * Bounded, deterministic 8-directional A* over the walkability grid
 * (ACTORS-SPEC.md &sect;2.5's flagged follow-up: "pathfinding refinement —
 * flow fields or A* — flagged for later, greedy is v1" — this is that
 * deferred work). A stateless static utility kept in this package for the
 * same "world-agnostic, no {@link ActorContext} coupling" reason
 * {@link Actor.WalkabilityQuery} itself lives here; see
 * {@link Actor#stepAlongRoute} for the per-actor cached-route consumer that
 * calls this.
 *
 * <p><b>Determinism</b> (ARCHITECTURE.md &sect;6): every data structure here
 * is a plain array — no {@code HashMap}/{@code HashSet}, no float/double, no
 * {@code java.util.Random}. The open set is a binary min-heap over parallel
 * arrays with a monotonic insertion-sequence tie-break (never object
 * identity/hashing), so the expansion order — and therefore the returned
 * route — is a pure function of {@code (startCell, targetCell, walk)}.
 *
 * <p><b>Bounded on two independent axes</b> so a genuinely unreachable or
 * pathological target fails fast instead of hanging: a bounding box no
 * larger than {@link #MAX_DIM} per side (the search window — see
 * {@link #findRoute}'s span check), and a hard cap of {@code maxNodes}
 * expansions (the search effort). Whichever is hit first, the search gives
 * up deterministically and returns {@code null} — never a partial/best-
 * effort route.
 */
public final class PathFinder {

    /** Default expansion budget every production call site passes. */
    public static final int DEFAULT_MAX_NODES = 4000;

    /** Slack added around the start/target bounding box, in tiles, on each side. */
    private static final int PADDING = 32;

    /**
     * Safety-valve cap on the start/target span, independent of the node
     * budget: docks is 192x128, so a full-map traversal plus padding fits at
     * 256 &lt;= 384. If the raw x or y span alone exceeds this, the target is
     * out of scope by design — {@link #findRoute} returns {@code null}
     * immediately rather than building an oversized search window.
     */
    private static final int MAX_DIM = 384;

    private static final int ORTHOGONAL_COST = 10;
    private static final int DIAGONAL_COST = 14; // integer 10*sqrt(2) approximation

    /**
     * Per-actor route-variety jitter (density revisit, "unique paths per person"): entering a
     * cell costs an extra {@code hash(actorSalt, cell) & JITTER_MASK} (0..3) — small against the
     * 10/14 octile base, so routes stay near-optimal, but the tie-breaking landscape differs per
     * actor and two actors sharing a start/target no longer walk the same ruler line. A PURE
     * integer hash (the cosmetic-variant murmur-fmix shape) of {@code (actorSalt, cell)} — no RNG
     * stream, no state — so one actor's route is a pure function of
     * {@code (actorSalt, startCell, targetCell, walk)}: twin runs reproduce it exactly.
     * Salt {@code 0} is the legacy no-jitter path (kept for salt-less callers/tests).
     */
    private static final int JITTER_MASK = 3;

    private static final byte UNVISITED = 0;
    private static final byte OPEN = 1;
    private static final byte CLOSED = 2;

    /** Fixed 8-neighbor order — West, East, North, South, then the four diagonals; never varies. */
    private static final int[] NEIGHBOR_DX = {-1, 1, 0, 0, -1, 1, -1, 1};
    private static final int[] NEIGHBOR_DY = {0, 0, -1, 1, -1, -1, 1, 1};
    private static final int[] NEIGHBOR_COST = {
            ORTHOGONAL_COST, ORTHOGONAL_COST, ORTHOGONAL_COST, ORTHOGONAL_COST,
            DIAGONAL_COST, DIAGONAL_COST, DIAGONAL_COST, DIAGONAL_COST
    };
    private static final boolean[] NEIGHBOR_IS_DIAGONAL = {
            false, false, false, false, true, true, true, true
    };

    private PathFinder() {
    }

    /**
     * Finds a bounded, deterministic route from {@code startCell} to
     * {@code targetCell}.
     *
     * @return the ordered waypoint cells (excluding start, including
     *         target); {@code new int[0]} if {@code startCell == targetCell};
     *         or {@code null} if the target is on a different z-level, the
     *         target itself is unwalkable, the start/target span exceeds
     *         {@link #MAX_DIM}, or the search exhausts {@code maxNodes}
     *         expansions without reaching the target
     */
    public static int[] findRoute(int startCell, int targetCell, Actor.WalkabilityQuery walk, int maxNodes) {
        return findRoute(startCell, targetCell, walk, maxNodes, 0);
    }

    /**
     * Salted variant of {@link #findRoute(int, int, Actor.WalkabilityQuery, int)}: {@code
     * actorSalt} (an actor id) perturbs every cell-entry cost by a tiny pure-hash jitter
     * ({@link #JITTER_MASK}), so different actors get visibly different, slightly wiggly —
     * still near-optimal — routes between the same endpoints, while the same actor always
     * gets the same route. Salt {@code 0} disables the jitter entirely (byte-identical to
     * the unsalted overload).
     */
    public static int[] findRoute(int startCell, int targetCell, Actor.WalkabilityQuery walk,
            int maxNodes, int actorSalt) {
        if (startCell == targetCell) {
            return new int[0];
        }
        int z = PackedPos.z(startCell);
        if (z != PackedPos.z(targetCell)) {
            return null;
        }
        if (!walk.isWalkable(targetCell)) {
            return null;
        }
        int sx = PackedPos.x(startCell);
        int sy = PackedPos.y(startCell);
        int tx = PackedPos.x(targetCell);
        int ty = PackedPos.y(targetCell);
        if (Math.abs(tx - sx) > MAX_DIM - 1 || Math.abs(ty - sy) > MAX_DIM - 1) {
            return null; // out of scope by design, not a hang
        }

        int minX = clamp(Math.min(sx, tx) - PADDING, PackedPos.X_MASK);
        int maxX = clamp(Math.max(sx, tx) + PADDING, PackedPos.X_MASK);
        int minY = clamp(Math.min(sy, ty) - PADDING, PackedPos.Y_MASK);
        int maxY = clamp(Math.max(sy, ty) + PADDING, PackedPos.Y_MASK);
        int width = maxX - minX + 1;
        int height = maxY - minY + 1;
        int cellCount = width * height;

        byte[] state = new byte[cellCount];
        int[] gScore = new int[cellCount];
        int[] parentLocalIdx = new int[cellCount];
        Arrays.fill(parentLocalIdx, -1);

        Heap heap = new Heap(64);

        int startLocalIdx = localIdx(sx, sy, minX, minY, width);
        int targetLocalIdx = localIdx(tx, ty, minX, minY, width);
        gScore[startLocalIdx] = 0;
        state[startLocalIdx] = OPEN;
        heap.push(startLocalIdx, octile(sx, sy, tx, ty));

        int expansions = 0;
        while (!heap.isEmpty()) {
            int currentLocalIdx = heap.popMinLocalIdx();
            if (state[currentLocalIdx] == CLOSED) {
                continue; // stale duplicate heap entry (lazy deletion, consistent heuristic)
            }
            state[currentLocalIdx] = CLOSED;
            expansions++;
            if (currentLocalIdx == targetLocalIdx) {
                return buildRoute(parentLocalIdx, startLocalIdx, targetLocalIdx, minX, minY, width, z);
            }
            if (expansions > maxNodes) {
                return null; // budget exhausted — deterministic give-up, never a hang
            }
            int cx = minX + currentLocalIdx % width;
            int cy = minY + currentLocalIdx / width;
            int currentG = gScore[currentLocalIdx];
            for (int n = 0; n < NEIGHBOR_DX.length; n++) {
                int nx = cx + NEIGHBOR_DX[n];
                int ny = cy + NEIGHBOR_DY[n];
                if (nx < minX || nx > maxX || ny < minY || ny > maxY) {
                    continue;
                }
                int neighborLocalIdx = localIdx(nx, ny, minX, minY, width);
                if (state[neighborLocalIdx] == CLOSED) {
                    continue;
                }
                int neighborCell = PackedPos.pack(nx, ny, z);
                if (!walk.isWalkable(neighborCell)) {
                    continue;
                }
                if (NEIGHBOR_IS_DIAGONAL[n]) {
                    int flankACell = PackedPos.pack(nx, cy, z);
                    int flankBCell = PackedPos.pack(cx, ny, z);
                    if (!walk.isWalkable(flankACell) || !walk.isWalkable(flankBCell)) {
                        continue; // never cut a solid diagonal wall corner
                    }
                }
                // Entry cost = octile base + the per-(actor, cell) jitter. Non-negative, so the
                // unjittered octile heuristic stays admissible/consistent (it only underestimates
                // more) — A* correctness and the lazy-deletion heap discipline are untouched.
                int tentativeG = currentG + NEIGHBOR_COST[n]
                        + (actorSalt == 0 ? 0 : jitter(actorSalt, neighborCell));
                if (state[neighborLocalIdx] == UNVISITED || tentativeG < gScore[neighborLocalIdx]) {
                    gScore[neighborLocalIdx] = tentativeG;
                    parentLocalIdx[neighborLocalIdx] = currentLocalIdx;
                    state[neighborLocalIdx] = OPEN;
                    heap.push(neighborLocalIdx, tentativeG + octile(nx, ny, tx, ty));
                }
            }
        }
        return null; // open set exhausted without reaching the target — genuinely unreachable
    }

    private static int[] buildRoute(int[] parentLocalIdx, int startLocalIdx, int targetLocalIdx,
            int minX, int minY, int width, int z) {
        int length = 0;
        for (int i = targetLocalIdx; i != startLocalIdx; i = parentLocalIdx[i]) {
            length++;
        }
        int[] route = new int[length];
        int i = targetLocalIdx;
        for (int idx = length - 1; idx >= 0; idx--) {
            int x = minX + i % width;
            int y = minY + i / width;
            route[idx] = PackedPos.pack(x, y, z);
            i = parentLocalIdx[i];
        }
        return route;
    }

    /**
     * The per-(actor, cell) entry-cost jitter in {@code [0, JITTER_MASK]}: a murmur3-fmix32
     * avalanche over the two ints (the cosmetic-variant hash precedent) — pure, stateless,
     * draw-free.
     */
    private static int jitter(int actorSalt, int cell) {
        int h = actorSalt * 0xCC9E2D51;
        h = Integer.rotateLeft(h, 15) * 0x1B873593;
        h ^= cell * 0xCC9E2D51;
        h ^= (h >>> 16);
        h *= 0x85EBCA6B;
        h ^= (h >>> 13);
        h *= 0xC2B2AE35;
        h ^= (h >>> 16);
        return h & JITTER_MASK;
    }

    /** Octile-distance admissible+consistent heuristic, integer units matching {@link #NEIGHBOR_COST}. */
    private static int octile(int x, int y, int tx, int ty) {
        int dx = Math.abs(tx - x);
        int dy = Math.abs(ty - y);
        int max = Math.max(dx, dy);
        int min = Math.min(dx, dy);
        return ORTHOGONAL_COST * max + (DIAGONAL_COST - ORTHOGONAL_COST) * min;
    }

    private static int localIdx(int x, int y, int minX, int minY, int width) {
        return (y - minY) * width + (x - minX);
    }

    private static int clamp(int coordinate, int max) {
        if (coordinate < 0) {
            return 0;
        }
        return Math.min(coordinate, max);
    }

    /**
     * A binary min-heap over parallel arrays, keyed by {@code f} with a
     * monotonic insertion-sequence tie-break — the same array-backed,
     * doubling-growth shape as {@code ActiveSet.grow()}'s precedent, so heap
     * order never depends on object identity or hash iteration order.
     */
    private static final class Heap {
        private int[] localIdx;
        private int[] f;
        private int[] seq;
        private int size;
        private int nextSeq;

        Heap(int initialCapacity) {
            localIdx = new int[initialCapacity];
            f = new int[initialCapacity];
            seq = new int[initialCapacity];
        }

        boolean isEmpty() {
            return size == 0;
        }

        void push(int idx, int fValue) {
            if (size == localIdx.length) {
                grow();
            }
            int i = size++;
            localIdx[i] = idx;
            f[i] = fValue;
            seq[i] = nextSeq++;
            siftUp(i);
        }

        int popMinLocalIdx() {
            int result = localIdx[0];
            size--;
            localIdx[0] = localIdx[size];
            f[0] = f[size];
            seq[0] = seq[size];
            siftDown(0);
            return result;
        }

        private boolean lessThan(int a, int b) {
            if (f[a] != f[b]) {
                return f[a] < f[b];
            }
            return seq[a] < seq[b];
        }

        private void swap(int a, int b) {
            int tl = localIdx[a];
            localIdx[a] = localIdx[b];
            localIdx[b] = tl;
            int tf = f[a];
            f[a] = f[b];
            f[b] = tf;
            int ts = seq[a];
            seq[a] = seq[b];
            seq[b] = ts;
        }

        private void siftUp(int i) {
            while (i > 0) {
                int parent = (i - 1) / 2;
                if (lessThan(i, parent)) {
                    swap(i, parent);
                    i = parent;
                } else {
                    break;
                }
            }
        }

        private void siftDown(int i) {
            while (true) {
                int left = 2 * i + 1;
                int right = 2 * i + 2;
                int smallest = i;
                if (left < size && lessThan(left, smallest)) {
                    smallest = left;
                }
                if (right < size && lessThan(right, smallest)) {
                    smallest = right;
                }
                if (smallest == i) {
                    break;
                }
                swap(i, smallest);
                i = smallest;
            }
        }

        private void grow() {
            int newCap = localIdx.length * 2;
            localIdx = Arrays.copyOf(localIdx, newCap);
            f = Arrays.copyOf(f, newCap);
            seq = Arrays.copyOf(seq, newCap);
        }
    }
}
