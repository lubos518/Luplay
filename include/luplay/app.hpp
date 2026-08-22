#ifndef LUPLAY_APP_HPP
#define LUPLAY_APP_HPP

#include <string>
#include <memory>

namespace luplay {

struct AppConfig {
    std::string window_title = "Luplay - Native AI Video Player";
    int width = 1280;
    int height = 720;
    bool vsync = true;
};

enum class AppMode {
    Playback,
    Transcription
};

class Application {
public:
    explicit Application(AppConfig config = {});
    ~Application();

    // Disable copy
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool init();
    void run();
    void shutdown();

private:
    AppConfig config_;
    AppMode current_mode_{AppMode::Playback};
    bool is_running_{false};
};

} // namespace luplay

#endif // LUPLAY_APP_HPP
