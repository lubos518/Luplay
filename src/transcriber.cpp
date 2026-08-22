#include "luplay/transcriber.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <future>
#include <atomic>
#include <thread>
#include <vector>
#include <regex>

#include "httplib.h"
#include "whisper_wrapper.hpp"
#include "json.hpp"
#include "luplay/logger.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace {
std::string formatSrtTimestamp(int64_t time_ms) {
    if (time_ms < 0) time_ms = 0;
    int hours  = static_cast<int>(time_ms / 3600000);
    int mins   = static_cast<int>((time_ms % 3600000) / 60000);
    int secs   = static_cast<int>((time_ms % 60000) / 1000);
    int millis = static_cast<int>(time_ms % 1000);

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setfill('0') << std::setw(2) << mins << ":"
        << std::setfill('0') << std::setw(2) << secs << ","
        << std::setfill('0') << std::setw(3) << millis;
    return oss.str();
}

struct SubtitleBlock {
    int id;
    std::string timestamp_line;
    std::string text;
};
} // namespace

namespace luplay {

Transcriber::Transcriber() = default;

Transcriber::~Transcriber() {
    cancel();
}

void Transcriber::set_model_path(const std::string& model_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    model_path_ = model_path;
}

std::string Transcriber::get_model_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return model_path_;
}

void Transcriber::set_target_language(TargetLanguage lang) {
    std::lock_guard<std::mutex> lock(mutex_);
    target_language_ = lang;
}

TargetLanguage Transcriber::get_target_language() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return target_language_;
}

void Transcriber::set_ollama_model(const std::string& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    ollama_model_ = model;
}

std::string Transcriber::get_ollama_model() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ollama_model_;
}

void Transcriber::set_use_gpu(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    use_gpu_ = enable;
}

bool Transcriber::get_use_gpu() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return use_gpu_;
}

void Transcriber::set_whisper_progress(float prog) {
    if (prog > 1.0f) prog = 1.0f;
    progress_.store(prog);
}

bool Transcriber::start_pipeline(const std::string& video_filepath) {
    if (video_filepath.empty()) {
        luplay::Logger::error("Video file path is empty.");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == TranscriberState::ExtractingAudio || state_ == TranscriberState::Transcribing) {
            luplay::Logger::warning("Transcription pipeline is already running.");
            return false;
        }

        cancel_requested_.store(false);
        progress_.store(0.0f);
        state_ = TranscriberState::ExtractingAudio;
        last_error_.clear();
        generated_srt_path_.clear();
        current_filepath_ = video_filepath;
        pcm_data_.clear();
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    worker_thread_ = std::thread(&Transcriber::pipeline_worker, this, video_filepath);
    luplay::Logger::info("Started background transcription pipeline for: " + video_filepath);
    return true;
}

void Transcriber::cancel() {
    cancel_requested_.store(true);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

TranscriberState Transcriber::get_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

float Transcriber::get_progress() const {
    return progress_.load();
}

std::string Transcriber::get_last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

std::string Transcriber::get_generated_srt_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return generated_srt_path_;
}

const std::vector<float>& Transcriber::get_audio_data() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pcm_data_;
}

size_t Transcriber::get_audio_sample_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pcm_data_.size();
}

void Transcriber::pipeline_worker(std::string video_filepath) {
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;

    auto cleanupFFmpeg = [&]() {
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (swr_ctx) swr_free(&swr_ctx);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (fmt_ctx) avformat_close_input(&fmt_ctx);
    };

    auto setError = [&](const std::string& err_msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = err_msg;
        state_ = TranscriberState::Error;
        luplay::Logger::error(err_msg);
        cleanupFFmpeg();
    };

    // =========================================================================
    // PHASE 1: FFmpeg Demuxing & 16kHz Mono Float Resampling
    // =========================================================================

    if (avformat_open_input(&fmt_ctx, video_filepath.c_str(), nullptr, nullptr) < 0) {
        setError("Failed to open media container: " + video_filepath);
        return;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        setError("Failed to find stream information in media file.");
        return;
    }

    int audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_idx < 0) {
        setError("No audio stream found in media file.");
        return;
    }

    AVStream* stream = fmt_ctx->streams[audio_stream_idx];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        setError("Failed to find suitable audio decoder.");
        return;
    }

    codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx || avcodec_parameters_to_context(codec_ctx, stream->codecpar) < 0) {
        setError("Failed to setup audio codec context.");
        return;
    }

    if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
        setError("Failed to open audio decoder codec.");
        return;
    }

    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        setError("Failed to allocate audio resampler context.");
        return;
    }

