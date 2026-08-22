#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0;
}
#endif

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "luplay/app.hpp"
#include "luplay/logger.hpp"
#include "luplay/player.hpp"
#include "luplay/transcriber.hpp"

namespace {
luplay::Player *g_player_ptr = nullptr;

void glfwErrorCallback(int error, const char *description) {
  luplay::Logger::error(std::string("[GLFW Error ") + std::to_string(error) +
                        "]: " + description);
}

void dropCallback(GLFWwindow *window, int count, const char **paths) {
  (void)window;
  if (count > 0 && g_player_ptr && paths && paths[0]) {
    luplay::Logger::info(std::string("File dropped: ") + paths[0]);
    g_player_ptr->load_file(paths[0]);
  }
}

std::string formatTime(double seconds) {
  if (seconds < 0.0)
    seconds = 0.0;
  int total_sec = static_cast<int>(seconds);
  int mins = total_sec / 60;
  int secs = total_sec % 60;
  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(2) << mins << ":" << std::setfill('0')
      << std::setw(2) << secs;
  return oss.str();
}

std::string openFileDialog() {
#ifdef _WIN32
  OPENFILENAMEA ofn;
  CHAR szFile[512] = {0};
  ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
  ofn.lStructSize = sizeof(OPENFILENAMEA);
  ofn.hwndOwner = NULL;
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = sizeof(szFile);
  ofn.lpstrFilter = "Video Files\0*.mp4;*.mkv;*.avi;*.webm\0All Files\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.lpstrFileTitle = NULL;
  ofn.nMaxFileTitle = 0;
  ofn.lpstrInitialDir = NULL;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameA(&ofn) == TRUE) {
    return std::string(ofn.lpstrFile);
  }
#elif defined(__APPLE__)
  luplay::Logger::warning(
      "File dialog is not implemented natively on macOS yet. Please Drag & "
      "Drop the video file into the window!");
#else
  luplay::Logger::warning(
      "File dialog not supported on this OS. Please use Drag & Drop.");
#endif
  return "";
}

bool is_fullscreen = false;
int windowed_x = 100, windowed_y = 100, windowed_width = 1280,
    windowed_height = 720;

void toggleFullscreen(GLFWwindow *window) {
  if (!is_fullscreen) {
    glfwGetWindowPos(window, &windowed_x, &windowed_y);
    glfwGetWindowSize(window, &windowed_width, &windowed_height);
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height,
                         mode->refreshRate);
    is_fullscreen = true;
  } else {
    glfwSetWindowMonitor(window, nullptr, windowed_x, windowed_y,
                         windowed_width, windowed_height, 0);
    is_fullscreen = false;
  }
}

bool PlayPauseButton(const char *id, bool is_paused, ImVec2 size) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton(id, size);
  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();

  ImU32 col = ImColor(hovered ? ImVec4(0.85f, 0.0f, 0.15f, 1.0f)
                              : ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
  if (active)
    col = ImColor(ImVec4(0.7f, 0.0f, 0.1f, 1.0f));

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  float padding = 8.0f;
  ImVec2 icon_pos = ImVec2(p.x + padding, p.y + padding);
  float icon_size = size.y - padding * 2.0f;

  if (is_paused) {
    draw_list->AddTriangleFilled(
        icon_pos, ImVec2(icon_pos.x, icon_pos.y + icon_size),
        ImVec2(icon_pos.x + icon_size * 0.86f, icon_pos.y + icon_size / 2.0f),
        col);
  } else {
    float w = icon_size * 0.3f;
    draw_list->AddRectFilled(
        icon_pos, ImVec2(icon_pos.x + w, icon_pos.y + icon_size), col, 2.0f);
    draw_list->AddRectFilled(
        ImVec2(icon_pos.x + icon_size - w, icon_pos.y),
        ImVec2(icon_pos.x + icon_size, icon_pos.y + icon_size), col, 2.0f);
  }
  return clicked;
}

