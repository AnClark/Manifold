# ImGuiApplication

A lightweight base class for building [Dear ImGui](https://github.com/ocornut/imgui) desktop applications with **GLFW3 + OpenGL2**.

Derive from `ImGuiApplication`, implement three virtual hooks, and call `mainLoop()` — the entire GLFW window creation, Dear ImGui context setup, render loop, and teardown are handled for you.

---

## Features

- **Zero boilerplate** — no manual `glfwInit`, `ImGui::NewFrame`, or backend setup in your code.
- **DPI-aware** — window size and UI scale are derived from the primary monitor's content scale at startup.
- **VSync enabled** by default (`glfwSwapInterval(1)`).
- **Safe virtual dispatch** — `onInit()` is called at the start of `mainLoop()` and `onTerminate()` at the end, guaranteeing full object construction/destruction around your code.
- **Minimization guard** — the render loop sleeps while the window is iconified, avoiding wasted CPU cycles.
- **Keyboard & gamepad navigation** enabled by default via `ImGuiConfigFlags`.

---

## Requirements

| Dependency | Notes |
|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | Core UI library |
| [GLFW3](https://www.glfw.org/) | Window + input backend |
| OpenGL 2.x (or any compatible driver) | Renderer backend (`imgui_impl_opengl2`) |

---

## Usage

### 1. Derive and implement the three hooks

```cpp
#include "Application.hpp"
#include "imgui.h"

class MyApp : public ImGuiApplication
{
public:
    MyApp() : ImGuiApplication("My Window") {}

protected:
    void onInit() override
    {
        // Called once before the first frame.
        // Load fonts, open files, start worker threads, etc.
        ImGui::GetIO().Fonts->AddFontDefault();
    }

    void onImGuiDisplay() override
    {
        // Called every frame — put all ImGui::* widget calls here.
        ImGui::ShowDemoWindow();
    }

    void onTerminate() override
    {
        // Called once after the window close is requested.
        // Signal threads to stop, flush data to disk, etc.
    }
};
```

### 2. Call `mainLoop()` from `main()`

```cpp
int main()
{
    MyApp app;
    app.mainLoop();  // blocks until the window is closed
}
```

### 3. Closing the window programmatically

```cpp
// From inside onImGuiDisplay() or from a worker callback:
setWindowShouldClose(true);
```

> **Warning:** Make sure all worker threads have been signalled to stop *before* calling `setWindowShouldClose(true)`, so that `onTerminate()` can complete cleanly.

---

## Application lifecycle

```
ImGuiApplication()
  ├─ _initGlfw()    — GLFW init, window creation (DPI-scaled 1280×800), vsync
  └─ _initImGui()   — ImGui context, dark style, GLFW/OpenGL2 backends

mainLoop()
  ├─ onInit()            ← your code (fonts, workers, handlers…)
  ├─ [per-frame loop]
  │    ├─ glfwPollEvents()
  │    ├─ ImGui::NewFrame()
  │    ├─ onImGuiDisplay()   ← your code (all widget calls)
  │    └─ ImGui::Render() + glfwSwapBuffers()
  └─ onTerminate()       ← your code (cancel workers, flush state…)

~ImGuiApplication()
  ├─ _cleanupImGui()
  └─ _cleanupGlfw()
```

---

## API reference

### Constructor

```cpp
ImGuiApplication(const char* initWindowTitle = nullptr);
```

Initializes GLFW and Dear ImGui.  If `initWindowTitle` is `nullptr`, a generic
default title is used.  Call `getIfGlfwOK()` afterwards if you need to verify
that initialization succeeded before calling `mainLoop()`.

### Destructor

Shuts down the Dear ImGui backends and GLFW.  `onTerminate()` is **not** called
here — it is already called by `mainLoop()`.

### `void mainLoop()`

Enters the render loop.  Blocks until the window is closed.  Calls `onInit()`
once at the start and `onTerminate()` once at the end.  Returns immediately if
GLFW is not OK.

### `virtual void onInit() = 0`

Override to perform one-time startup work (resource loading, font registration,
thread startup, event handler registration, …).

### `virtual void onImGuiDisplay() = 0`

Override to build the UI each frame.  The ImGui frame is already open; do not
call `ImGui::NewFrame()` or `ImGui::Render()` here.

### `virtual void onTerminate() = 0`

Override to perform cleanup before the application exits (cancel workers, save
state, …).

### `GLFWwindow* getWindow()`

Returns the raw GLFW window handle.  Useful for registering GLFW callbacks or
querying window state directly.

### `void setWindowTitle(const char* newTitle)`

Updates the OS window title at runtime.

### `void setWindowShouldClose(bool shouldClose)`

Signals GLFW that the window should (or should not) close after the current
frame.

### `bool getIfGlfwOK()`

Returns `true` if GLFW and the window initialized successfully.  When `false`,
`mainLoop()` is a no-op and all window-related methods are no-ops.

---

## Notes

- This implementation uses the **OpenGL2** Dear ImGui backend.  If your project
  requires OpenGL3+, replace `imgui_impl_opengl2` with `imgui_impl_opengl3` and
  update `Application.cpp` accordingly.
- The `ImGuiApplication` class is **not** thread-safe.  All Dear ImGui calls
  must happen on the main (render) thread.