#if LIBSWRESAMPLE_VERSION_INT >= AV_VERSION_INT(4, 5, 100)
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, 1);

    AVChannelLayout in_ch_layout;
    if (codec_ctx->ch_layout.nb_channels > 0) {
        av_channel_layout_copy(&in_ch_layout, &codec_ctx->ch_layout);
    } else {
        av_channel_layout_default(&in_ch_layout, 2);
    }

    av_opt_set_chlayout(swr_ctx, "in_chlayout", &in_ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", codec_ctx->sample_fmt, 0);

    av_opt_set_chlayout(swr_ctx, "out_chlayout", &out_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", 16000, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
#else
    int64_t in_ch_layout = codec_ctx->channel_layout;
    if (in_ch_layout == 0) {
        in_ch_layout = av_get_default_channel_layout(codec_ctx->channels > 0 ? codec_ctx->channels : 2);
    }

    av_opt_set_channel_layout(swr_ctx, "in_channel_layout", in_ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", codec_ctx->sample_fmt, 0);

    av_opt_set_channel_layout(swr_ctx, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", 16000, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
#endif

    if (swr_init(swr_ctx) < 0) {
        setError("Failed to initialize audio resampler context.");
        return;
    }

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        setError("Failed to allocate packet or frame buffers.");
        return;
    }

    double total_duration_sec = (fmt_ctx->duration != AV_NOPTS_VALUE) 
        ? (static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE) 
        : 1.0;

    std::vector<float> local_pcm;
    local_pcm.reserve(16000 * 60);

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (cancel_requested_.load()) {
            av_packet_unref(pkt);
            break;
        }

        if (pkt->stream_index == audio_stream_idx) {
            int ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret >= 0) {
                while (ret >= 0) {
                    ret = avcodec_receive_frame(codec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        break;
                    }

                    int max_dst_samples = av_rescale_rnd(
                        swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                        16000,
                        codec_ctx->sample_rate,
                        AV_ROUND_UP
                    );

                    uint8_t* out_buffer[1] = { nullptr };
                    av_samples_alloc(out_buffer, nullptr, 1, max_dst_samples, AV_SAMPLE_FMT_FLT, 0);

                    int converted = swr_convert(
                        swr_ctx,
                        out_buffer,
                        max_dst_samples,
                        (const uint8_t**)frame->extended_data,
                        frame->nb_samples
                    );

                    if (converted > 0 && out_buffer[0]) {
                        float* float_data = reinterpret_cast<float*>(out_buffer[0]);
                        local_pcm.insert(local_pcm.end(), float_data, float_data + converted);
                    }

                    if (out_buffer[0]) {
                        av_freep(&out_buffer[0]);
                    }

                    if (frame->pts != AV_NOPTS_VALUE) {
                        double pts_sec = frame->pts * av_q2d(stream->time_base);
                        float cur_prog = static_cast<float>(pts_sec / total_duration_sec) * 0.40f; // 0..40%
                        if (cur_prog > 0.40f) cur_prog = 0.40f;
                        progress_.store(cur_prog);
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

    // Flush resampler
    int max_dst_samples = av_rescale_rnd(swr_get_delay(swr_ctx, codec_ctx->sample_rate), 16000, codec_ctx->sample_rate, AV_ROUND_UP);
    if (max_dst_samples > 0) {
        uint8_t* out_buffer[1] = { nullptr };
        av_samples_alloc(out_buffer, nullptr, 1, max_dst_samples, AV_SAMPLE_FMT_FLT, 0);
        int converted = swr_convert(swr_ctx, out_buffer, max_dst_samples, nullptr, 0);
        if (converted > 0 && out_buffer[0]) {
            float* float_data = reinterpret_cast<float*>(out_buffer[0]);
            local_pcm.insert(local_pcm.end(), float_data, float_data + converted);
        }
        if (out_buffer[0]) {
            av_freep(&out_buffer[0]);
        }
    }

    cleanupFFmpeg();

    if (cancel_requested_.load()) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = TranscriberState::Idle;
        progress_.store(0.0f);
        luplay::Logger::info("Extraction pipeline cancelled.");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pcm_data_ = std::move(local_pcm);
        state_ = TranscriberState::Transcribing;
        progress_.store(0.45f);
    }

    luplay::Logger::info("Phase 1 Complete: Extracted " + std::to_string(pcm_data_.size()) + " samples. Moving to Phase 2: Whisper AI Transcription...");

    // =========================================================================
    // PHASE 2: Whisper AI Model Inference & SRT Generation
    // =========================================================================

    if (!run_whisper_transcription(video_filepath)) {
        // setError handled inside run_whisper_transcription
        return;
    }

    TargetLanguage lang_opt;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lang_opt = target_language_;
    }

    if (lang_opt == TargetLanguage::Czech) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = TranscriberState::TranslatingAI;
            progress_.store(0.0f); 
        }
        
        std::filesystem::path vpath(video_filepath);
        std::string eng_srt = vpath.replace_extension(".srt").string();
        std::string cz_srt = vpath.replace_extension(".cz.srt").string();
        
        if (!run_deepl_translation(eng_srt, cz_srt)) {
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = TranscriberState::Done;
        progress_.store(1.0f);
    }

    luplay::Logger::info("Phase 2 Complete: Subtitles generated at: " + get_generated_srt_path());
}

bool Transcriber::run_whisper_transcription(const std::string& video_filepath) {
    if (pcm_data_.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "Audio buffer is empty for Whisper transcription.";
        state_ = TranscriberState::Error;
        return false;
    }

    std::string target_model_path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target_model_path = model_path_;
    }

    // Robust search paths for GGML model files across working directories
    std::vector<std::string> search_paths = {
        target_model_path,
        "../" + target_model_path,
        "../../" + target_model_path,
        "A:/Luplay/" + target_model_path,
        "A:/Luplay/models/ggml-small.bin",
        "models/ggml-small.bin",
        "../models/ggml-small.bin",
        "../../models/ggml-small.bin",
        "models/ggml-base.bin",
        "../models/ggml-base.bin",
        "models/ggml-tiny.bin",
        "../models/ggml-tiny.bin"
    };

    std::string found_model_path;
    for (const auto& path : search_paths) {
        if (!path.empty() && std::filesystem::exists(path)) {
            found_model_path = path;
            break;
        }
    }

    if (found_model_path.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "Whisper GGML model file not found: '" + model_path_ + "'. Please place GGML model in ./models/ directory.";
        state_ = TranscriberState::Error;
        luplay::Logger::error(last_error_);
        return false;
    }

    target_model_path = found_model_path;

    bool use_gpu_opt = true;
    TargetLanguage lang_opt = TargetLanguage::English;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lang_opt = target_language_;
        use_gpu_opt = use_gpu_;
    }

    luplay::Logger::info("Loading Whisper GGML model: " + target_model_path + " (GPU: " + (use_gpu_opt ? "ON" : "OFF") + ")");

    try {
        WhisperWrapper whisper_api;

        struct whisper_context_params cparams = whisper_api.whisper_context_default_params();
        cparams.use_gpu = use_gpu_opt;
        cparams.flash_attn = true;
        cparams.gpu_device = 0;

        struct whisper_context* ctx = whisper_api.whisper_init_from_file_with_params(target_model_path.c_str(), cparams);

        if (!ctx) {
            std::lock_guard<std::mutex> lock(mutex_);
            last_error_ = "Failed to initialize Whisper AI context from model file: " + target_model_path;
            state_ = TranscriberState::Error;
            return false;
        }

        // Set whisper_full_params
        struct whisper_full_params wparams = whisper_api.whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.print_progress   = false;
        wparams.print_special    = false;
        wparams.n_threads = 8; // 8 threads for parallel audio window staging to fully saturate RTX 3060 CUDA pipeline

        auto whisper_start_time = std::chrono::steady_clock::now();
        luplay::Logger::info("[Whisper GPU] Starting CUDA neural transcription on NVIDIA GeForce RTX 3060...");

        wparams.progress_callback = [](struct whisper_context* /*ctx*/, struct whisper_state* /*state*/, int progress, void* user_data) {
            auto* transcriber = static_cast<Transcriber*>(user_data);
            if (transcriber) {
                float overall_prog = 0.45f + (static_cast<float>(progress) / 100.0f) * 0.50f;
                transcriber->set_whisper_progress(overall_prog);
                if (progress % 25 == 0 && progress > 0 && progress < 100) {
                    luplay::Logger::info("[Whisper GPU] Processing: " + std::to_string(progress) + "%");
                }
            }
        };
        wparams.progress_callback_user_data = this;

        // Force English / translation mode to get English SRT as intermediate
        wparams.language = "en";
        wparams.translate = true;

        progress_.store(0.45f);

        if (whisper_api.whisper_full(ctx, wparams, pcm_data_.data(), static_cast<int>(pcm_data_.size())) != 0) {
            whisper_api.whisper_free(ctx);
            std::lock_guard<std::mutex> lock(mutex_);
            last_error_ = "Whisper AI transcription execution failed.";
            state_ = TranscriberState::Error;
            return false;
        }

        auto whisper_end_time = std::chrono::steady_clock::now();
        double whisper_sec = std::chrono::duration<double>(whisper_end_time - whisper_start_time).count();
        double audio_sec = static_cast<double>(pcm_data_.size()) / 16000.0;
        double speed_ratio = audio_sec / (whisper_sec > 0.01 ? whisper_sec : 1.0);
        
        luplay::Logger::info("[Whisper GPU] Finished: Transcribed " + std::to_string(static_cast<int>(audio_sec)) + "s of audio in " + std::to_string(static_cast<int>(whisper_sec)) + "s (" + std::to_string(static_cast<int>(speed_ratio)) + "x realtime on RTX 3060).");
        progress_.store(0.95f);

        // Format & export SRT file next to the video
        namespace fs = std::filesystem;
        fs::path vpath(video_filepath);
        fs::path srt_path = vpath;
        srt_path.replace_extension(".srt");

        std::ofstream srt_out(srt_path);
        if (!srt_out.is_open()) {
            whisper_api.whisper_free(ctx);
            std::lock_guard<std::mutex> lock(mutex_);
            last_error_ = "Failed to create output SRT file: " + srt_path.string();
            state_ = TranscriberState::Error;
            return false;
        }

        int n_segments = whisper_api.whisper_full_n_segments(ctx);
        int out_id = 1;
        for (int i = 0; i < n_segments; ++i) {
            int64_t t0 = whisper_api.whisper_full_get_segment_t0(ctx, i);
            int64_t t1 = whisper_api.whisper_full_get_segment_t1(ctx, i);
            const char* raw_text = whisper_api.whisper_full_get_segment_text(ctx, i);
            
            std::string text_str = raw_text ? raw_text : "";
            // Remove text between () and [] (e.g. [music], (bell ringing))
            text_str = std::regex_replace(text_str, std::regex(R"(\(.*?\)|\[.*?\])"), "");
            
            // Trim whitespace
            text_str.erase(text_str.begin(), std::find_if(text_str.begin(), text_str.end(), [](unsigned char ch) { return !std::isspace(ch); }));
            text_str.erase(std::find_if(text_str.rbegin(), text_str.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), text_str.end());

            if (text_str.empty()) continue;

            srt_out << out_id++ << "\n";
            srt_out << formatSrtTimestamp(t0 * 10) << " --> " << formatSrtTimestamp(t1 * 10) << "\n";
            srt_out << text_str << "\n\n";
        }

        srt_out.close();
        whisper_api.whisper_free(ctx);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            generated_srt_path_ = srt_path.string();
        }

        return true;
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = std::string("Whisper Wrapper Error: ") + e.what();
        state_ = TranscriberState::Error;
        return false;
    }
}