void applyCustomDarkStyle() {
  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 *colors = style.Colors;

  colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  colors[ImGuiCol_WindowBg] =
      ImVec4(0.08f, 0.08f, 0.09f, 0.94f); // Glassmorphic dark
  colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.09f, 0.98f);
  colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.15f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.18f, 0.80f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.27f, 0.90f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);

  // Netflix Red accent
  ImVec4 accent = ImVec4(0.89f, 0.04f, 0.08f, 1.00f);
  ImVec4 accent_hover = ImVec4(0.95f, 0.10f, 0.15f, 1.00f);
  ImVec4 accent_active = ImVec4(0.70f, 0.00f, 0.00f, 1.00f);

  colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.04f, 0.04f, 0.04f, 0.95f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.50f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);

  colors[ImGuiCol_CheckMark] = accent;
  colors[ImGuiCol_SliderGrab] = accent;
  colors[ImGuiCol_SliderGrabActive] = accent_active;
  colors[ImGuiCol_Button] =
      ImVec4(0.00f, 0.00f, 0.00f,
             0.00f); // default invisible buttons pro Netflix overlay
  colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
  colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.20f);
  colors[ImGuiCol_Header] = ImVec4(0.18f, 0.18f, 0.20f, 0.85f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.24f, 0.27f, 1.00f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);

  style.WindowRounding = 8.0f;
  style.ChildRounding = 8.0f;
  style.FrameRounding = 10.0f;
  style.PopupRounding = 8.0f;
  style.ScrollbarRounding = 8.0f;
  style.GrabRounding = 12.0f;
  style.WindowPadding = ImVec2(18.0f, 18.0f);
  style.FramePadding = ImVec2(16.0f, 10.0f);
  style.ItemSpacing = ImVec2(14.0f, 12.0f);
  style.WindowBorderSize = 0.0f;
}
} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
  // Ensure CUDA toolkit bin\x64 and bin are in the PATH & DLL search so
  // ggml-cuda.dll can find cublas64_*.dll
  const char *cuda_env = getenv("CUDA_PATH");
  std::string cuda_root =
      cuda_env ? cuda_env
               : "C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\v13.3";
  std::string cuda_bin_x64 = cuda_root + "\\bin\\x64";
  std::string cuda_bin = cuda_root + "\\bin";

  const char *current_path = getenv("PATH");
  if (current_path) {
    std::string new_path =
        cuda_bin_x64 + ";" + cuda_bin + ";" + std::string(current_path);
    _putenv_s("PATH", new_path.c_str());
  }
  SetDllDirectoryA(cuda_bin_x64.c_str());
