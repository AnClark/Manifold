#include "DnDHandler.hpp"
#include "Main.hpp"

#include <utils/NFDIncludes.h>

void DnDHandler::registerDropHandler()
{
    // GLFW callbacks only accept plain C function pointers (no captures).
    // Store `this` in the window user pointer so the non-capturing lambda can retrieve it.
    glfwSetWindowUserPointer(app->getWindow(), app);

    // `paths` is the file / folder paths transfered with DnD action.    
    glfwSetDropCallback(app->getWindow(), [](GLFWwindow* window, int count, const char** paths) {
        if (count <= 0 || !paths)
        {
            LOG_FATAL("DnDHandler", "GLFW Drop Callback misbehaves");
            return;
        }

        auto* app = static_cast<ManifoldApp*>(glfwGetWindowUserPointer(window));

        switch (app->uiState) {
            case pUIFiles:
                app->uiFiles.system_DropHandler(count, paths);
                break;
            case pUIActions:
            case pUITasks:
            case pUILog:
                break;
        }
    });
}
