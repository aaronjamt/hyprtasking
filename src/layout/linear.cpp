#include "linear.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/managers/AnimationManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>
#include <ranges>

#include "../config.hpp"
#include "../globals.hpp"
#include "../render.hpp"
#include "layout_base.hpp"

using Hyprutils::Utils::CScopeGuard;

HTLayoutLinear::HTLayoutLinear(VIEWID new_view_id) : HTLayoutBase(new_view_id) {
    g_pAnimationManager->createAnimation(
        0.f,
        scroll_offset,
        g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE
    );
    g_pAnimationManager->createAnimation(
        0.f,
        view_offset,
        g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE
    );
    g_pAnimationManager->createAnimation(
        0.f,
        blur_strength,
        g_pConfigManager->getAnimationPropertyConfig("fadeIn"),
        AVARDAMAGE_NONE
    );
    g_pAnimationManager->createAnimation(
        0.f,
        dim_opacity,
        g_pConfigManager->getAnimationPropertyConfig("fadeDim"),
        AVARDAMAGE_NONE
    );

    init_position();
}

std::string HTLayoutLinear::layout_name() {
    return "linear";
}

void HTLayoutLinear::close_open_lerp(float perc) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const float HEIGHT = HTConfig::value<Hyprlang::FLOAT>("linear:height") * monitor->scale;

    view_offset->resetAllCallbacks();
    blur_strength->resetAllCallbacks();
    dim_opacity->resetAllCallbacks();
    view_offset->setValueAndWarp(std::lerp(0.0, HEIGHT, perc));
    blur_strength->setValueAndWarp(std::lerp(0.0, 2.0, perc));
    dim_opacity->setValueAndWarp(std::lerp(0.0, 0.4, perc));
}

void HTLayoutLinear::on_show(CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            view_offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const float HEIGHT = HTConfig::value<Hyprlang::FLOAT>("linear:height") * monitor->scale;
    *view_offset = HEIGHT;
    *blur_strength = 2.0;
    *dim_opacity = 0.4;
}

void HTLayoutLinear::on_hide(CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            view_offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    *view_offset = 0;
    *blur_strength = 0;
    *dim_opacity = 0;
}

void HTLayoutLinear::on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            scroll_offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const float GAP_SIZE = HTConfig::value<Hyprlang::FLOAT>("gap_size") * monitor->scale;

    const PHLWORKSPACE new_ws = g_pCompositor->getWorkspaceByID(new_id);
    if (new_ws == nullptr)
        return;

    build_overview_layout(HT_VIEW_ANIMATING);

    const float cur_screen_min_x = overview_layout[new_id].box.x - GAP_SIZE;
    const float cur_screen_max_x =
        overview_layout[new_id].box.x + overview_layout[new_id].box.w + GAP_SIZE;

    if (cur_screen_min_x < 0) {
        *scroll_offset = scroll_offset->value() - cur_screen_min_x;
    } else if (cur_screen_max_x > monitor->vecTransformedSize.x) {
        *scroll_offset =
            scroll_offset->value() - (cur_screen_max_x - monitor->vecTransformedSize.x);
    }
}

bool HTLayoutLinear::on_mouse_axis(double delta) {
    if (!should_manage_mouse())
        return false;

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return false;

    const float GAP_SIZE = HTConfig::value<Hyprlang::FLOAT>("gap_size") * monitor->scale;

    const float total_ws_width =
        (overview_layout.size() * (GAP_SIZE + calculate_ws_box(0, 0, HT_VIEW_ANIMATING).w))
        + GAP_SIZE;

    // Stay at 0 if not long enough
    if (total_ws_width < monitor->vecTransformedSize.x) {
        *scroll_offset = 0.;
        return true;
    }

    double new_offset = scroll_offset->goal()
        + delta * HTConfig::value<Hyprlang::FLOAT>("linear:scroll_speed") * -10.f;

    const float max_x = new_offset
        + (overview_layout.size() * (GAP_SIZE + calculate_ws_box(0, 0, HT_VIEW_ANIMATING).w))
        + GAP_SIZE;

    // Snap to left
    if (new_offset > 0.)
        new_offset = 0.;

    // Snap to right
    if (max_x < monitor->vecTransformedSize.x)
        new_offset = new_offset + (monitor->vecTransformedSize.x - max_x);

    *scroll_offset = new_offset;
    return true;
}

