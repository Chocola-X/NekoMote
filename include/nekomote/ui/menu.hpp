#pragma once

#include <nekomote/application/catalog.hpp>
#include <nekomote/application/controller.hpp>
#include <nekomote/ui/layout.hpp>
#include <nekomote/ui/state.hpp>

namespace nekomote::ui
{

struct MenuState
{
    SidebarState sidebar{};
    int fps_limit{60};
    bool locked{false};
    bool muted{false};
};

// Prepare all supported UI languages and catalog labels once after font/catalog
// creation. Call again when the catalog changes, with a valid backend context.
bool prepare_menu_text(FontMetrics& font, const application::AssetCatalog& catalog);

void build_sidebar(Context& ctx, Painter& painter, const application::AssetCatalog& catalog,
                   application::WallpaperController& controller, MenuState& menu,
                   int output_height);

} // namespace nekomote::ui
