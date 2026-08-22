#ifndef LUPLAY_PLAYER_HPP
#define LUPLAY_PLAYER_HPP

#include <string>
#include <memory>
#include <functional>
#include <cstdint>

struct mpv_handle;
struct mpv_render_context;

namespace luplay {

struct SubtitleTrackInfo {
    int id = 0;
    std::string title;
    std::string lang;
    std::string display_name;
    bool selected = false;
};

class Player {
public:
    Player();
    ~Player();

    // Disable copy / assignment
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Initialize mpv instance and OpenGL render context
    bool init();
    void shutdown();

    // Core media commands
    bool load_file(const std::string& filepath);
    bool load_subtitle(const std::string& filepath);

    // Subtitle track selection
    std::vector<SubtitleTrackInfo> get_subtitle_tracks() const;
    void select_subtitle_track(int track_id);
    int get_active_subtitle_track_id() const;

    // Subtitle properties
    void set_sub_scale(double scale);
    void set_sub_pos(int pos);
    void set_sub_delay(double seconds);

    // Playback controls
    void play();
    void pause();
    void toggle_pause();
    void seek(double seconds_relative);
    void set_position(double seconds_absolute);
    void set_volume(int volume_0_100);

    // State getters
    bool is_initialized() const { return mpv_ != nullptr && mpv_gl_ != nullptr; }
    bool is_paused() const;
    bool is_file_loaded() const { return !current_filepath_.empty(); }
    double get_position() const;
    double get_duration() const;
    int get_volume() const;
    std::string get_current_filepath() const { return current_filepath_; }
    std::string get_active_subtitle() const { return active_subtitle_path_; }

    // Video frame rendering into target FBO (fbo = 0 for main OpenGL window)
    void render(int width, int height, uint32_t fbo = 0);

    // Wakeup callback trigger for mpv event processing loop
    void set_wakeup_callback(std::function<void()> callback);

private:
    void auto_detect_subtitles(const std::string& video_path);

    mpv_handle* mpv_{nullptr};
    mpv_render_context* mpv_gl_{nullptr};
    
    std::string current_filepath_;
    std::string active_subtitle_path_;
    std::function<void()> wakeup_callback_;
};

} // namespace luplay

#endif // LUPLAY_PLAYER_HPP
