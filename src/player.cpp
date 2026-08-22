#include "luplay/player.hpp"

#include <iostream>
#include <filesystem>
#include <GLFW/glfw3.h>
#include "luplay/logger.hpp"

#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace {
// Callback for libmpv to query OpenGL extension function pointers via GLFW
void* get_mpv_proc_address(void* ctx, const char* name) {
    (void)ctx;
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

void on_mpv_render_update(void* ctx) {
    // Render update notification from libmpv
    (void)ctx;
}
} // namespace

namespace luplay {

Player::Player() = default;

Player::~Player() {
    shutdown();
}

bool Player::init() {
    if (mpv_) return true;

    mpv_ = mpv_create();
    if (!mpv_) {
        luplay::Logger::error("Failed to create mpv handle.");
        return false;
    }

    // Force hardware decoding (iGPU / GPU acceleration to save battery)
    if (mpv_set_option_string(mpv_, "hwdec", "auto-safe") < 0) {
        luplay::Logger::warning("Failed to set hwdec=auto-safe.");
    }

    // Set video output to libmpv embed mode
    mpv_set_option_string(mpv_, "vo", "libmpv");
    mpv_set_option_string(mpv_, "keep-open", "yes");

    if (mpv_initialize(mpv_) < 0) {
        luplay::Logger::error("Failed to initialize mpv instance.");
        mpv_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    // Initialize OpenGL Render Context for libmpv
    mpv_opengl_init_params gl_init_params{
        .get_proc_address = get_mpv_proc_address,
        .get_proc_address_ctx = nullptr,
    };

    const char* api_type = MPV_RENDER_API_TYPE_OPENGL;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(api_type)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (mpv_render_context_create(&mpv_gl_, mpv_, params) < 0) {
        luplay::Logger::error("Failed to create mpv OpenGL render context.");
        mpv_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    mpv_render_context_set_update_callback(mpv_gl_, on_mpv_render_update, this);

    luplay::Logger::info("Initialized mpv engine with hwdec=auto-safe successfully.");
    return true;
}

void Player::shutdown() {
    if (mpv_gl_) {
        mpv_render_context_free(mpv_gl_);
        mpv_gl_ = nullptr;
    }
    if (mpv_) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
    current_filepath_.clear();
    active_subtitle_path_.clear();
}

bool Player::load_file(const std::string& filepath) {
    if (!mpv_) return false;

    const char* cmd[] = {"loadfile", filepath.c_str(), nullptr};
    int err = mpv_command(mpv_, cmd);
    if (err < 0) {
        luplay::Logger::error(std::string("Error loading file '") + filepath + "': " + mpv_error_string(err));
        return false;
    }

    current_filepath_ = filepath;
    active_subtitle_path_.clear();
    luplay::Logger::info("Loaded media file: " + filepath);

    // Auto-detect matching subtitle (.srt) file in the same directory
    auto_detect_subtitles(filepath);
    return true;
}

void Player::auto_detect_subtitles(const std::string& video_path) {
    namespace fs = std::filesystem;
    try {
        fs::path vpath(video_path);
        fs::path srt_path = vpath;
        srt_path.replace_extension(".srt");

        if (fs::exists(srt_path)) {
            luplay::Logger::info("Auto-detected matching subtitle: " + srt_path.string());
            load_subtitle(srt_path.string());
        }
    } catch (const std::exception& e) {
        luplay::Logger::error(std::string("Error during subtitle auto-detection: ") + e.what());
    }
}

bool Player::load_subtitle(const std::string& filepath) {
    if (!mpv_) return false;

    const char* cmd[] = {"sub-add", filepath.c_str(), "select", nullptr};
    int err = mpv_command(mpv_, cmd);
    if (err < 0) {
        luplay::Logger::error(std::string("Error adding subtitle '") + filepath + "': " + mpv_error_string(err));
        return false;
    }

    active_subtitle_path_ = filepath;
    luplay::Logger::info("Subtitle loaded & selected: " + filepath);
    return true;
}

std::vector<SubtitleTrackInfo> Player::get_subtitle_tracks() const {
    std::vector<SubtitleTrackInfo> tracks;
    if (!mpv_) return tracks;

    int64_t count = 0;
    if (mpv_get_property(mpv_, "track-list/count", MPV_FORMAT_INT64, &count) < 0) {
        return tracks;
    }

    for (int64_t i = 0; i < count; ++i) {
        std::string prefix = "track-list/" + std::to_string(i) + "/";
        
        char* type = nullptr;
        if (mpv_get_property(mpv_, (prefix + "type").c_str(), MPV_FORMAT_STRING, &type) >= 0 && type) {
            std::string type_str(type);
            mpv_free(type);

            if (type_str == "sub") {
                SubtitleTrackInfo track;
                int64_t id = 0;
                mpv_get_property(mpv_, (prefix + "id").c_str(), MPV_FORMAT_INT64, &id);
                track.id = static_cast<int>(id);

                char* title = nullptr;
                if (mpv_get_property(mpv_, (prefix + "title").c_str(), MPV_FORMAT_STRING, &title) >= 0 && title) {
                    track.title = title;
                    mpv_free(title);
                }

                char* lang = nullptr;
                if (mpv_get_property(mpv_, (prefix + "lang").c_str(), MPV_FORMAT_STRING, &lang) >= 0 && lang) {
                    track.lang = lang;
                    mpv_free(lang);
                }

                int selected = 0;
                mpv_get_property(mpv_, (prefix + "selected").c_str(), MPV_FORMAT_FLAG, &selected);
                track.selected = (selected != 0);

                std::ostringstream oss;
                oss << "Track " << track.id;
                if (!track.lang.empty()) oss << " [" << track.lang << "]";
                if (!track.title.empty()) oss << " - " << track.title;
                track.display_name = oss.str();

                tracks.push_back(track);
            }
        }
    }
    return tracks;
}

void Player::select_subtitle_track(int track_id) {
    if (!mpv_) return;
    if (track_id <= 0) {
        mpv_set_property_string(mpv_, "sid", "no");
    } else {
        std::string val = std::to_string(track_id);
        mpv_set_property_string(mpv_, "sid", val.c_str());
    }
}

int Player::get_active_subtitle_track_id() const {
    if (!mpv_) return 0;
    int64_t sid = 0;
    if (mpv_get_property(mpv_, "sid", MPV_FORMAT_INT64, &sid) >= 0) {
        return static_cast<int>(sid);
    }
    return 0;
}

void Player::set_sub_scale(double scale) {
    if (!mpv_) return;
    mpv_set_property(mpv_, "sub-scale", MPV_FORMAT_DOUBLE, &scale);
}

void Player::set_sub_pos(int pos) {
    if (!mpv_) return;
    int64_t val = pos;
    mpv_set_property(mpv_, "sub-pos", MPV_FORMAT_INT64, &val);
}

void Player::set_sub_delay(double seconds) {
    if (!mpv_) return;
    mpv_set_property(mpv_, "sub-delay", MPV_FORMAT_DOUBLE, &seconds);
}

void Player::play() {
    if (!mpv_) return;
    int pause_flag = 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &pause_flag);
}

void Player::pause() {
    if (!mpv_) return;
    int pause_flag = 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &pause_flag);
}

void Player::toggle_pause() {
    if (is_paused()) {
        play();
    } else {
        pause();
    }
}

void Player::seek(double seconds_relative) {
    if (!mpv_) return;
    std::string val = std::to_string(seconds_relative);
    const char* cmd[] = {"seek", val.c_str(), "relative", nullptr};
    mpv_command(mpv_, cmd);
}

void Player::set_position(double seconds_absolute) {
    if (!mpv_) return;
    mpv_set_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &seconds_absolute);
}

