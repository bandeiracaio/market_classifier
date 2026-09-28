// Market Classifier terminal — platform entry point.
//
// Creates the SDL window and GL context, initializes Dear ImGui (docking) and ImPlot,
// and runs the frame loop. The terminal UI itself lives in app/app.cpp.
//
// Platform matrix:
//   Native Windows/Linux: SDL2 + OpenGL 3.3 Core + imgui_impl_opengl3
//   WASM/Emscripten:      SDL2 port + OpenGL ES 3 / WebGL 2 + imgui_impl_opengl3
//
// Refs:
//   Dear ImGui Emscripten example: imgui/examples/example_emscripten_opengl3
//   Emscripten set_main_loop: https://emscripten.org/docs/api_reference/emscripten.h.html
//   SDL2 OpenGL ES 3 on Emscripten:
//   https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html Verification
//   date: 2026-09-24

#include "market_classifier/bridge/browser_api.hpp"
#include "market_classifier/bridge/raw_frame.hpp"
#include "market_classifier/version.hpp"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "implot.h"

#include "app/app.hpp"

// On native Windows/Linux: prevent SDL2 from redefining main() via SDL_main.h.
// SDL_SetMainReady() must be called before SDL_Init() when this macro is defined.
// On Emscripten: Emscripten manages the entry point itself; this macro is not needed.
#ifndef __EMSCRIPTEN__
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>

#ifdef __EMSCRIPTEN__
// OpenGL ES 3 / WebGL 2
#include <emscripten.h>

#include <GLES3/gl3.h>
#else
// Desktop OpenGL — provided by the platform or by SDL2's OpenGL headers
#include <SDL2/SDL_opengl.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr int k_timing_sample_frames = 120;
#ifdef __EMSCRIPTEN__
constexpr int k_timing_warmup_frames = 30;
#endif

// ---------------------------------------------------------------------------
// Application state — struct kept here so main_loop_step can be a plain function
// pointer as required by emscripten_set_main_loop.
// ---------------------------------------------------------------------------
struct AppState {
    SDL_Window *window       = nullptr;
    SDL_GLContext gl_context = nullptr;
    bool done                = false;
    bool ready_sent          = false;
    bool metrics_sent        = false;
    bool bridge_smoke_sent   = false;
    int frame                = 0;
    std::array<double, k_timing_sample_frames> render_ms{};
    int render_sample_count = 0;
};

// emscripten_set_main_loop takes a plain function pointer, so the loop state lives behind
// a function-local static rather than a mutable global.
AppState &state() {
    static AppState instance{};
    return instance;
}

// ---------------------------------------------------------------------------
// JS signals — set data attributes readable by Playwright smoke tests.
// Called only from the WASM build; no-ops in native.
// ---------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
// clang-format off
EM_JS(void, js_set_wasm_ready, (), {
    document.body.dataset.wasmStatus = 'ready';
    document.body.dataset.wasmReadyMs = performance.now().toFixed(3);
});

EM_JS(void, js_increment_frame_count, (), {
    var n = parseInt(document.body.dataset.frameCount || '0');
    document.body.dataset.frameCount = String(n + 1);
});

EM_JS(void, js_publish_render_metrics, (double mean_ms, double p95_ms), {
    document.body.dataset.renderMeanMs = mean_ms.toFixed(3);
    document.body.dataset.renderP95Ms = p95_ms.toFixed(3);
    document.body.dataset.wasmHeapBytes = String(HEAPU8.buffer.byteLength);
});
EM_JS(void, js_exercise_bridge, (const uint8_t* bytes, int size), {
    // Decode-only smoke: never submits, so no synthetic data reaches live views.
    const validResult = Module._mc_bridge_decode(bytes, size);
    const savedVersion = HEAPU8[bytes + 4];
    HEAPU8[bytes + 4] = savedVersion + 1;
    const invalidResult = Module._mc_bridge_decode(bytes, size);
    HEAPU8[bytes + 4] = savedVersion;

    const name = (code) => UTF8ToString(Module._mc_bridge_result_name(code));
    document.body.dataset.bridgeValidResult = name(validResult);
    document.body.dataset.bridgeInvalidError = name(invalidResult);
    document.body.dataset.bridgePayloadBytes = String(size);

    for (let i = 0; i < 50; ++i) Module._mc_bridge_decode(bytes, size);
    const started = performance.now();
    const iterations = 1000;
    for (let i = 0; i < iterations; ++i) Module._mc_bridge_decode(bytes, size);
    document.body.dataset.bridgeDecodeMeanMs = ((performance.now() - started) / iterations).toFixed(6);
});
// clang-format on