#endif

  (void)argc;
  (void)argv;

  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) {
    luplay::Logger::error("Failed to initialize GLFW");
    return EXIT_FAILURE;
  }

  // Configure OpenGL 3.3 Core Profile
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
#if __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  const luplay::AppConfig config;
  GLFWwindow *window =
      glfwCreateWindow(config.width, config.height, config.window_title.c_str(),
                       nullptr, nullptr);

  if (!window) {
    luplay::Logger::error("Failed to create GLFW window");
    glfwTerminate();
    return EXIT_FAILURE;
  }

  // Zamezení příliš malému nebo negativnímu rozměru okna
  glfwSetWindowSizeLimits(window, 800, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);

  glfwMakeContextCurrent(window);
  glfwSwapInterval(config.vsync ? 1 : 0);

  // Initialize MPV Engine & Transcriber Worker
  luplay::Player player;
  luplay::Transcriber transcriber;
  g_player_ptr = &player;

  if (!player.init()) {
    luplay::Logger::warning(
        "Could not initialize MPV engine. Video playback may be unavailable.");
  }

  // Register GLFW drag and drop callback
  glfwSetDropCallback(window, dropCallback);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();

  applyCustomDarkStyle();

  // Načtení systémového Segoe UI fontu (pokud je dostupný) pro luxusnější
  // vzhled
  if (std::filesystem::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 22.0f);
  } else {
    io.Fonts->AddFontDefault();
  }

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  char file_input_buf[512] = "";
  char model_input_buf[256] = "models/ggml-small.bin";
  char ollama_input_buf[256] = "translategemma:4b";
  std::string last_auto_loaded_srt;
  bool show_log_window = false;
  static bool show_subtitle_window = false;

  // Main Render Loop
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);

    // 1. Clear OpenGL Background Framebuffer
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.05f, 0.05f, 0.07f, 1.00f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 2. Render libmpv Hardware-Decoded Video Frame directly to FBO 0
    if (player.is_initialized()) {
      player.render(display_w, display_h, 0);
    }

    // 3. Auto-load Subtitle upon completion of Transcription Pipeline
    luplay::TranscriberState trans_state = transcriber.get_state();
    if (trans_state == luplay::TranscriberState::Done) {
      std::string generated_srt = transcriber.get_generated_srt_path();
      if (!generated_srt.empty() && generated_srt != last_auto_loaded_srt) {
        luplay::Logger::info("Pipeline Finished! Auto-loading generated SRT: " +
                             generated_srt);
        player.load_subtitle(generated_srt);
        last_auto_loaded_srt = generated_srt;
      }
    }

    // 4. ImGui Overlay Frame Preparation
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Fade-out and Mouse Idle Logic
    static double last_mouse_activity = glfwGetTime();
    static double last_mouse_x = 0;
    static double last_mouse_y = 0;
    static bool cursor_hidden = false;

    double current_mouse_x, current_mouse_y;
    glfwGetCursorPos(window, &current_mouse_x, &current_mouse_y);

    double m_dx = current_mouse_x - last_mouse_x;
    double m_dy = current_mouse_y - last_mouse_y;
    if ((m_dx * m_dx + m_dy * m_dy) > 4.0 || ImGui::IsAnyItemActive()) {
      last_mouse_activity = glfwGetTime();
      last_mouse_x = current_mouse_x;
      last_mouse_y = current_mouse_y;
    }

    // Check if mouse is hovering over top bar, bottom panel, active windows, or
    // any interactive widget
    bool is_mouse_over_controls =
        (current_mouse_y <= 65.0) ||
        (current_mouse_y >= static_cast<double>(display_h) - 175.0) ||
        io.WantCaptureMouse || ImGui::IsAnyItemHovered() ||
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

    if (is_mouse_over_controls) {
      last_mouse_activity = glfwGetTime();
    }

    // Show UI if mouse moved recently, or hovering over controls, or if paused,
    // or windows open
    bool is_ui_visible = (glfwGetTime() - last_mouse_activity) < 3.0 ||
                         is_mouse_over_controls || player.is_paused() ||
                         show_subtitle_window || show_log_window;

    // Dynamic Subtitle Position: Automatically lift subtitles up when bottom
    // panel is visible so they remain readable
    static int user_sub_pos = 100;
    static int current_applied_sub_pos = -1;
    int target_sub_pos = is_ui_visible ? (user_sub_pos - 18) : user_sub_pos;
    if (target_sub_pos < 0)
      target_sub_pos = 0;
    if (current_applied_sub_pos != target_sub_pos) {
      player.set_sub_pos(target_sub_pos);
      current_applied_sub_pos = target_sub_pos;
    }

    if (is_ui_visible) {
      if (cursor_hidden) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        cursor_hidden = false;
      }
    } else {
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      if (!cursor_hidden) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        cursor_hidden = true;
      }
    }

    // Single click to pause
    if (ImGui::IsMouseClicked(0) && !io.WantCaptureMouse) {
      player.toggle_pause();
    }

    // Double click for fullscreen
    if (ImGui::IsMouseDoubleClicked(0) && !io.WantCaptureMouse) {
      toggleFullscreen(window);
    }

    // Key Shortcuts (Globalne pro Escape/Enter)
    if (ImGui::IsKeyPressed(ImGuiKey_F) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      toggleFullscreen(window);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && is_fullscreen) {
      toggleFullscreen(window);
    }

    // Player shortcuts (Always active unless typing in an input field)
    if (!io.WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        player.toggle_pause();
      }
      if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        player.seek(10.0);
      }
      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        player.seek(-10.0);
      }
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        player.set_volume(player.get_volume() + 5);
      }
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        player.set_volume(player.get_volume() - 5);
      }
    }

    if (is_ui_visible) {
      // 5. Render Netflix-style Bottom Panel
      float bottom_panel_height = 140.0f;
      float panel_width = static_cast<float>(display_w);

      ImGui::SetNextWindowPos(ImVec2(0.0f, static_cast<float>(display_h) -
                                               bottom_panel_height - 30.0f),
                              ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(panel_width, bottom_panel_height),
                               ImGuiCond_Always);

      ImGuiWindowFlags flags =
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration;

      ImGui::PushStyleColor(ImGuiCol_WindowBg,
                            ImVec4(0, 0, 0, 0)); // fully transparent
      ImGui::Begin("Luplay Control Panel", nullptr, flags);
      ImGui::PopStyleColor();

      // Very thin Seek Bar which expands on hover
      double current_pos = player.get_position();
      double duration = player.get_duration();
      float seek_pos = static_cast<float>(current_pos);
      float max_dur = static_cast<float>(duration > 0.0 ? duration : 1.0);

      ImGui::PushItemWidth(-1.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 14.0f);
      std::string time_str =
          formatTime(current_pos) + " / " + formatTime(duration);

      // Adjust slider height manually by overriding frame padding
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 2.0f));
      if (ImGui::SliderFloat("##seek", &seek_pos, 0.0f, max_dur, "")) {
        player.set_position(static_cast<double>(seek_pos));
      }
      ImGui::PopStyleVar();

      ImGui::PopStyleVar(2);
      ImGui::PopItemWidth();

      // Row 2: Controls
      ImGui::Spacing();

      float controls_y = ImGui::GetCursorPosY();

      // 1. Time (Left)
      ImGui::SetCursorPosY(controls_y + 14.0f);
      ImGui::TextUnformatted(time_str.c_str());

      // 2. Center Block (-10s, Play/Pause, +10s)
      float center_block_width = 230.0f;
      float center_x = (panel_width - center_block_width) * 0.5f;
      ImGui::SameLine();
      ImGui::SetCursorPosX(center_x);
      ImGui::SetCursorPosY(controls_y);

      if (ImGui::Button("< 10s", ImVec2(80.0f, 50.0f))) {
        player.seek(-10.0);
      }
      ImGui::SameLine();
      ImGui::SetCursorPosY(controls_y);
      bool is_paused = player.is_paused();
      if (PlayPauseButton("##PlayPause", is_paused, ImVec2(50.0f, 50.0f))) {
        player.toggle_pause();
      }
      ImGui::SameLine();
      ImGui::SetCursorPosY(controls_y);
      if (ImGui::Button("10s >", ImVec2(80.0f, 50.0f))) {
        player.seek(10.0);
      }

      // 3. Right Block (Volume, CC)
      // === NASTAVENÍ VÝŠEK / POZIC PRO PRAVÝ BLOK (MŮŽEŠ ZDE SNADNO UPRAVIT
      // PIXELY) ===
      float cc_pos_y = controls_y + 10.0f;     // Výška tlačítka CC
      float vol_text_y = controls_y + 5.0f;    // Výška nápisu "Vol"
      float slider_pos_y = controls_y + 10.0f; // Výška posuvníku hlasitosti

      float right_block_width =
          220.0f; // CC (44) + Spacing + Vol + Slider (100)
      float right_x = panel_width - right_block_width - 20.0f;
      ImGui::SameLine();
      ImGui::SetCursorPosX(right_x);

      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));

      // 1. Tlačítko CC
      ImGui::SetCursorPosY(cc_pos_y);
      if (ImGui::Button("CC", ImVec2(44.0f, 32.0f))) {
        show_subtitle_window = !show_subtitle_window;
      }

      // 2. Text "Vol"
      ImGui::SameLine(0, 10.0f);
      ImGui::SetCursorPosY(vol_text_y);
      ImGui::TextUnformatted("Vol");

      // 3. Posuvník hlasitosti
      ImGui::SameLine(0, 10.0f);
      ImGui::SetCursorPosY(slider_pos_y);
      int current_vol = player.get_volume();
      ImGui::PushItemWidth(100.0f);
      if (ImGui::SliderInt("##Vol", &current_vol, 0, 100, "")) {
        player.set_volume(current_vol);
      }
      ImGui::PopItemWidth();

      ImGui::PopStyleVar();

      ImGui::End();

      // Top Menu Overlay (Transparent, floats on top)
      ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(panel_width, 60.0f), ImGuiCond_Always);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
      ImGui::Begin("TopMenu", nullptr, flags);
      ImGui::PopStyleColor();

      ImGui::SetCursorPosY(12.0f);
      if (ImGui::Button("Open Video", ImVec2(140.0f, 35.0f))) {
        std::string file = openFileDialog();
        if (!file.empty()) {
          player.load_file(file.c_str());
        }
      }

      ImGui::SameLine();
      ImGui::SetCursorPosY(12.0f);
      ImGui::PushItemWidth(250.0f);
      if (ImGui::BeginCombo("##AudioOut", "System Default (Sound)",
                            ImGuiComboFlags_NoArrowButton)) {
        ImGui::Selectable("System Default", true);
        ImGui::EndCombo();
      }
      ImGui::PopItemWidth();

      // Top Menu Drag Logic (Smooth non-blocking window movement without
      // freezing video)
      static bool is_dragging_window = false;
