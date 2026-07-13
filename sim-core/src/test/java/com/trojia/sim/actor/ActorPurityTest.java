package com.trojia.sim.actor;

import com.tngtech.archunit.core.domain.JavaClass;
import com.tngtech.archunit.core.domain.JavaField;
import com.tngtech.archunit.core.domain.JavaMethod;
import com.tngtech.archunit.core.domain.JavaModifier;
import com.tngtech.archunit.core.importer.ImportOption;
import com.tngtech.archunit.junit.AnalyzeClasses;
import com.tngtech.archunit.junit.ArchTest;
import com.tngtech.archunit.lang.ArchCondition;
import com.tngtech.archunit.lang.ArchRule;
import com.tngtech.archunit.lang.ConditionEvents;
import com.tngtech.archunit.lang.SimpleConditionEvent;

import static com.tngtech.archunit.lang.syntax.ArchRuleDefinition.classes;
import static com.tngtech.archunit.lang.syntax.ArchRuleDefinition.noClasses;
import static com.tngtech.archunit.lang.syntax.ArchRuleDefinition.noFields;

/**
 * The actor package's own guardrails (ACTORS-SPEC.md §1.1/§1.4, test A24;
 * ARCHITECTURE.md §2/§6), scoped explicitly to {@code com.trojia.sim.actor..}
 * on top of the project-wide {@code ArchitecturePurityTest} (whose
 * {@code com.trojia.sim} analysis already covers this subtree — these rules
 * exist so a failure here names the actor package specifically).
 */
@AnalyzeClasses(packages = "com.trojia.sim.actor",
        importOptions = ImportOption.DoNotIncludeTests.class)
final class ActorPurityTest {

    @ArchTest
    static final ArchRule NO_THIRD_PARTY_DEPENDENCIES = classes()
            .that().resideInAPackage("com.trojia.sim.actor..")
            .should().onlyDependOnClassesThat()
            .resideInAnyPackage("com.trojia.sim..", "java..", "javax..")
            .because("sim-core (and every subpackage) is zero-third-party-dependency, "
                    + "JDK + sim-core only (ARCHITECTURE.md §2)");

    @ArchTest
    static final ArchRule NO_HASH_CONTAINER_FIELDS = noFields()
            .should().haveRawType(java.util.HashMap.class)
            .orShould().haveRawType(java.util.HashSet.class)
            .because("hash iteration order is JVM-dependent (ARCHITECTURE.md §6) — actor "
                    + "registries/side-tables use sorted arrays instead");

    @ArchTest
    static final ArchRule NO_FLOATING_POINT_FIELDS = noFields()
            .should().haveRawType(float.class)
            .orShould().haveRawType(double.class)
            .orShould().haveRawType(Float.class)
            .orShould().haveRawType(Double.class)
            .because("actor state and score math are integer-only (ARCHITECTURE.md §6)");

    @ArchTest
    static final ArchRule ACTOR_SUBCLASSES_ARE_THIN = classes()
            .that().resideInAPackage("com.trojia.sim.actor.type")
            .should(declareOnlyConstructorAndPoliciesOverride())
            .because("thin Actor subclasses declare zero INSTANCE fields (static raws/policy "
                    + "constants — TYPE, STACK — are the sanctioned §1.4 shape) and zero verbs "
                    + "beyond the constructor + policies() (ACTORS-SPEC.md §1.1/§1.4, test A24)");

    @ArchTest
    static final ArchRule ACTOR_HIERARCHY_IS_DEPTH_TWO = classes()
            .that().areAssignableTo(Actor.class)
            .should(haveActorAsDirectSuperclass())
            .because("Actor -> type, never deeper (DECISIONS.md depth-2 ruling, test A24)");

    private static ArchCondition<JavaClass> declareOnlyConstructorAndPoliciesOverride() {
        return new ArchCondition<>("declare only a constructor and policies()") {
            @Override
            public void check(JavaClass javaClass, ConditionEvents events) {
                if (!javaClass.isAssignableTo(Actor.class)
                        || javaClass.getFullName().equals(Actor.class.getName())) {
                    return;
                }
                for (JavaField field : javaClass.getFields()) {
                    if (!field.getModifiers().contains(JavaModifier.STATIC)) {
                        events.add(SimpleConditionEvent.violated(javaClass,
                                javaClass.getFullName() + " declares instance field "
                                        + field.getName() + " (thin subclasses may not)"));
                    }
                }
                for (JavaMethod method : javaClass.getMethods()) {
                    if (!method.getName().equals("policies")) {
                        events.add(SimpleConditionEvent.violated(javaClass,
                                javaClass.getFullName() + " declares method " + method.getName()
                                        + " (thin subclasses may declare only policies())"));
                    }
                }
            }
        };
    }

    private static ArchCondition<JavaClass> haveActorAsDirectSuperclass() {
        return new ArchCondition<>("have Actor as its direct superclass") {
            @Override
            public void check(JavaClass javaClass, ConditionEvents events) {
                if (javaClass.isEquivalentTo(Actor.class)) {
                    return; // Actor itself is the root, not a "type" subject to the rule
                }
                boolean direct = javaClass.getRawSuperclass()
                        .map(sc -> sc.isEquivalentTo(Actor.class))
                        .orElse(false);
                if (!direct) {
                    events.add(SimpleConditionEvent.violated(javaClass,
                            javaClass.getFullName() + " does not extend Actor directly "
                                    + "(depth > 2)"));
                }
            }
        };
    }
}