bool Player::is_paused() const {
    if (!mpv_) return true;
    int paused = 0;
    mpv_get_property(mpv_, "pause", MPV_FORMAT_FLAG, &paused);
    return paused != 0;
}

double Player::get_position() const {
    if (!mpv_) return 0.0;
    double pos = 0.0;
    mpv_get_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &pos);
    return pos;
}

double Player::get_duration() const {
    if (!mpv_) return 0.0;
    double dur = 0.0;
    mpv_get_property(mpv_, "duration", MPV_FORMAT_DOUBLE, &dur);
    return dur;
}

int Player::get_volume() const {
    if (!mpv_) return 100;
    double vol = 100.0;
    mpv_get_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &vol);
    return static_cast<int>(vol);
}

void Player::set_volume(int volume) {
    if (!mpv_) return;
    double vol = static_cast<double>(volume);
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &vol);
}

void Player::render(int width, int height, uint32_t fbo) {
    if (!mpv_gl_) return;

    mpv_opengl_fbo mpv_fbo{
        .fbo = static_cast<int>(fbo),
        .w = width,
        .h = height,
        .internal_format = 0
    };

    int flip_y = 1;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    mpv_render_context_render(mpv_gl_, params);
}

void Player::set_wakeup_callback(std::function<void()> callback) {
    wakeup_callback_ = std::move(callback);
}

} // namespace luplay
