#pragma once

// Forward decls.
class ManifoldApp;

class DnDHandler
{
public:
    DnDHandler(ManifoldApp* app_) : app(app_)
    {}

    void registerDropHandler();

private:
    ManifoldApp* app;
};