#ifdef _WIN32
      static POINT start_drag_cursor = {0, 0};
#else
      static double start_drag_cx = 0, start_drag_cy = 0;
#endif
      static int start_drag_win_x = 0, start_drag_win_y = 0;

      // Double-click top bar to toggle fullscreen (like standard window
      // titlebar)
      if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(0) &&
          !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
        toggleFullscreen(window);
        is_dragging_window = false;
      } else if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) &&
                 !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
        if (!is_fullscreen) {
          is_dragging_window = true;
#ifdef _WIN32
          GetCursorPos(&start_drag_cursor);
#else
          glfwGetCursorPos(window, &start_drag_cx, &start_drag_cy);
#endif
          glfwGetWindowPos(window, &start_drag_win_x, &start_drag_win_y);
        }
      }

      if (is_dragging_window) {
        if (ImGui::IsMouseDown(0)) {
          if (!is_fullscreen) {
#ifdef _WIN32
            POINT current_cursor;
            GetCursorPos(&current_cursor);
            int dx = current_cursor.x - start_drag_cursor.x;
            int dy = current_cursor.y - start_drag_cursor.y;
            glfwSetWindowPos(window, start_drag_win_x + dx,
                             start_drag_win_y + dy);
#else
            double cx, cy;
            glfwGetCursorPos(window, &cx, &cy);
            int dx = static_cast<int>(cx - start_drag_cx);
            int dy = static_cast<int>(cy - start_drag_cy);
            glfwSetWindowPos(window, start_drag_win_x + dx,
                             start_drag_win_y + dy);
#endif
          }
        } else {
          is_dragging_window = false;
        }
      }

      // Close, Min, Max Buttons
      float btn_size = 35.0f;
      // Subtracting 2 * ItemSpacing + width of 3 buttons.
      // Also add 15 pixels of explicit margin to be safe from any borders or
      // rounding.
      float right_offset = ImGui::GetWindowWidth() - (btn_size * 3) -
                           (ImGui::GetStyle().ItemSpacing.x * 2) -
                           ImGui::GetStyle().WindowPadding.x - 15.0f;
      ImGui::SameLine();
      ImGui::SetCursorPosX(right_offset);
      ImGui::SetCursorPosY(12.0f);

      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImVec4(0, 0, 0, 0)); // Transparent background
      if (ImGui::Button("-", ImVec2(btn_size, btn_size))) {
        glfwIconifyWindow(window);
      }
      ImGui::SameLine();
      if (ImGui::Button("[ ]", ImVec2(btn_size, btn_size))) {
        toggleFullscreen(window);
      }
      ImGui::SameLine();
      ImGui::PushStyleColor(
          ImGuiCol_ButtonHovered,
          ImVec4(0.8f, 0.1f, 0.1f, 1.0f)); // Red hover for close
      if (ImGui::Button("X", ImVec2(btn_size, btn_size))) {
        glfwSetWindowShouldClose(window, true);
      }
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar();

      ImGui::End();
    }

    // 6. Resize Handle (Pravý dolní roh)
    if (!is_fullscreen) {
      ImGui::SetNextWindowPos(ImVec2(static_cast<float>(display_w) - 20.0f,
                                     static_cast<float>(display_h) - 20.0f),
                              ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

      ImGuiWindowFlags resize_flags =
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;

      ImGui::Begin("ResizeHandle", nullptr, resize_flags);

      // Vykreslení malého trojúhelníku
      ImDrawList *draw_list = ImGui::GetWindowDrawList();
      ImVec2 p = ImGui::GetCursorScreenPos();
      draw_list->AddTriangleFilled(
          ImVec2(p.x + 20, p.y), ImVec2(p.x + 20, p.y + 20),
          ImVec2(p.x, p.y + 20), IM_COL32(150, 150, 150, 200));

      // 6. Resize Handle (Pravý dolní roh s přísným limitem min 800x600)
      static bool is_resizing = false;
#ifdef _WIN32
      static POINT start_resize_cursor = {0, 0};
#else
      static double start_resize_cx = 0, start_resize_cy = 0;
#endif
      static int start_resize_win_w = 0, start_resize_win_h = 0;

      ImGui::InvisibleButton("##rsz", ImVec2(20.0f, 20.0f));

      if (ImGui::IsItemHovered() || is_resizing) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
      }

      if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
        is_resizing = true;
#ifdef _WIN32
        GetCursorPos(&start_resize_cursor);
#else
        glfwGetCursorPos(window, &start_resize_cx, &start_resize_cy);
#endif
        glfwGetWindowSize(window, &start_resize_win_w, &start_resize_win_h);
      }

      if (is_resizing) {
        if (ImGui::IsMouseDown(0)) {
#ifdef _WIN32
          POINT current_cursor;
          GetCursorPos(&current_cursor);
          int dx = current_cursor.x - start_resize_cursor.x;
          int dy = current_cursor.y - start_resize_cursor.y;
          int target_w = (std::max)(800, start_resize_win_w + dx);
          int target_h = (std::max)(600, start_resize_win_h + dy);
          glfwSetWindowSize(window, target_w, target_h);
#else
          double cx, cy;
          glfwGetCursorPos(window, &cx, &cy);
          int dx = static_cast<int>(cx - start_resize_cx);
          int dy = static_cast<int>(cy - start_resize_cy);
          int target_w = (std::max)(800, start_resize_win_w + dx);
          int target_h = (std::max)(600, start_resize_win_h + dy);
          glfwSetWindowSize(window, target_w, target_h);
#endif
        } else {
          is_resizing = false;
        }
      }
      ImGui::End();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor();
    }

    // 5.5. Subtitles & AI Dedicated Window
    if (show_subtitle_window) {
      ImGui::SetNextWindowSize(ImVec2(450.0f, 320.0f), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Subtitles & AI", &show_subtitle_window)) {

        ImGui::Text("Available Subtitles");
        std::vector<luplay::SubtitleTrackInfo> sub_tracks =
            player.get_subtitle_tracks();
        std::string ext_sub = player.get_active_subtitle();

        std::vector<std::string> combo_items;
        combo_items.push_back("Off / None");

        int active_track_idx = 0;
        int current_sid = player.get_active_subtitle_track_id();

        for (size_t i = 0; i < sub_tracks.size(); ++i) {
          combo_items.push_back(sub_tracks[i].display_name);
          if (sub_tracks[i].selected || sub_tracks[i].id == current_sid) {
            active_track_idx = static_cast<int>(i) + 1;
          }
        }

        if (!ext_sub.empty()) {
          std::string ext_name =
              "External SRT: " +
              std::filesystem::path(ext_sub).filename().string();
          combo_items.push_back(ext_name);
          if (current_sid <= 0) {
            active_track_idx = static_cast<int>(combo_items.size()) - 1;
          }
        }

        std::vector<const char *> combo_ptrs;
        for (const auto &item : combo_items) {
          combo_ptrs.push_back(item.c_str());
        }

        ImGui::PushItemWidth(-1.0f);
        int selected_combo = active_track_idx;
        if (ImGui::Combo("##subtracks", &selected_combo, combo_ptrs.data(),
                         static_cast<int>(combo_ptrs.size()))) {
          if (selected_combo == 0) {
            player.select_subtitle_track(0);
          } else if (selected_combo <= static_cast<int>(sub_tracks.size())) {
            player.select_subtitle_track(sub_tracks[selected_combo - 1].id);
          }
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Subtitle Adjustments");

        if (ImGui::SliderInt("Position", &user_sub_pos, 0, 150)) {
          int target_pos = is_ui_visible ? (user_sub_pos - 18) : user_sub_pos;
          if (target_pos < 0)
            target_pos = 0;
          player.set_sub_pos(target_pos);
          current_applied_sub_pos = target_pos;
        }

        static float sub_scale = 1.0f;
        if (ImGui::SliderFloat("Scale", &sub_scale, 0.1f, 5.0f, "%.2fx")) {
          player.set_sub_scale(static_cast<double>(sub_scale));
        }

        static float sub_delay = 0.0f;
        if (ImGui::SliderFloat("Delay (s)", &sub_delay, -10.0f, 10.0f,
                               "%.2f s")) {
          player.set_sub_delay(static_cast<double>(sub_delay));
        }

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("AI Subtitle Generation (Whisper)");

        ImGui::PushItemWidth(-1.0f);
        ImGui::Text("Model Path");
        if (ImGui::InputText("##Model", model_input_buf,
                             sizeof(model_input_buf))) {
          transcriber.set_model_path(model_input_buf);
        }

        static int lang_combo_idx = 0;
        const char *lang_items[] = {"English Subtitles",
                                    "Czech Subtitles (DeepL API)"};
        ImGui::Text("Output Language");
        if (ImGui::Combo("##Lang", &lang_combo_idx, lang_items,
                         IM_ARRAYSIZE(lang_items))) {
          transcriber.set_target_language(
              lang_combo_idx == 1 ? luplay::TargetLanguage::Czech
                                  : luplay::TargetLanguage::English);
        }

        ImGui::PopItemWidth();

        bool use_gpu_val = transcriber.get_use_gpu();
        if (ImGui::Checkbox("GPU Acceleration", &use_gpu_val)) {
          transcriber.set_use_gpu(use_gpu_val);
        }

        ImGui::Spacing();

        trans_state = transcriber.get_state();
        if (trans_state == luplay::TranscriberState::ExtractingAudio ||
            trans_state == luplay::TranscriberState::Transcribing ||
            trans_state == luplay::TranscriberState::TranslatingAI) {

          float prog = transcriber.get_progress();
          char status_buf[128];
          if (trans_state == luplay::TranscriberState::ExtractingAudio) {
            snprintf(status_buf, sizeof(status_buf), "Extracting Audio: %d%%",
                     static_cast<int>(prog * 100.0f));
          } else if (trans_state == luplay::TranscriberState::Transcribing) {
            snprintf(status_buf, sizeof(status_buf), "Transcribing AI: %d%%",
                     static_cast<int>(prog * 100.0f));
          } else if (trans_state == luplay::TranscriberState::TranslatingAI) {
            snprintf(status_buf, sizeof(status_buf),
                     "Translating AI (DeepL): %d%%",
                     static_cast<int>(prog * 100.0f));
          }

          if (prog < 0.0f)
            prog = 1.0f; // Display full bar for translating since we don't have
                         // percentage

          ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                ImVec4(0.95f, 0.95f, 0.95f, 1.00f));
          ImGui::ProgressBar(prog, ImVec2(-1.0f, 28.0f), status_buf);
          ImGui::PopStyleColor();

          if (ImGui::Button("Cancel Generation", ImVec2(-1.0f, 28.0f))) {
            transcriber.cancel();
          }
        } else {
          bool file_ready = player.is_file_loaded();
          if (!file_ready)
            ImGui::BeginDisabled();

          if (ImGui::Button("Generate New Subtitles", ImVec2(-1.0f, 32.0f))) {
            transcriber.start_pipeline(player.get_current_filepath());
          }

          if (!file_ready)
            ImGui::EndDisabled();

          if (trans_state == luplay::TranscriberState::Done) {
            ImGui::TextColored(ImVec4(0.38f, 0.85f, 0.45f, 1.00f),
                               "Subtitles Auto-Loaded!");
          } else if (trans_state == luplay::TranscriberState::Error) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.00f), "Error: %s",
                               transcriber.get_last_error().c_str());
          }
        }
      }
      ImGui::End();
    }

    // Log Window Render
    if (show_log_window) {
      ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Application Logs", &show_log_window)) {
        if (ImGui::Button("Clear Logs")) {
          luplay::Logger::clear();
        }
        ImGui::Separator();

        ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        auto logs = luplay::Logger::get_logs();
        for (const auto &log : logs) {
          ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // Info (White)
          if (log.level == luplay::LogLevel::Warning)
            color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
          else if (log.level == luplay::LogLevel::Error)
            color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
          ImGui::TextUnformatted(log.timestamp.c_str());
          ImGui::PopStyleColor();

          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_Text, color);
          ImGui::TextWrapped("%s", log.message.c_str());
          ImGui::PopStyleColor();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
          ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
      }
      ImGui::End();
    }

    // 6. Render the Final UI overlays on top
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);

    // Frame rate limiter when idle/paused to save battery and reduce idle GPU
    // load
    if (player.is_paused() || !player.is_file_loaded()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  }

  // Teardown
  g_player_ptr = nullptr;
  transcriber.cancel();
  player.shutdown();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  luplay::Logger::info("Shutdown complete.");
  return EXIT_SUCCESS;
}
