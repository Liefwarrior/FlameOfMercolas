package com.trojia.sim.json;

/**
 * An immutable node in a parsed JSON tree.
 *
 * <p>This is the root of the closed vocabulary produced by {@link MiniJson#parse(String)}:
 * exactly one variant exists per JSON value kind. The hierarchy is sealed so that
 * consumers (raws loaders in particular) can switch exhaustively over it and the
 * compiler will flag any unhandled kind.</p>
 *
 * <p><strong>Immutability contract:</strong> every implementation is deeply immutable
 * and safe to share across threads without synchronization. No implementation exposes
 * a mutable view of its contents.</p>
 *
 * <p><strong>Determinism contract:</strong> equality, hash codes, iteration order
 * ({@link JsonObject} preserves source insertion order) and {@link Object#toString()}
 * (canonical text per {@link MiniJson#write(JsonValue)}) are all fully determined by
 * the value's content — never by identity, load order, or JVM run.</p>
 */
public sealed interface JsonValue
        permits JsonObject, JsonArray, JsonString, JsonNumber, JsonBool, JsonNull {
}
