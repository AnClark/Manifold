#pragma once

#include <GLFW/glfw3.h>

/**
 * @brief Base class for a Dear ImGui application backed by GLFW3 + OpenGL2.
 *
 * @details
 * `ImGuiApplication` wraps the full GLFW3 / Dear ImGui initialization, render
 * loop, and teardown into a single, reusable class.  Derive from it, implement
 * the three pure-virtual hooks (`onInit`, `onImGuiDisplay`, `onTerminate`), and
 * call `mainLoop()` from `main()` — that is all that is needed to get a working
 * Dear ImGui window.
 *
 * ### Lifecycle
 * ```
 * Constructor
 *   └─ _initGlfw()      — creates the GLFW window (DPI-aware, 1280×800 logical)
 *   └─ _initImGui()     — sets up Dear ImGui context, style, and backends
 * mainLoop()
 *   └─ onInit()         ← override  (resource allocation, font loading, …)
 *   └─ [render loop]
 *        └─ onImGuiDisplay() ← override  (all Dear ImGui calls go here)
 *   └─ onTerminate()    ← override  (request worker cancellations, cleanup, …)
 * Destructor
 *   └─ _cleanupImGui()
 *   └─ _cleanupGlfw()
 * ```
 *
 * @note Virtual dispatch is **not** available inside a constructor or
 *       destructor in C++.  Therefore `onInit()` is invoked at the very
 *       beginning of `mainLoop()`, and `onTerminate()` is invoked at the very
 *       end, so that the fully-constructed derived object is in scope.
 *
 * ### Features
 * - DPI-aware window sizing via `ImGui_ImplGlfw_GetContentScaleForMonitor`.
 * - VSync enabled by default (`glfwSwapInterval(1)`).
 * - Keyboard and gamepad navigation flags enabled by default.
 * - Dear ImGui Dark style applied by default.
 * - Window minimization is handled gracefully (sleep while iconified).
 *
 * ### Minimal example
 * @code{.cpp}
 * class MyApp : public ImGuiApplication {
 * public:
 *     MyApp() : ImGuiApplication("My Window") {}
 * protected:
 *     void onInit()         override { /* load resources * / }
 *     void onImGuiDisplay() override { ImGui::ShowDemoWindow(); }
 *     void onTerminate()    override { /* release resources * / }
 * };
 *
 * int main() {
 *     MyApp app;
 *     app.mainLoop();
 * }
 * @endcode
 */
class ImGuiApplication
{
public:
    /**
     * @brief Constructs the application, initializing GLFW3 and Dear ImGui.
     *
     * @param initWindowTitle  Title shown in the OS window title bar.
     *                         If `nullptr`, a generic default title is used.
     *
     * @note  `onInit()` is **not** called here.  It is called at the start of
     *        `mainLoop()` once the object is fully constructed.
     */
    ImGuiApplication(const char* initWindowTitle = nullptr);

    /**
     * @brief Destructor — shuts down the Dear ImGui backends and GLFW.
     *
     * @note  `onTerminate()` is **not** called here.  It is called at the end
     *        of `mainLoop()` before the object begins destruction.
     */
    ~ImGuiApplication();

    // ------------------------------------------------------------------------
    // Run

    /**
     * @brief Enters the main render loop.
     *
     * @details Call this from `main()`.  The function blocks until the window
     * is closed or `setWindowShouldClose(true)` is called.
     *
     * Internally the loop:
     * 1. Calls `onInit()` once before the first frame.
     * 2. Polls GLFW events each frame and skips rendering while minimized.
     * 3. Starts a new Dear ImGui frame and calls `onImGuiDisplay()`.
     * 4. Renders the ImGui draw data via the OpenGL2 backend.
     * 5. Calls `onTerminate()` once after the window close is requested.
     *
     * If GLFW failed to initialize (see `getIfGlfwOK()`), this function
     * returns immediately without doing anything.
     */
    void mainLoop();

    // ------------------------------------------------------------------------
    // User hooks — must be implemented by the derived class

    /**
     * @brief Called once at application startup, before the first frame.
     *
     * Override this to load fonts, open files, start worker threads, register
     * event handlers, and perform any other one-time initialization that
     * requires a fully constructed derived object.
     */
    virtual void onInit() = 0;

    /**
     * @brief Called every frame to build and submit the Dear ImGui UI.
     *
     * All `ImGui::*` calls that draw widgets or windows should be placed here.
     * The ImGui frame has already been started when this is called; do **not**
     * call `ImGui::NewFrame()` or `ImGui::Render()` yourself.
     */
    virtual void onImGuiDisplay() = 0;

    /**
     * @brief Called once just before the application exits.
     *
     * Override this to signal worker threads to stop, flush state to disk, or
     * release any resources acquired in `onInit()`.
     *
     * @note At this point the GLFW window is still open and rendering has
     *       stopped.  Worker threads should be signalled here; their join is
     *       handled by their own destructors after `onTerminate()` returns.
     */
    virtual void onTerminate() = 0;

    // ------------------------------------------------------------------------
    // Utilities

    /**
     * @brief Returns the underlying GLFW window handle.
     * @return Pointer to the `GLFWwindow`, or `nullptr` if GLFW failed to
     *         initialize.
     */
    GLFWwindow* getWindow() { return this->window; }

    /**
     * @brief Changes the OS window title bar text at runtime.
     * @param newTitle  New UTF-8 title string.  Must not be `nullptr`.
     *
     * Has no effect if GLFW did not initialize successfully.
     */
    void setWindowTitle(const char* newTitle);

    /**
     * @brief Requests the GLFW window to close (or cancels a close request).
     *
     * @param shouldClose  Pass `true` to signal that the window should close
     *                     at the end of the current (or next) frame.  Pass
     *                     `false` to cancel a pending close request.
     *
     * @warning Ensure all worker threads have been signalled to stop before
     *          calling this with `true`, so that `onTerminate()` can complete
     *          cleanly.
     *
     * Has no effect if GLFW did not initialize successfully.
     */
    void setWindowShouldClose(bool shouldClose);

    /**
     * @brief Returns whether GLFW (and the window) initialized successfully.
     * @return `true` if GLFW and the window are ready; `false` otherwise.
     *
     * When this returns `false`, `mainLoop()` returns immediately and all
     * other methods that touch the window become no-ops.
     */
    bool getIfGlfwOK() { return isGlfwOK; }

private:
    // ------------------------------------------------------------------------
    // Status & instances

    /** @brief `true` when GLFW and the window were created without error. */
    bool isGlfwOK;

    /** @brief Opaque GLFW window handle. */
    GLFWwindow *window;

    /** @brief Content-scale factor queried from the primary monitor (DPI). */
    float mainScale;

    // ------------------------------------------------------------------------
    // Internal initialization / teardown helpers

    /**
     * @brief Initializes GLFW, creates the window, and makes the GL context current.
     * @param initWindowTitle  Window title (may be `nullptr`).
     * @return `true` on success; `false` if `glfwInit()` or window creation failed.
     */
    bool _initGlfw(const char* initWindowTitle);

    /**
     * @brief Sets up the Dear ImGui context, style, and GLFW/OpenGL2 backends.
     *
     * Must only be called after `_initGlfw()` succeeds.
     */
    void _initImGui();

    /** @brief Shuts down the Dear ImGui GLFW/OpenGL2 backends and destroys the context. */
    void _cleanupImGui();

    /** @brief Destroys the GLFW window and terminates the GLFW library. */
    void _cleanupGlfw();
};
