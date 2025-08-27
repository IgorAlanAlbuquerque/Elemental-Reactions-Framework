# Skyrim Magic System Overhaul: Destruction

**Skyrim Magic System Overhaul: Destruction** is a comprehensive mod for *The Elder Scrolls V: Skyrim Special Edition* that redefines the Destruction school with a dynamic elemental system. Built using SKSE and C++ for top-tier performance, this mod introduces gauge-based mechanics, elemental states, powerful reactions, and 39 reworked spells to make fire, frost, and shock magic strategic and immersive. From incinerating foes with explosive combos to freezing enemies solid or chaining lightning through crowds, SMSO Des transforms magical combat into a spectacle of elemental mastery.

## Features

### 1. Elemental Gauges
Every Destruction spell (fire, frost, shock) builds a corresponding gauge (*FireGauge*, *FrostGauge*, *ShockGauge*) on enemies when hit. Gauges track elemental exposure, enabling powerful effects and reactions when filled to 100 units.

### 2. Elemental States
Environmental and equipment-based states alter how enemies interact with magic:
- **Wet**: Triggered by rain or water effects. Reduces fire damage and *FireGauge* buildup, but boosts shock damage and *frostGauge*.
- **Rubber**: From crafted rubber armor. Reduces shock damage and *ShockGauge* buildup, but ncreases the buildup rate of FireGauge and FrostGauge.
- **Fur/Fat**: From crafted fur armor or enemies like trolls. Enhances frost resistance and *FrostGauge* buildup, but increases fire damage taken and speeds up ShockGauge buildup

### 3. Maximum Gauge Effects
When a gauge reaches 100 units, a unique effect triggers:
- **Incineration (Fire)**: A damage-over-time (DoT) effect burns the target.
- **Deep Freeze (Frost)**: Freezes the target for 3 seconds, immobilizing them with a lingering slow effect.
- **Overcharge (Shock)**: Cause small continuous electric shocks that stagger the enemy for 0.3 seconds every 1.3 seconds. Enemies with the 'electrified' effect shock nearby enemies also affected by electricity.

### 4. Elemental Reactions
Combining elements triggers powerful reactions by consuming one gauge to amplify another:

### 5. Removal of Vanilla Drains
Vanilla magicka and stamina drains from shock and frost spells are removed, replaced by the gauge system for a cohesive experience.

### 6. Reworked Spells (39 New Spells)
The Destruction school is revamped with 39 spells (11 fire, 11 frost, 11 shock), from Novice to Master:

### 7. Reworked Perks
The Destruction perk tree is overhauled:

### 8. Power-Ups for Low-Level Spells
Novice to Adept spells scale with Destruction skill.

### 9. Cooldowns for Balance
Spells have cooldowns.

### 10. Long-Cast Animations and NPC Collaboration
Higher-ranked spells have longer animations. Spells will continuously drain mana during the animation until their cost is met. It is possible for one (or more) NPC with the same spell to cast simultaneously, jointly consuming mana until the total required for activating the spell is reached. If this is done, only one spell will be cast, but the casting time can be drastically reduced. The initial mana points are consumed more rapidly, but subsequent points are consumed more slowly.

## Contributing

Want to contribute to *SMSOD*? We welcome bug fixes, feature suggestions, and code contributions!
1. Fork the repository on [GitHub](https://github.com/yourusername/smsod).
2. Create a branch for your changes (`git checkout -b feature/your-feature`).
3. Commit your changes with clear messages (`git commit -m "Add feature X"`).
4. Push to your fork (`git push origin feature/your-feature`).
5. Open a Pull Request on GitHub.