bool HTLayoutLinear::should_manage_mouse() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return 1;

    const float HEIGHT = HTConfig::value<Hyprlang::FLOAT>("linear:height") * monitor->scale;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    CBox scaled_view_box = {
        Vector2D {0.f, monitor->vecTransformedSize.y - view_offset->value()},
        {(float)monitor->vecTransformedSize.x, (float)HEIGHT}
    };

    return scaled_view_box.scale(1 / monitor->scale)
        .translate(monitor->vecPosition)
        .containsPoint(mouse_coords);
}

bool HTLayoutLinear::should_render_window(PHLWINDOW window) {
    bool ori_result = HTLayoutBase::should_render_window(window);

    const PHLMONITOR monitor = get_monitor();
    if (window == nullptr || monitor == nullptr)
        return ori_result;

    if (window == g_pInputManager->currentlyDraggedWindow.lock())
        return false;

    if (rendering_standard_ws)
        return ori_result;

    PHLWORKSPACE workspace = window->m_pWorkspace;
    if (workspace == nullptr)
        return false;

    CBox window_box = get_global_window_box(window, window->workspaceID());
    if (window_box.empty())
        return false;
    if (window_box.intersection(monitor->logicalBox()).empty())
        return false;

    return ori_result;
}

float HTLayoutLinear::drag_window_scale() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return 1;

    if (should_manage_mouse())
        return calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->vecTransformedSize.x;

    return 1;
}

void HTLayoutLinear::init_position() {
    build_overview_layout(HT_VIEW_CLOSED);

    scroll_offset->setValueAndWarp(0);
    view_offset->setValueAndWarp(0);
}

CBox HTLayoutLinear::calculate_ws_box(int x, int y, HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    const float HEIGHT = HTConfig::value<Hyprlang::FLOAT>("linear:height") * monitor->scale;
    const float GAP_SIZE = HTConfig::value<Hyprlang::FLOAT>("gap_size") * monitor->scale;

    if (HEIGHT < 0 || HEIGHT > monitor->vecTransformedSize.y)
        fail_exit("Linear layout height {} is taller than monitor size", HEIGHT);

    if (GAP_SIZE < 0 || GAP_SIZE > HEIGHT / 2.f)
        fail_exit("Invalid gap_size {} for linear layout", GAP_SIZE);

    float use_view_offset = view_offset->value();
    if (stage == HT_VIEW_CLOSED)
        use_view_offset = 0;
    else if (stage == HT_VIEW_OPENED)
        use_view_offset = HEIGHT;

    const float ws_height = HEIGHT - 2 * GAP_SIZE;
    const float ws_width =
        ws_height * monitor->vecTransformedSize.x / monitor->vecTransformedSize.y;

    const float ws_x = scroll_offset->value() + (x * (GAP_SIZE + ws_width) + GAP_SIZE);
    const float ws_y = monitor->vecTransformedSize.y - use_view_offset + GAP_SIZE;
    return CBox {ws_x, ws_y, ws_width, ws_height};
}

void HTLayoutLinear::build_overview_layout(HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    overview_layout.clear();

    std::vector<WORKSPACEID> monitor_workspaces;
    for (PHLWORKSPACE workspace : g_pCompositor->m_vWorkspaces) {
        if (workspace == nullptr)
            continue;
        if (workspace->m_pMonitor != monitor)
            continue;
        if (workspace->m_iID < 0)
            continue;
        monitor_workspaces.push_back(workspace->m_iID);
    }
    std::sort(monitor_workspaces.begin(), monitor_workspaces.end());

    WORKSPACEID big_id = monitor_workspaces.back();
    while (g_pCompositor->getWorkspaceByID(big_id) != nullptr)
        big_id++;
    monitor_workspaces.push_back(big_id);

    for (const auto& [x, ws_id] : monitor_workspaces | std::views::enumerate) {
        CBox ws_box = calculate_ws_box(x, 0, stage);
        overview_layout[ws_id] = {x, 0, ws_box};
    }
}

