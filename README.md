# Elemental Reactions Framework (ERF)

**Elemental Reactions Framework (ERF)** is a high-performance SKSE/C++ **framework** for Skyrim SE/AE that lets mods **register Elements, States, Pre-Effects, and Reactions**, and drive a **gauge system** that accumulates on actors and **triggers reactions at 100**. It also ships an in-game HUD and an SKSE-Menu configuration UI.

---

## What ERF is (in one paragraph)

ERF provides a lightweight runtime for **elemental gameplay**. Mods declare **Elements** (e.g., Fire, Frost, Shock), optional **States** (e.g., Wet), **Pre-Effects** (logic that runs before a reaction), and **Reactions** (logic that executes when thresholds are met). During combat, ERF tracks **per-actor gauges** for each registered element and fires the best matching reaction when the **threshold (100)** is reached — either **Single** (per-element) or **Mixed** (combined share across elements).

---

## Key Concepts

### Elements
- Each Element has a **name**, a **color (RGB)** for HUD, and a **keyword/MGEF linkage** to detect accumulation from spells or effects.

### States
- Long/short-lived flags on actors that can **modify gauge gain** or **reaction selection** (e.g., *Wet* boosts Shock gauge, reduces Fire, etc.).

### Pre-Effects
- Callbacks executed **when accumulation happens** (before evaluating reactions).
- Useful for micro-interactions, e.g. “spark” visuals, small slowdowns, or conditional locks.

### Reactions
- Declared with an **element signature** (single or multi-element), **threshold policy** and **callback**.
- **Mixed mode**: ERF computes the **share** of each element in the gauge sum (0–100) and picks the best matching reaction for the current composition.
- **Single mode**: reaction triggers when an **individual element** hits 100.

---

## HUD & Configuration (SKSE Menu)

- Built-in circular **gauge HUD** per actor (player/NPC) with:
  - **Layout**: horizontal or vertical alignment, adjustable spacing.
  - **Offsets**: per-actor-type X/Y offsets (player & NPC).
  - **Scale**: per-actor-type scaling.
  - **Modes**: toggle HUD on/off; toggle Single vs Mixed.
  - **Multipliers**: separate gauge gain multipliers for player/NPC.
- All settings persist to an **INI** next to the DLL and can be changed in-game via the **SKSE Menu** (no SkyUI/MCM dependency required).

---


## Contributing

Want to contribute to *ERF*? We welcome bug fixes, feature suggestions, and code contributions!

1. Fork the repository on [GitHub](https://github.com/IgorAlanAlbuquerque/Elemental-Reactions-Framework).
2. Create a branch for your changes (`git checkout -b feature/your-feature`).
3. Commit your changes with clear messages (`git commit -m "Add feature X"`).
4. Push to your fork (`git push origin feature/your-feature`).
5. Open a Pull Request on GitHub.
