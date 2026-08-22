#pragma once

#include <string>
#include <stdexcept>
#include "whisper.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace luplay {

class WhisperWrapper {
public:
    WhisperWrapper() {
#ifdef _WIN32
        // 1. Add CUDA toolkit 64-bit bin directory to DLL search path so cublas64_*.dll and cudart64_*.dll load properly
        const char* cuda_env = getenv("CUDA_PATH");
        std::string cuda_root = cuda_env ? cuda_env : "C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\v13.3";
        std::string cuda_bin_x64 = cuda_root + "\\bin\\x64";
        std::string cuda_bin = cuda_root + "\\bin";

        SetDllDirectoryA(cuda_bin_x64.c_str());

        // 2. Pre-load ggml dependencies and backend DLLs
        hGgmlBase = LoadLibraryA("ggml-base.dll");
        hGgmlCpu  = LoadLibraryA("ggml-cpu.dll");
        hGgmlCuda = LoadLibraryA("ggml-cuda.dll");
        hGgml     = LoadLibraryA("ggml.dll");

        // 3. Register CUDA backend directly with ggml runtime
        if (hGgml && hGgmlCuda) {
            auto p_ggml_backend_cuda_reg = (void*(*)())GetProcAddress(hGgmlCuda, "ggml_backend_cuda_reg");
            auto p_ggml_backend_register = (void(*)(void*))GetProcAddress(hGgml, "ggml_backend_register");
            if (p_ggml_backend_cuda_reg && p_ggml_backend_register) {
                p_ggml_backend_register(p_ggml_backend_cuda_reg());
            }
        }

        hMod = LoadLibraryA("whisper.dll");
        if (!hMod) {
            if (hGgmlCuda) FreeLibrary(hGgmlCuda);
            if (hGgmlCpu) FreeLibrary(hGgmlCpu);
            if (hGgmlBase) FreeLibrary(hGgmlBase);
            if (hGgml) FreeLibrary(hGgml);
            throw std::runtime_error("Failed to load whisper.dll - Make sure it is in the same directory as the executable.");
        }

        p_whisper_init_from_file_with_params = (decltype(p_whisper_init_from_file_with_params))GetProcAddress(hMod, "whisper_init_from_file_with_params");
        p_whisper_full_default_params = (decltype(p_whisper_full_default_params))GetProcAddress(hMod, "whisper_full_default_params");
        p_whisper_full = (decltype(p_whisper_full))GetProcAddress(hMod, "whisper_full");
        p_whisper_full_n_segments = (decltype(p_whisper_full_n_segments))GetProcAddress(hMod, "whisper_full_n_segments");
        p_whisper_full_get_segment_t0 = (decltype(p_whisper_full_get_segment_t0))GetProcAddress(hMod, "whisper_full_get_segment_t0");
        p_whisper_full_get_segment_t1 = (decltype(p_whisper_full_get_segment_t1))GetProcAddress(hMod, "whisper_full_get_segment_t1");
        p_whisper_full_get_segment_text = (decltype(p_whisper_full_get_segment_text))GetProcAddress(hMod, "whisper_full_get_segment_text");
        p_whisper_free = (decltype(p_whisper_free))GetProcAddress(hMod, "whisper_free");
        p_whisper_context_default_params = (decltype(p_whisper_context_default_params))GetProcAddress(hMod, "whisper_context_default_params");

        if (!p_whisper_init_from_file_with_params || !p_whisper_full || !p_whisper_free) {
            FreeLibrary(hMod);
            hMod = nullptr;
            throw std::runtime_error("Failed to find required whisper functions in whisper.dll. The DLL might be outdated or corrupt.");
        }
#endif
    }

