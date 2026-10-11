#pragma once

#include <nekomote/application/controller.hpp>
#include <nekomote/audio/device.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/render_gl/ui_renderer.hpp>
#include <nekomote/ui/layout.hpp>

#include <nekomote/abi.h>

#include <filesystem>
#include <memory>
#include <string>

#if defined(NEKOMOTE_HAS_VULKAN)
#include <nekomote/render_vk/renderer.hpp>
#include <nekomote/render_vk/ui_renderer.hpp>
#endif

namespace nekomote::abi
{

enum class WallpaperBackend
{
    OpenGL,
    Vulkan
};

struct WallpaperPanelState
{
    ui::IconState icon{};
    ui::SidebarState sidebar{};
    int fps_limit{30};
    bool locked{false};
    bool muted{false};
    std::uint32_t icon_texture{0};
    bool ui_ready{false};
};

class WallpaperInstance
{
  public:
    explicit WallpaperInstance(render_gl::GlLoader loader);
#if defined(NEKOMOTE_HAS_VULKAN)
    explicit WallpaperInstance(const render_vk::DeviceContext& ctx);
    void set_vk_target(render_vk::Target target);
#endif
    ~WallpaperInstance();
    WallpaperInstance(const WallpaperInstance&) = delete;
    WallpaperInstance& operator=(const WallpaperInstance&) = delete;

    nekomote_error set_assets_dir(const std::filesystem::path& dir);
    nekomote_error set_no_save(bool value);
    nekomote_error set_muted(bool value);
    nekomote_error set_seed(std::uint32_t seed);
    nekomote_error set_output_size(int width, int height);
    nekomote_error set_paused(bool paused);
    void set_render_target_fbo(std::uint32_t fbo);
    void reset_gpu_resources();
    nekomote_error load_initial();
    nekomote_error load_model(const std::filesystem::path& model_path,
                              const std::filesystem::path& session_path);

    nekomote_error advance(double dt);
    nekomote_error render_frame();
    nekomote_error render_ui(float dt);

    nekomote_error pointer_move(double x, double y,
                                application::WallpaperController::PointerTime now);
    nekomote_error pointer_enter(double x, double y);
    nekomote_error pointer_leave();
    nekomote_error touch(float x, float y);
    nekomote_error mouse_button(int button, int action,
                                application::WallpaperController::PointerTime now);
    nekomote_error mouse_scroll(double amount);
    nekomote_error key(int key, int action);

    nekomote_error switch_character(int index);
    nekomote_error switch_background(int index);
    nekomote_error clear_background();
    nekomote_error fit();
    nekomote_error reset();

    int character_count() const;
    const char* character_name(int index) const;
    int background_count() const;
    const char* background_name(int index) const;
    int current_character() const;
    int current_background() const;
    const char* current_character_name() const;
    bool has_background() const;
    bool paused() const;
    float fade_alpha() const;

    int audio_state() const;
    float audio_rms() const;
    int audio_error() const;

    std::string last_error() const;

  private:
    void finish_candidate_gpu(const application::CandidateCpu& candidate);
    void create_audio_device(const application::CandidateCpu& candidate);
    void apply_background(const std::filesystem::path& path, double fade_duration, int bg_index);
    void draw_ui(float dt);
    ui::Painter* painter();
    bool ui_captures_mouse();
    void handle_switch_poll();
    void handle_background_poll();
    nekomote_error check_session();

    render_gl::GlLoader loader_;
    application::WallpaperController controller_;
    application::ControllerConfig controller_cfg_;

    WallpaperBackend backend_{WallpaperBackend::OpenGL};
    std::unique_ptr<render_gl::Renderer> renderer_;
    std::unique_ptr<render_gl::Renderer> previous_renderer_;
    std::unique_ptr<render_gl::Renderer> background_renderer_;
    std::unique_ptr<audio::Device> audio_device_;
    render_gl::UiRenderer ui_renderer_;
#if defined(NEKOMOTE_HAS_VULKAN)
    render_vk::DeviceContext vk_ctx_{};
    render_vk::Target vk_target_{};
    bool vk_target_set_{false};
    std::unique_ptr<render_vk::Renderer> vk_renderer_;
    std::unique_ptr<render_vk::Renderer> vk_previous_renderer_;
    std::unique_ptr<render_vk::Renderer> vk_background_renderer_;
    render_vk::UiRenderer vk_ui_renderer_;
#endif
    ui::Context ui_context_;
    WallpaperPanelState panel_;

    TextureId current_background_id_{0};
    std::filesystem::path current_background_path_;
    std::filesystem::path assets_dir_;
    mutable std::string name_buffer_;

    int output_width_{0};
    int output_height_{0};
    std::uint32_t render_target_fbo_{0};
    int last_render_width_{0};
    int last_render_height_{0};
    bool paused_{false};
    bool minimized_{false};
    bool output_unavailable_{false};
    bool assets_scanned_{false};
    bool initialized_{false};
    bool audio_failed_{false};
    std::string error_string_;
};

} // namespace nekomote::abi