void RunBridgeSmokeOnce() {
    const std::vector<market_classifier::bridge::RawFrame> frames{
        {market_classifier::venues::StreamTag::BinanceWs, 1'700'000'000'007,
         R"({"stream":"btcusdt@aggTrade","data":{}})"}};
    const auto bytes = market_classifier::bridge::encode_raw_frames(frames);
    if (!bytes.empty()) {
        js_exercise_bridge(bytes.data(), static_cast<int>(bytes.size()));
    }
}
#endif

// ---------------------------------------------------------------------------
// Main loop step — must be a plain void() function for emscripten_set_main_loop.
// ---------------------------------------------------------------------------
void main_loop_step() noexcept {
#ifdef __EMSCRIPTEN__
    const double render_started_ms = emscripten_get_now();
    if (!state().bridge_smoke_sent) {
        RunBridgeSmokeOnce();
        state().bridge_smoke_sent = true;
    }
#endif
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) {
            state().done = true;
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID == SDL_GetWindowID(state().window)) {
            state().done = true;
        }
    }

    market_classifier::app::prepare(); // layout switches load ini outside the frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    market_classifier::app::frame();

    // Render
    ImGui::Render();
    ImGuiIO &io = ImGui::GetIO();
    glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
    glClearColor(0.12F, 0.12F, 0.13F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(state().window);

    ++state().frame;

#ifdef __EMSCRIPTEN__
    if (state().frame > k_timing_warmup_frames &&
        state().render_sample_count < k_timing_sample_frames) {
        state().render_ms[static_cast<std::size_t>(state().render_sample_count)] =
            emscripten_get_now() - render_started_ms;
        ++state().render_sample_count;
    }

    if (!state().metrics_sent && state().render_sample_count == k_timing_sample_frames) {
        auto sorted_samples = state().render_ms;
        std::sort(sorted_samples.begin(), sorted_samples.end());

        double total_ms = 0.0;
        for (const double sample_ms : state().render_ms) {
            total_ms += sample_ms;
        }

        constexpr std::size_t p95_index =
            static_cast<std::size_t>(k_timing_sample_frames * 95 / 100) - 1;
        js_publish_render_metrics(total_ms / k_timing_sample_frames, sorted_samples[p95_index]);
        state().metrics_sent = true;
    }

    if (!state().ready_sent) {
        js_set_wasm_ready();
        state().ready_sent = true;
    }
    js_increment_frame_count();
#endif
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int /*argc*/, char * /*argv*/[]) {
#ifndef __EMSCRIPTEN__
    SDL_SetMainReady(); // required when SDL_MAIN_HANDLED is defined
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

#ifdef __EMSCRIPTEN__
    // Emscripten/WebGL 2 — request OpenGL ES 3.0 context.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    const char *glsl_version = "#version 300 es";
#else
    // Desktop — request OpenGL 3.3 Core.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    const char *glsl_version = "#version 330 core";
#endif

    // Plain Uint32: SDL_CreateWindow takes OR-ed flags, which are not a single enumerator.
    constexpr Uint32 k_window_flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

    state().window = SDL_CreateWindow("Market Classifier", SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED, 1280, 720, k_window_flags);
    if (state().window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    state().gl_context = SDL_GL_CreateContext(state().window);
    if (state().gl_context == nullptr) {
        std::fprintf(stderr, "SDL_GL_CreateContext error: %s\n", SDL_GetError());
        SDL_DestroyWindow(state().window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_MakeCurrent(state().window, state().gl_context);
    SDL_GL_SetSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Multi-viewport requires native OS windows; incompatible with browser canvas.
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(state().window, state().gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);
    market_classifier::app::init();

#ifdef __EMSCRIPTEN__
    // Browser requires a non-blocking loop driven by requestAnimationFrame.
    // 0 fps = use browser's own frame scheduling.
    emscripten_set_main_loop(main_loop_step, 0, true);
    // Execution does not return past this point in WASM.
#else
    while (!state().done) {
        main_loop_step();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(state().gl_context);
    SDL_DestroyWindow(state().window);
    SDL_Quit();
#endif

    return 0;
}
