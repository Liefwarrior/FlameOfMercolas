package com.trojia.sim;

import com.tngtech.archunit.core.domain.JavaClass;
import com.tngtech.archunit.core.domain.JavaField;
import com.tngtech.archunit.core.domain.JavaModifier;
import com.tngtech.archunit.core.importer.ImportOption;
import com.tngtech.archunit.junit.AnalyzeClasses;
import com.tngtech.archunit.junit.ArchTest;
import com.tngtech.archunit.lang.ArchCondition;
import com.tngtech.archunit.lang.ArchRule;
import com.tngtech.archunit.lang.ConditionEvents;
import com.tngtech.archunit.lang.SimpleConditionEvent;
import com.trojia.sim.event.SimEvent;

import static com.tngtech.archunit.lang.syntax.ArchRuleDefinition.classes;
import static com.tngtech.archunit.lang.syntax.ArchRuleDefinition.noClasses;
import static com.tngtech.archunit.lang.syntax.ArchRuleDefinition.noFields;

/**
 * The automated guardrails for the project's foundational rules (ARCHITECTURE
 * §2, §6): sim-core is a pure, deterministic simulation engine. If a rule here
 * fails, the module boundary or the determinism contract has been breached —
 * fix the code, never the rule.
 *
 * <p>Analyzes production classes only ({@code DoNotIncludeTests}): test code
 * may use hash containers and floating point for verification math (e.g. RNG
 * avalanche statistics) without weakening the sim-state bans.
 */
@AnalyzeClasses(packages = "com.trojia.sim",
        importOptions = ImportOption.DoNotIncludeTests.class)
final class ArchitecturePurityTest {

    @ArchTest
    static final ArchRule SIM_CORE_STAYS_PURE = noClasses()
            .should().dependOnClassesThat().resideInAnyPackage(
                    "com.badlogic..",
                    "java.awt..",
                    "javax.swing..",
                    "com.trojia.client..",
                    "com.trojia.tools..",
                    "com.trojia.headless..")
            .because("sim-core must stay renderable-agnostic and client-free "
                    + "(the whole engine must run headless and deterministic)");

    /**
     * §6 determinism rule: no hash-ordered containers anywhere in sim-core
     * state — iteration order of HashMap/HashSet is JVM-dependent, so any
     * side-effect-relevant iteration must run over sorted/insertion-ordered
     * structures instead. Allowlist: none.
     */
    @ArchTest
    static final ArchRule NO_HASH_CONTAINER_FIELDS = noFields()
            .should().haveRawType(java.util.HashMap.class)
            .orShould().haveRawType(java.util.HashSet.class)
            .because("hash iteration order is JVM-dependent; sim state and side-effectful "
                    + "iteration must use canonical sorted key order (ARCHITECTURE.md §6)");

    /**
     * §6 determinism rule: no float/double in sim state — integer/fixed-point
     * only (Q8/Q16, milli-units, deciK), so golden masters are cross-platform.
     */
    @ArchTest
    static final ArchRule NO_FLOATING_POINT_FIELDS = noFields()
            .should().haveRawType(float.class)
            .orShould().haveRawType(double.class)
            .orShould().haveRawType(Float.class)
            .orShould().haveRawType(Double.class)
            .orShould().haveRawType(float[].class)
            .orShould().haveRawType(double[].class)
            .because("sim state and state-affecting math are integer/fixed-point only "
                    + "(ARCHITECTURE.md §6 — goldens must be cross-platform)");

    /**
     * §5 event taxonomy rule: events are records of primitives + ids only —
     * no object payloads, no enums, no collections; a cell is a PackedPos int.
     */
    @ArchTest
    static final ArchRule EVENTS_ARE_RECORDS_OF_PRIMITIVES = classes()
            .that().implement(SimEvent.class)
            .should().beRecords()
            .andShould(haveOnlyPrimitiveInstanceFields())
            .because("events are records of primitives and ids only "
                    + "(ARCHITECTURE.md §5, ArchUnit-enforced)");

    private static ArchCondition<JavaClass> haveOnlyPrimitiveInstanceFields() {
        return new ArchCondition<>("have only primitive instance fields") {
            @Override
            public void check(JavaClass javaClass, ConditionEvents events) {
                for (JavaField field : javaClass.getFields()) {
                    if (field.getModifiers().contains(JavaModifier.STATIC)) {
                        continue;
                    }
                    if (!field.getRawType().isPrimitive()) {
                        events.add(SimpleConditionEvent.violated(field,
                                field.getFullName() + " has non-primitive type "
                                        + field.getRawType().getName()
                                        + " (events carry primitives and ids only)"));
                    }
                }
            }
        };
    }
}