void HTLayoutLinear::render() {
    HTLayoutBase::render();
    CScopeGuard x([this] { post_render(); });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return;
    const PHLMONITOR monitor = par_view->get_monitor();
    if (monitor == nullptr)
        return;

    static auto PACTIVECOL = CConfigValue<Hyprlang::CUSTOMTYPE>("general:col.active_border");
    static auto PINACTIVECOL = CConfigValue<Hyprlang::CUSTOMTYPE>("general:col.inactive_border");

    auto* const ACTIVECOL = (CGradientValueData*)(PACTIVECOL.ptr())->getData();
    auto* const INACTIVECOL = (CGradientValueData*)(PINACTIVECOL.ptr())->getData();

    const float BORDERSIZE = HTConfig::value<Hyprlang::FLOAT>("border_size");
    const float HEIGHT = HTConfig::value<Hyprlang::FLOAT>("linear:height") * monitor->scale;

    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);

    g_pHyprRenderer->damageMonitor(monitor);
    g_pHyprOpenGL->m_RenderData.pCurrentMonData->blurFBShouldRender = true;

    // Do a dance with active workspaces: Hyprland will only properly render the
    // current active one so make the workspace active before rendering it, etc
    const PHLWORKSPACE start_workspace = monitor->activeWorkspace;
    start_workspace->startAnim(false, false, true);
    start_workspace->m_bVisible = false;

    const PHLWORKSPACE big_ws = monitor->activeWorkspace;

    rendering_standard_ws = true;
    monitor->activeWorkspace = big_ws;
    big_ws->startAnim(true, false, true);
    big_ws->m_bVisible = true;

    // use pixel size for geometry
    CBox mon_box = {{0, 0}, monitor->vecPixelSize};
    // Render the current workspace on the screen
    ((render_workspace_t)(render_workspace_hook->m_pOriginal))(
        g_pHyprRenderer.get(),
        monitor,
        big_ws,
        &time,
        mon_box
    );

    // add blur/dim over the original workspace
    CRectPassElement::SRectData blur_data;
    blur_data.color = CHyprColor(0, 0, 0, dim_opacity->value());
    blur_data.box = mon_box;
    blur_data.blur = (bool)HTConfig::value<Hyprlang::INT>("linear:blur");
    blur_data.blurA = blur_strength->value();
    g_pHyprRenderer->m_sRenderPass.add(makeShared<CRectPassElement>(blur_data));

    big_ws->startAnim(false, false, true);
    big_ws->m_bVisible = false;
    rendering_standard_ws = false;

    CBox view_box = {
        {0.f, monitor->vecTransformedSize.y - view_offset->value()},
        {(float)monitor->vecTransformedSize.x, (float)HEIGHT}
    };

    CRectPassElement::SRectData data;
    data.color = CHyprColor {HTConfig::value<Hyprlang::INT>("bg_color")}.stripA();
    data.box = view_box;
    g_pHyprRenderer->m_sRenderPass.add(makeShared<CRectPassElement>(data));

    build_overview_layout(HT_VIEW_ANIMATING);

    CBox global_mon_box = {monitor->vecPosition, monitor->vecTransformedSize};
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        // Could be nullptr, in which we render only layers
        const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);

        // renderModif translation used by renderWorkspace is weird so need
        // to scale the translation up as well. Geometry is also calculated from pixel size and not transformed size??
        CBox render_box = {
            {ws_layout.box.pos() / (ws_layout.box.w / monitor->vecTransformedSize.x)},
            ws_layout.box.size()
        };
        if (monitor->transform % 2 == 1)
            std::swap(render_box.w, render_box.h);

        CBox global_box = {ws_layout.box.pos() + monitor->vecPosition, ws_layout.box.size()};
        if (global_box.intersection(global_mon_box).empty())
            continue;

        const CGradientValueData border_col = workspace == big_ws ? *ACTIVECOL : *INACTIVECOL;
        CBox border_box = ws_layout.box;

        CBorderPassElement::SBorderData data;
        data.box = border_box;
        data.grad1 = border_col;
        data.borderSize = BORDERSIZE;
        g_pHyprRenderer->m_sRenderPass.add(makeShared<CBorderPassElement>(data));

        if (workspace != nullptr) {
            monitor->activeWorkspace = workspace;
            workspace->startAnim(true, false, true);
            workspace->m_bVisible = true;

            ((render_workspace_t)(render_workspace_hook->m_pOriginal))(
                g_pHyprRenderer.get(),
                monitor,
                workspace,
                &time,
                render_box
            );

            workspace->startAnim(false, false, true);
            workspace->m_bVisible = false;
        } else {
            // If pWorkspace is null, then just render the layers
            ((render_workspace_t)(render_workspace_hook->m_pOriginal))(
                g_pHyprRenderer.get(),
                monitor,
                workspace,
                &time,
                render_box
            );
        }
    }

    monitor->activeWorkspace = start_workspace;
    start_workspace->startAnim(true, false, true);
    start_workspace->m_bVisible = true;

    // Render dragged window at mouse cursor
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;
    const PHLWINDOW dragged_window = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragged_window == nullptr)
        return;
    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const CBox window_box = dragged_window->getWindowMainSurfaceBox()
                                .translate(-mouse_coords)
                                .scale(cursor_view->layout->drag_window_scale())
                                .translate(mouse_coords);
    if (!window_box.intersection(monitor->logicalBox()).empty())
        render_window_at_box(dragged_window, monitor, &time, window_box);
}
