# Ski-Runner – 2D Endless Runner Game

An endless-runner game written in **C with Raylib** and compiled to **WebAssembly**, so it plays in any web browser.

Built for the *Principles of Programming* module (BSc Computer Science, UWE Bristol).

**▶ [Play it in your browser](https://sojib-hosain.github.io/Ski-Runner/)**: press Enter to start, and Space or Up to jump.

## Gameplay

Ski downhill for as long as you can, jumping over trees and rocks and collecting coins. Hold jump for extra height. Your best score is saved as the high score.

## Technical highlights

- **Physics:** gravity, variable-height jumps (hold to boost) and frame-time clamping for smooth movement at any frame rate
- **Game-state machine:** menu → playing → game over
- **Procedural world:** obstacles and coins spawn ahead of the player and are recycled once off-screen (fixed-size object pools, no dynamic allocation)
- **Collision detection:** rectangle–rectangle checks for obstacles and circle–rectangle checks for coins
- **Visuals:** multi-layer parallax mountains and hills, a sky gradient, light rays, fog and a particle snow system
- **Persistence:** high score saved to file
- **WebAssembly build:** compiled with Emscripten to run in the browser

## Build and run

```bash
# Build to WebAssembly (outputs to out/)
/opt/pop/bin/build-wasm.sh src/main.c

# Serve locally
/opt/pop/bin/run-wasm.sh
```

The build scripts come from the module's development environment. To build natively instead, install [Raylib](https://www.raylib.com/) and compile `src/main.c` with your C compiler, linking against Raylib.

## Tech

C · Raylib · WebAssembly (Emscripten)

## Author

**Md Sojib Hosain**, BSc (Hons) Computer Science, UWE Bristol
[LinkedIn](https://www.linkedin.com/in/sojib-hosain) · [GitHub](https://github.com/Sojib-Hosain)
