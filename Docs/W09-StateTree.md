# W09: StateTree combat foundation

This commit adds the native foundation for the first enemy weapon slice. It does not include Content assets, a compiled StateTree asset, animations, or a completed playable enemy integration.

- `UDustEnemyWeaponComponent` belongs on an enemy pawn. Assign a weapon definition and combat profile, set a muzzle component owned by that pawn, and call `InitializeWeapon()` before starting StateTree logic.
- Each component owns a separate bare `AWeaponRuntime`; definitions remain shared and read-only. This avoids running first-person weapon Blueprint events on enemies.
- `FDustEnemyContextEvaluator` reads the controller's configured target property (default `TargetActor`) and exposes target, range, sight, ammo, and component references for bindings.
- Use the AI Component StateTree schema with an AI StateTree component on the controller. `Enemy Settle` stops movement; `Enemy Combat Engagement` starts shooting and cleans up on exit. Gate combat by target, range, sight, and ammo. Use a delayed retry on failures. Do not run legacy chase/attack loops concurrently.
- Bind `OnShotResolved` once. For a blocking hit on an actor implementing the existing damage interface, forward the supplied damage, bone, penetration and head multiplier once. The native component does not call Blueprint damage interfaces automatically.
- Weapon submission owns ammo/cooldown/recoil. AI tracking and delayed compensation are independent. New read-only recoil accessors do not advance a pattern or consume a presentation kick. Random weapon spread remains unchanged.
- This slice requires standing still to fire, pauses between volleys, stops on empty, and does not reload. Movement combat and cover behavior are subsequent slices.

Validation: Development Editor build succeeded. Selected native suites produced 51 successful results, zero failures (one successful result included an unrelated HTTP connectivity warning). Includes 10 new AI math/runtime tests. World tests explicitly step temporary components; these are not evidence of Blueprint, StateTree asset, animation, or PIE acceptance.

Integration still requires the existing perception/search/death graphs to be reviewed, old direct damage to be made mutually exclusive, visual weapon and muzzle setup, damage event wiring, StateTree bindings/transitions, and in-editor regression checks. A source-only checkout cannot reproduce the full project without its excluded assets and third-party resources.
