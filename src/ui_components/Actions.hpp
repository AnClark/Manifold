#pragma once

#include "config/FilenameConfig.hpp"

// Forward decls.
class ManifoldApp;

class UIComponents_Actions
{
public:
    UIComponents_Actions(ManifoldApp* app);

    void popup_OutputFileNameRule(FilenameTemplate& s_editTemplate, int& s_selectedTokenIdx);

private:
    ManifoldApp* app;
};
