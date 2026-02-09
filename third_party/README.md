# Third-Party Overrides

This folder is only needed when you do **not** want CMake to auto-fetch Nuklear.

Default behavior (`DUNGEONEER_NUKLEAR_AUTO_FETCH=ON`) fetches Nuklear + GLFW
for the optional Nuklear demo automatically.

If you disable auto-fetch, place these files directly in this folder:

- `third_party/nuklear.h`
- `third_party/nuklear_glfw_gl2.h`

Then configure with:

```sh
cmake -S . -B build -DDUNGEONEER_BUILD_NUKLEAR_APP=ON -DDUNGEONEER_NUKLEAR_AUTO_FETCH=OFF
cmake --build build
```
