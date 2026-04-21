#pragma once

#include <GLFW/glfw3.h>

class ImGuiApplication
{
public:
    ImGuiApplication(const char* initWindowTitle = nullptr);
    ~ImGuiApplication();

    // ------------------------------------------------------------------------
    // mainLoop() - Invoke this in your main() function to get the program run

    void mainLoop();

    // ------------------------------------------------------------------------
    // User Actions - must be implemented by user

    virtual void onInit() = 0;
    virtual void onImGuiDisplay() = 0;
    virtual void onTerminate() = 0;

    // ------------------------------------------------------------------------
    // Utilities

    GLFWwindow* getWindow() { return this->window; }
    void setWindowTitle(const char* newTitle);
    void setWindowShouldClose(bool shouldClose);
    bool getIfGlfwOK() { return isGlfwOK; }

private:
    // ------------------------------------------------------------------------
    // Status & Instances

    // Mark if Glfw loaded successfully (backend + window).
    // NOTICE: All actions must check this flag before doing everything!
    bool isGlfwOK;  

    GLFWwindow *window; // Glfw Window handle
    float mainScale;    // Main scale

    // ------------------------------------------------------------------------
    // Workflows

    bool _initGlfw(const char* initWindowTitle);
    void _initImGui();

    void _cleanupImGui();
    void _cleanupGlfw();
};
