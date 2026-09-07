#include "Application.hpp"

// NOTICE:
// This module is based on the official example of ImGui+OpenGL2+Glfw.

#include "imgui.h"
#include "imgui_impl_glfw.h"
#if defined(__APPLE__)
#include "ApplicationMetal.hpp"
#else
#include "imgui_impl_opengl2.h"
#endif
#include <stdio.h>

#include <GLFW/glfw3.h>


static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

ImGuiApplication::ImGuiApplication(const char* initWindowTitle)
    : window(NULL), isGlfwOK(false)
#if defined(__APPLE__)
    , metalRenderer(nullptr)
#endif
{
    // Initialize Glfw3
    if(!_initGlfw(initWindowTitle))
        return;
    
    // Initialize Dear ImGui stuff
    // ImGui backend stuffs does not have return values. Make sure they are not executed if Glfw failed to load.
    _initImGui();

    // ▲NOTE▲ Do NOT call onInit() here! Virtual functions don't dispatch to derived class in constructor.
    // onInit() will be called at the beginning of mainLoop() instead.
}

ImGuiApplication::~ImGuiApplication()
{
    // ▲NOTE▲ Do NOT call onTerminate() here! Virtual functions don't dispatch to derived class in destructor.
    // onTerminate() will be called at the end of mainLoop() instead.

    _cleanupImGui();
    _cleanupGlfw();
}

bool ImGuiApplication::_initGlfw(const char* initWindowTitle)
{
    glfwSetErrorCallback(glfw_error_callback);
    this->isGlfwOK = glfwInit();
    if (!isGlfwOK)
        return false;

#if defined(__APPLE__)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif
    // Create window with graphics context
    this->mainScale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
    this->window = glfwCreateWindow((int)(1280 * this->mainScale), (int)(800 * this->mainScale), initWindowTitle ? initWindowTitle : "Dear ImGui + Glfw3 Application", nullptr, nullptr);
    if (this->window == nullptr)
    {
        isGlfwOK = false;
        return false;
    }

#if !defined(__APPLE__)
    glfwMakeContextCurrent(this->window);
    glfwSwapInterval(1); // Enable vsync
#endif

    return true;
}

void ImGuiApplication::_initImGui()
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(this->mainScale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    // Note: FontScaleDpi is not a standard ImGuiStyle property. Font scale should be handled via io.FontGlobalScale or loading scaled fonts.

    // Setup Platform/Renderer backends
#if defined(__APPLE__)
    ImGui_ImplGlfw_InitForOther(window, true);  // Init Glfw for custom backend (Metal)
    this->metalRenderer = ImGuiApplicationMetal_Create(window);
    if (this->metalRenderer == nullptr)
        this->isGlfwOK = false;
#else
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();
#endif
}

void ImGuiApplication::_cleanupImGui()
{
    // Cleanup
#if defined(__APPLE__)
    ImGuiApplicationMetal_Destroy(this->metalRenderer);
    this->metalRenderer = nullptr;
#else
    ImGui_ImplOpenGL2_Shutdown();
#endif
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiApplication::_cleanupGlfw()
{
    if (!isGlfwOK)
        return;

    glfwDestroyWindow(this->window);
    glfwTerminate();
}

void ImGuiApplication::setWindowTitle(const char *newTitle)
{
    if (!isGlfwOK)
        return;
    
    glfwSetWindowTitle(this->window, newTitle);
}

void ImGuiApplication::setWindowShouldClose(bool shouldClose)
{
    // NOTICE: Applications should make sure everything is done before invoing this method!
    //         (Especially they should make sure every program thread is terminated)

    if (!isGlfwOK)
        return;

    glfwSetWindowShouldClose(this->window, int(shouldClose));
}

void ImGuiApplication::mainLoop()
{
    if (!isGlfwOK)
        return;

    // Call user's initialization method after the object is fully constructed
    onInit();

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    while (!glfwWindowShouldClose(this->window))
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();
        if (glfwGetWindowAttrib(this->window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // Start the Dear ImGui frame
    #if defined(__APPLE__)
        if (!ImGuiApplicationMetal_BeginFrame(this->metalRenderer, this->window))
            continue;
    #else
        ImGui_ImplOpenGL2_NewFrame();
    #endif
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Invoke user's own Dear ImGui display workflows
        onImGuiDisplay();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
    #if defined(__APPLE__)
        ImGuiApplicationMetal_RenderFrame(this->metalRenderer, ImGui::GetDrawData());
    #else
        glfwGetFramebufferSize(this->window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glDisable(GL_SCISSOR_TEST); // Ensure glClear covers the full framebuffer (not restricted by ImGui's scissor box)
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        // If you are using this code with non-legacy OpenGL header/contexts (which you should not, prefer using imgui_impl_opengl3.cpp!!),
        // you may need to backup/reset/restore other state, e.g. for current shader using the commented lines below.
        //GLint last_program;
        //glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
        //glUseProgram(0);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        //glUseProgram(last_program);

        glfwMakeContextCurrent(this->window);
        glfwSwapBuffers(this->window);
    #endif
    }

    // Call user's termination before the object starts to be destroyed
    onTerminate();
}
