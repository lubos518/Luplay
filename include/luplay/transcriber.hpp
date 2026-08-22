#ifndef LUPLAY_TRANSCRIBER_HPP
#define LUPLAY_TRANSCRIBER_HPP

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstddef>

namespace luplay {

enum class TranscriberState {
    Idle,
    ExtractingAudio,
    Transcribing,
    TranslatingAI,
    Done,
    Error
};

enum class TargetLanguage {
    English,
    Czech
};

class Transcriber {
public:
    Transcriber();
    ~Transcriber();

    // Disable copy / assignment
    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;

    // Model path & target language configuration
    void set_model_path(const std::string& model_path);
    std::string get_model_path() const;

    void set_target_language(TargetLanguage lang);
    TargetLanguage get_target_language() const;

    void set_ollama_model(const std::string& model);
    std::string get_ollama_model() const;

    void set_use_gpu(bool enable);
    bool get_use_gpu() const;
    void set_whisper_progress(float prog);

    // Start background extraction & AI transcription pipeline
    bool start_pipeline(const std::string& video_filepath);

    // Cancel ongoing worker thread safely
    void cancel();

    // Thread-safe state & progress getters
    TranscriberState get_state() const;
    float get_progress() const;
    std::string get_last_error() const;
    std::string get_generated_srt_path() const;
    
    // Decoded audio PCM sample buffer (16000 Hz, mono float32)
    const std::vector<float>& get_audio_data() const;
    size_t get_audio_sample_count() const;

private:
    void pipeline_worker(std::string video_filepath);
    bool run_whisper_transcription(const std::string& video_filepath);
    bool run_deepl_translation(const std::string& english_srt_path, const std::string& output_czech_srt_path);

    mutable std::mutex mutex_;
    std::thread worker_thread_;
    std::atomic<bool> cancel_requested_{false};

    TranscriberState state_{TranscriberState::Idle};
    std::atomic<float> progress_{0.0f};
    std::string model_path_{"models/ggml-small.bin"};
    TargetLanguage target_language_{TargetLanguage::English};
    std::string ollama_model_{"translategemma:4b"};
    bool use_gpu_{true};
    std::string last_error_;
    std::string current_filepath_;
    std::string generated_srt_path_;

    std::vector<float> pcm_data_;
};

} // namespace luplay

#endif // LUPLAY_TRANSCRIBER_HPP