    ~WhisperWrapper() {
#ifdef _WIN32
        if (hMod) {
            FreeLibrary(hMod);
            hMod = nullptr;
        }
        if (hGgmlCuda) {
            FreeLibrary(hGgmlCuda);
            hGgmlCuda = nullptr;
        }
        if (hGgmlCpu) {
            FreeLibrary(hGgmlCpu);
            hGgmlCpu = nullptr;
        }
        if (hGgmlBase) {
            FreeLibrary(hGgmlBase);
            hGgmlBase = nullptr;
        }
        if (hGgml) {
            FreeLibrary(hGgml);
            hGgml = nullptr;
        }
#endif
    }

#ifdef _WIN32
    struct whisper_context* whisper_init_from_file_with_params(const char * path_model, struct whisper_context_params params) {
        return p_whisper_init_from_file_with_params(path_model, params);
    }
    struct whisper_full_params whisper_full_default_params(enum whisper_sampling_strategy strategy) {
        return p_whisper_full_default_params(strategy);
    }
    int whisper_full(struct whisper_context * ctx, struct whisper_full_params params, const float * samples, int n_samples) {
        return p_whisper_full(ctx, params, samples, n_samples);
    }
    int whisper_full_n_segments(struct whisper_context * ctx) {
        return p_whisper_full_n_segments(ctx);
    }
    int64_t whisper_full_get_segment_t0(struct whisper_context * ctx, int i_segment) {
        return p_whisper_full_get_segment_t0(ctx, i_segment);
    }
    int64_t whisper_full_get_segment_t1(struct whisper_context * ctx, int i_segment) {
        return p_whisper_full_get_segment_t1(ctx, i_segment);
    }
    const char * whisper_full_get_segment_text(struct whisper_context * ctx, int i_segment) {
        return p_whisper_full_get_segment_text(ctx, i_segment);
    }
    void whisper_free(struct whisper_context * ctx) {
        p_whisper_free(ctx);
    }
    struct whisper_context_params whisper_context_default_params() {
        return p_whisper_context_default_params();
    }
#else
    struct whisper_context* whisper_init_from_file_with_params(const char * path_model, struct whisper_context_params params) {
        return ::whisper_init_from_file_with_params(path_model, params);
    }
    struct whisper_full_params whisper_full_default_params(enum whisper_sampling_strategy strategy) {
        return ::whisper_full_default_params(strategy);
    }
    int whisper_full(struct whisper_context * ctx, struct whisper_full_params params, const float * samples, int n_samples) {
        return ::whisper_full(ctx, params, samples, n_samples);
    }
    int whisper_full_n_segments(struct whisper_context * ctx) {
        return ::whisper_full_n_segments(ctx);
    }
    int64_t whisper_full_get_segment_t0(struct whisper_context * ctx, int i_segment) {
        return ::whisper_full_get_segment_t0(ctx, i_segment);
    }
    int64_t whisper_full_get_segment_t1(struct whisper_context * ctx, int i_segment) {
        return ::whisper_full_get_segment_t1(ctx, i_segment);
    }
    const char * whisper_full_get_segment_text(struct whisper_context * ctx, int i_segment) {
        return ::whisper_full_get_segment_text(ctx, i_segment);
    }
    void whisper_free(struct whisper_context * ctx) {
        ::whisper_free(ctx);
    }
    struct whisper_context_params whisper_context_default_params() {
        return ::whisper_context_default_params();
    }
#endif

private:
#ifdef _WIN32
    HMODULE hMod = nullptr;
    HMODULE hGgml = nullptr;
    HMODULE hGgmlCuda = nullptr;
    HMODULE hGgmlCpu = nullptr;
    HMODULE hGgmlBase = nullptr;
    struct whisper_context* (*p_whisper_init_from_file_with_params)(const char *, struct whisper_context_params) = nullptr;
    struct whisper_full_params (*p_whisper_full_default_params)(enum whisper_sampling_strategy) = nullptr;
    int (*p_whisper_full)(struct whisper_context *, struct whisper_full_params, const float *, int) = nullptr;
    int (*p_whisper_full_n_segments)(struct whisper_context *) = nullptr;
    int64_t (*p_whisper_full_get_segment_t0)(struct whisper_context *, int) = nullptr;
    int64_t (*p_whisper_full_get_segment_t1)(struct whisper_context *, int) = nullptr;
    const char * (*p_whisper_full_get_segment_text)(struct whisper_context *, int) = nullptr;
    void (*p_whisper_free)(struct whisper_context *) = nullptr;
    struct whisper_context_params (*p_whisper_context_default_params)() = nullptr;
#endif
};

} // namespace luplay
