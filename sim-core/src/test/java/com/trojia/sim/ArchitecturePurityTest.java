package com.trojia.sim;

import com.tngtech.archunit.junit.AnalyzeClasses;
import com.tngtech.archunit.junit.ArchTest;
import com.tngtech.archunit.lang.ArchRule;

import static com.tngtech.archunit.lang.syntax.ArchRuleDefinition.noClasses;

/**
 * The automated guardrail for the project's foundational rule: sim-core is a
 * pure simulation engine. If this test fails, the module boundary has been
 * breached — fix the dependency, never the rule.
 */
@AnalyzeClasses(packages = "com.trojia.sim")
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
}
