#pragma once

#include <wayfire/plugins/common/view-change-viewport-signal.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>
#include <wayfire/plugins/common/workspace-wall.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/view.hpp>
#include <wayfire/view-transform.hpp>
#include <cmath>

namespace wf
{
namespace vswitch
{
using namespace animation;
class workspace_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t dx{*this};
    timed_transition_t dy{*this};
};

/**
 * Represents the action of switching workspaces with the vswitch algorithm.
 *
 * The workspace is actually switched at the end of the animation
 */
class workspace_switch_t
{
  public:
    /**
     * Initialize the workspace switch process.
     *
     * @param output The output the workspace switch happens on.
     */
    workspace_switch_t(output_t *output)
    {
        this->output = output;
        wall = std::make_unique<workspace_wall_t>(output);
        wall->connect_signal("frame", &on_frame);

        animation = workspace_animation_t{
            wf::option_wrapper_t<int>{"vswitch/duration"}
        };
    }

    /**
     * Initialize switching animation.
     * At this point, the calling plugin needs to have the custom renderer
     * ability set.
     */
    virtual void start_switch()
    {
        /* Setup wall */
        wall->set_gap_size(gap);
        wall->set_viewport(wall->get_workspace_rectangle(
            output->workspace->get_current_workspace()));
        wall->set_background_color(background_color);
        wall->start_output_renderer();

        /* Setup animation */
        animation.dx.set(0, 0);
        animation.dy.set(0, 0);
        animation.start();
    }

    /**
     * Start workspace switch animation towards the given workspace.
     *
     * @param workspace The new target workspace.
     */
    virtual void set_target_workspace(point_t workspace)
    {
        point_t cws = output->workspace->get_current_workspace();
        animation.dx.restart_with_end(workspace.x - cws.x);
        animation.dy.restart_with_end(workspace.y - cws.y);
        animation.start();
    }

    /** @return The current target workspace. */
    virtual point_t get_target_workspace()
    {
        point_t ws = output->workspace->get_current_workspace();

        return {
            (int)std::round(ws.x + animation.dx.end),
            (int)std::round(ws.y + animation.dy.end),
        };
    }

    /**
     * Set the overlay view. It will be hidden from the normal workspace layers
     * and shown on top of the workspace wall. The overlay view's position is
     * not animated together with the workspace transition, but its alpha is.
     *
     * Note: if the view disappears, the caller is responsible for resetting the
     * overlay view.
     *
     * @param view The desired overlay view, or NULL if the overlay view needs
     *   to be unset.
     */
    virtual void set_overlay_view(wayfire_view view)
    {
        if (this->overlay_view == view)
        {
            /* Nothing to do */
            return;
        }

        /* Reset old view */
        if (this->overlay_view)
        {
            overlay_view->set_visible(true);
            overlay_view->pop_transformer(vswitch_view_transformer_name);
        }

        /* Set new view */
        this->overlay_view = view;
        if (view)
        {
            view->add_transformer(std::make_unique<wf::view_2D>(view),
                vswitch_view_transformer_name);
            view->set_visible(false); // view is rendered as overlay
        }
    }

    /** @return the current overlay view, might be NULL. */
    virtual wayfire_view get_overlay_view()
    {
        return this->overlay_view;
    }

    /**
     * Called automatically when the workspace switch animation is done.
     * By default, this stops the animation.
     *
     * @param normal_exit Whether the operation has ended because of animation
     *   running out, in which case the workspace and the overlay view are
     *   adjusted, and otherwise not.
     */
    virtual void stop_switch(bool normal_exit)
    {
        if (normal_exit)
        {
            adjust_overlay_view_switch_done();
            output->workspace->set_workspace(get_target_workspace());
        }

        wall->stop_output_renderer(true);
    }

    virtual ~workspace_switch_t()
    {}

  protected:
    option_wrapper_t<int> gap{"vswitch/gap"};
    option_wrapper_t<color_t> background_color{"vswitch/background"};
    workspace_animation_t animation;

    output_t *output;
    std::unique_ptr<workspace_wall_t> wall;

    const std::string vswitch_view_transformer_name = "vswitch-transformer";
    wayfire_view overlay_view;

    wf::signal_connection_t on_frame = [=] (wf::signal_data_t *data)
    {
        render_frame(static_cast<wall_frame_event_t*>(data)->target);
    };

    virtual void render_overlay_view(const framebuffer_t& fb)
    {
        if (!overlay_view)
        {
            return;
        }

        double progress = animation.progress();
        auto tr = dynamic_cast<wf::view_2D*>(overlay_view->get_transformer(
            vswitch_view_transformer_name).get());

        static constexpr double smoothing_in     = 0.4;
        static constexpr double smoothing_out    = 0.2;
        static constexpr double smoothing_amount = 0.5;

        if (progress <= smoothing_in)
        {
            tr->alpha = 1.0 - (smoothing_amount / smoothing_in) * progress;
        } else if (progress >= 1.0 - smoothing_out)
        {
            tr->alpha = 1.0 - (smoothing_amount / smoothing_out) * (1.0 - progress);
        } else
        {
            tr->alpha = smoothing_amount;
        }

        overlay_view->render_transformed(fb, fb.geometry);
    }

    virtual void render_frame(const framebuffer_t& fb)
    {
        auto start = wall->get_workspace_rectangle(
            output->workspace->get_current_workspace());
        auto size = output->get_screen_size();
        geometry_t viewport = {
            (int)std::round(animation.dx * (size.width + gap) + start.x),
            (int)std::round(animation.dy * (size.height + gap) + start.y),
            start.width,
            start.height,
        };
        wall->set_viewport(viewport);

        render_overlay_view(fb);
        output->render->schedule_redraw();

        if (!animation.running())
        {
            stop_switch(true);
        }
    }

    /**
     * Move the overlay view to the target workspace and unset it.
     */
    virtual void adjust_overlay_view_switch_done()
    {
        if (!overlay_view)
        {
            return;
        }

        auto output_g = output->get_relative_geometry();
        overlay_view->pop_transformer(vswitch_view_transformer_name);
        auto wm = overlay_view->get_wm_geometry();
        overlay_view->move(wm.x + animation.dx.end * output_g.width,
            wm.y + animation.dy.end * output_g.height);
        output->workspace->bring_to_front(overlay_view);

        view_change_viewport_signal data;
        data.view = overlay_view;
        data.from = output->workspace->get_current_workspace();
        data.to   = get_target_workspace();
        output->emit_signal("view-change-viewport", &data);

        set_overlay_view(nullptr);
    }
};
}
}
