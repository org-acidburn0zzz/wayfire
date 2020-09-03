#include <wayfire/plugins/vswitch.hpp>
#include <wayfire/plugin.hpp>
#include <linux/input.h>


class vswitch : public wf::plugin_interface_t
{
  private:
    wf::activator_callback callback_left, callback_right, callback_up, callback_down;
    wf::activator_callback callback_win_left, callback_win_right, callback_win_up,
        callback_win_down;

    /**
     * Adapter around the general algorithm, so that our own stop function is
     * called.
     */
    class vswitch_plugin_algorithm : public wf::vswitch::workspace_switch_t
    {
      public:
        vswitch_plugin_algorithm(wf::output_t *output,
            std::function<void()> on_done) : workspace_switch_t(output)
        {
            this->on_done = on_done;
        }

        void stop_switch(bool normal_exit) override
        {
            workspace_switch_t::stop_switch(normal_exit);
            on_done();
        }

      private:
        std::function<void()> on_done;
    };

    std::unique_ptr<vswitch_plugin_algorithm> algorithm;

  public:
    wayfire_view get_top_view()
    {
        auto ws    = output->workspace->get_current_workspace();
        auto views = output->workspace->get_views_on_workspace(ws,
            wf::LAYER_WORKSPACE);

        return views.empty() ? nullptr : views[0];
    }

    void init()
    {
        grab_interface->name = "vswitch";
        /* note: workspace_wall_t sets a custom renderer, so we
         * need the capability for that */
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP |
            wf::CAPABILITY_CUSTOM_RENDERER;
        grab_interface->callbacks.cancel = [=] ()
        {
            algorithm->stop_switch(false);
        };

        callback_left = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(-1, 0);
        };
        callback_right = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(1, 0);
        };
        callback_up = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(0, -1);
        };
        callback_down = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(0, 1);
        };

        callback_win_left = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(-1, 0, get_top_view());
        };
        callback_win_right = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(1, 0, get_top_view());
        };
        callback_win_up = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(0, -1, get_top_view());
        };
        callback_win_down = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(0, 1, get_top_view());
        };

        wf::option_wrapper_t<wf::activatorbinding_t> binding_left{
            "vswitch/binding_left"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_right{
            "vswitch/binding_right"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_up{"vswitch/binding_up"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_down{
            "vswitch/binding_down"};

        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_left{
            "vswitch/binding_win_left"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_right{
            "vswitch/binding_win_right"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_up{
            "vswitch/binding_win_up"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_down{
            "vswitch/binding_win_down"};

        output->add_activator(binding_left, &callback_left);
        output->add_activator(binding_right, &callback_right);
        output->add_activator(binding_up, &callback_up);
        output->add_activator(binding_down, &callback_down);

        output->add_activator(binding_win_left, &callback_win_left);
        output->add_activator(binding_win_right, &callback_win_right);
        output->add_activator(binding_win_up, &callback_win_up);
        output->add_activator(binding_win_down, &callback_win_down);

        output->connect_signal("set-workspace-request", &on_set_workspace_request);
        output->connect_signal("view-disappeared", &on_grabbed_view_disappear);
        algorithm = std::make_unique<vswitch_plugin_algorithm>(output,
            [=] () { output->deactivate_plugin(grab_interface); });
    }

    inline bool is_active()
    {
        return output->is_plugin_active(grab_interface->name);
    }

    bool add_direction(int x, int y, wayfire_view view = nullptr)
    {
        if (!x && !y)
        {
            return false;
        }

        if (!is_active() && !start_switch())
        {
            return false;
        }

        if (view && (view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            view = nullptr;
        }

        algorithm->set_overlay_view(view);

        /* Make sure that when we add this direction, we won't go outside
         * of the workspace grid */
        auto target = algorithm->get_target_workspace();
        auto wsize  = output->workspace->get_workspace_grid_size();
        int tvx     = wf::clamp(target.x + x, 0, wsize.width - 1);
        int tvy     = wf::clamp(target.y + y, 0, wsize.height - 1);
        algorithm->set_target_workspace({tvx, tvy});

        return true;
    }

    wf::signal_connection_t on_grabbed_view_disappear = [=] (
        wf::signal_data_t *data)
    {
        if (get_signaled_view(data) == algorithm->get_overlay_view())
        {
            algorithm->set_overlay_view(nullptr);
        }
    };

    wf::signal_connection_t on_set_workspace_request = [=] (
        wf::signal_data_t *data)
    {
        auto ev = static_cast<wf::workspace_change_request_signal*>(data);
        if (is_active())
        {
            auto current_target = algorithm->get_target_workspace();
            ev->carried_out = add_direction(ev->new_viewport.x - current_target.x,
                ev->new_viewport.y - current_target.y);
        } else
        {
            ev->carried_out = add_direction(ev->new_viewport.x - ev->old_viewport.x,
                ev->new_viewport.y - ev->old_viewport.y);
        }
    };

    bool start_switch()
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        algorithm->start_switch();

        return true;
    }

    void fini()
    {
        if (is_active())
        {
            algorithm->stop_switch(false);
        }

        output->rem_binding(&callback_left);
        output->rem_binding(&callback_right);
        output->rem_binding(&callback_up);
        output->rem_binding(&callback_down);

        output->rem_binding(&callback_win_left);
        output->rem_binding(&callback_win_right);
        output->rem_binding(&callback_win_up);
        output->rem_binding(&callback_win_down);
    }
};

DECLARE_WAYFIRE_PLUGIN(vswitch);