bool Transcriber::run_deepl_translation(const std::string& english_srt_path, const std::string& output_czech_srt_path) {
    luplay::Logger::info("Starting Phase 3: DeepL API Translation (English -> Czech)...");
    
    std::ifstream in(english_srt_path);
    if (!in.is_open()) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "Failed to open English SRT for translation: " + english_srt_path;
        state_ = TranscriberState::Error;
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string srt_content = buffer.str();
    in.close();

    // Parse SRT do SubtitleBlock struktur
    std::vector<SubtitleBlock> parsed_blocks;
    size_t pos = 0;
    while (pos < srt_content.size()) {
        size_t next_pos = srt_content.find("\n\n", pos);
        std::string blk;
        if (next_pos == std::string::npos) {
            blk = srt_content.substr(pos);
            pos = srt_content.size();
        } else {
            blk = srt_content.substr(pos, next_pos - pos);
            pos = next_pos + 2;
        }
        while (!blk.empty() && (blk.back() == '\n' || blk.back() == '\r')) blk.pop_back();
        while (!blk.empty() && (blk.front() == '\n' || blk.front() == '\r')) blk.erase(blk.begin());
        if (!blk.empty()) {
            std::stringstream ss(blk);
            std::string id_str, ts_line, text_line, full_text;
            if (std::getline(ss, id_str) && std::getline(ss, ts_line)) {
                while (std::getline(ss, text_line)) {
                    if (!full_text.empty()) full_text += "\n";
                    full_text += text_line;
                }
                try {
                    SubtitleBlock sb;
                    sb.id = std::stoi(id_str);
                    sb.timestamp_line = ts_line;
                    sb.text = full_text;
                    parsed_blocks.push_back(sb);
                } catch (...) {
                    // Fallback ak se parsovani nezdari, blok preskocime
                }
            }
        }
    }

    if (parsed_blocks.empty()) {
        luplay::Logger::warning("No subtitle blocks found in English SRT.");
        std::lock_guard<std::mutex> lock(mutex_);
        generated_srt_path_ = english_srt_path;
        return true;
    }

    std::ofstream out(output_czech_srt_path);
    if (!out.is_open()) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "Failed to create output Czech SRT: " + output_czech_srt_path;
        state_ = TranscriberState::Error;
        return false;
    }

    const size_t CHUNK_SIZE = 200;
    size_t total_chunks = (parsed_blocks.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
    luplay::Logger::info("Translating " + std::to_string(parsed_blocks.size()) + " subtitle blocks in " + std::to_string(total_chunks) + " chunks using DeepL API (XML Tagging)...");

    auto trans_start_time = std::chrono::steady_clock::now();

    for (size_t i = 0; i < parsed_blocks.size(); i += CHUNK_SIZE) {
        if (cancel_requested_.load()) {
            out.close();
            return false;
        }

        size_t current_chunk = i / CHUNK_SIZE;
        float prog = static_cast<float>(current_chunk) / static_cast<float>(total_chunks);
        progress_.store(prog);

        std::string xml_document;
        for (size_t j = i; j < i + CHUNK_SIZE && j < parsed_blocks.size(); ++j) {
            std::string escaped_text = parsed_blocks[j].text;
            size_t p = 0;
            while ((p = escaped_text.find('<', p)) != std::string::npos) { escaped_text.replace(p, 1, "&lt;"); p += 4; }
            p = 0;
            while ((p = escaped_text.find('>', p)) != std::string::npos) { escaped_text.replace(p, 1, "&gt;"); p += 4; }

            xml_document += "<s id=\"" + std::to_string(parsed_blocks[j].id) + "\">" + escaped_text + "</s>\n";
        }

        nlohmann::json req_body;
        req_body["target_lang"] = "CS";
        req_body["tag_handling"] = "xml";
        req_body["text"] = nlohmann::json::array({xml_document});

        // Použijeme systémový curl, jelikož Windows 10/11 jej obsahují nativně 
        // a vyhneme se tak masivní kompilaci OpenSSL přes vcpkg.
        
        std::string req_json_path = (std::filesystem::temp_directory_path() / "deepl_req.json").string();
        std::ofstream req_out(req_json_path);
        req_out << req_body.dump();
        req_out.close();

        std::string curl_cmd = "curl -s -X POST https://api-free.deepl.com/v2/translate "
                               "-H \"Authorization: DeepL-Auth-Key 06b76faf-772e-4d82-8455-e3424645a066:fx\" "
                               "-H \"Content-Type: application/json\" "
                               "-d @" + req_json_path;
                               
        std::string res_body;
#ifdef _WIN32
        FILE* pipe = _popen(curl_cmd.c_str(), "r");
#else
        FILE* pipe = popen(curl_cmd.c_str(), "r");
#endif
        if (pipe) {
            char buffer[4096];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                res_body += buffer;
            }
#ifdef _WIN32
            _pclose(pipe);
#else
            pclose(pipe);
#endif
        }

        std::filesystem::remove(req_json_path);

        if (!res_body.empty()) {
            try {
                auto res_json = nlohmann::json::parse(res_body);
                if (res_json.contains("translations") && res_json["translations"].is_array() && !res_json["translations"].empty()) {
                    std::string translated_xml = res_json["translations"][0]["text"].get<std::string>();
                    
                    for (size_t j = i; j < i + CHUNK_SIZE && j < parsed_blocks.size(); ++j) {
                        std::string start_tag = "<s id=\"" + std::to_string(parsed_blocks[j].id) + "\">";
                        std::string end_tag = "</s>";
                        
                        size_t start_pos = translated_xml.find(start_tag);
                        if (start_pos != std::string::npos) {
                            start_pos += start_tag.length();
                            size_t end_pos = translated_xml.find(end_tag, start_pos);
                            if (end_pos != std::string::npos) {
                                std::string trans_text = translated_xml.substr(start_pos, end_pos - start_pos);
                                out << parsed_blocks[j].id << "\n";
                                out << parsed_blocks[j].timestamp_line << "\n";
                                out << trans_text << "\n\n";
                                continue;
                            }
                        }
                        
                        // Fallback k anglictine pokud XML selze u konkretniho radku
                        out << parsed_blocks[j].id << "\n";
                        out << parsed_blocks[j].timestamp_line << "\n";
                        out << parsed_blocks[j].text << "\n\n";
                    }
                    out.flush();

                    float chunk_done_prog = static_cast<float>(current_chunk + 1) / static_cast<float>(total_chunks);
                    progress_.store(chunk_done_prog);
                } else {
                    std::lock_guard<std::mutex> lock(mutex_);
                    last_error_ = "DeepL API Error: " + res_body;
                    state_ = TranscriberState::Error;
                    return false;
                }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(mutex_);
                last_error_ = std::string("Failed to parse DeepL response: ") + e.what() + " | Body: " + res_body;
                state_ = TranscriberState::Error;
                return false;
            }
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            last_error_ = "Failed to connect to DeepL API using curl.";
            state_ = TranscriberState::Error;
            return false;
        }
    }
    
    out.close();
    
    auto trans_end_time = std::chrono::steady_clock::now();
    double trans_sec = std::chrono::duration<double>(trans_end_time - trans_start_time).count();
    luplay::Logger::info("DeepL translation completed in " + std::to_string(static_cast<int>(trans_sec)) + "s.");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        generated_srt_path_ = output_czech_srt_path;
        progress_.store(1.0f);
    }
    
    return true;
}
} // namespace luplay
