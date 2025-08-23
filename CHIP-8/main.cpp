#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>

#include <SDL3/SDL.h>

#include "tinyfiledialogs/tinyfiledialogs.h"

#include <imgui.h>
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

using namespace std;


const uint32_t entry_point = 0x200; // CHIP-8 ROMs start at 0x200

// SDL container struct
struct Sdl {
    SDL_Window* window{};
    SDL_Renderer* renderer{};
};

// Emulator configuration structure
struct Config {
    uint32_t window_width;  // SDL window width
    uint32_t window_height; // SDL window height
    uint32_t fg_color;      // Foreground color RGBA8888
    uint32_t bg_color;      // Background color RGBA8888
    uint32_t scale_factor;  // Scale factor for the window
};

// Emulator state
enum class EmulatorState {
    QUIT,
    RUNNING,
    PAUSE,
};

// Debug window flags
struct DebugWindows {
    bool show_registers = false;
    bool show_memory = false;
    bool show_stack = false;
    bool show_display = false;
    bool show_keypad = false;
};

// CHIP-8 instruction format
struct instructions {
    uint16_t opcode; //operation code
    uint16_t NNN : 12; //12 bit address
    uint16_t NN : 12;  //8 bit constant
    uint16_t N : 4;    //4 bit constant
    uint16_t X : 4;    //4 bit register identifier
    uint16_t Y : 4;    //4 bit register identifier
    uint16_t kk : 8;
};

// CHIP-8 Machine object
struct Chip8 {
    EmulatorState state{};
    uint8_t memory[4096]{};     // 4KB of memory
    bool display[64 * 32]{};    // 64x32 pixel display
    uint16_t stack[12]{};       // 12 levels of stack
    uint8_t V[16]{};            // 16 general-purpose registers
    uint16_t I{};               // Index register
    uint16_t pc{};              // Program counter
    uint8_t delay_timer{};      // Decrements at 60Hz
    uint8_t sound_timer{};      // Decrements at 60Hz
    bool keypad[16]{};          // Hex-based keypad (0x0-0xF)
    string rom;                 // Path to the loaded ROM
    DebugWindows debug_windows; // Debug window visibility flags
	instructions inst{};        // Currently executing instruction
};

bool init_sdl(Sdl& sdl, const Config& config) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("Unable to initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    sdl.window = SDL_CreateWindow("CHIP-8 Emulator",
        config.window_width * config.scale_factor,
        config.window_height * config.scale_factor,
        0);

    if (!sdl.window) {
        SDL_Log("Could not create window: %s\n", SDL_GetError());
        return false;
    }

    sdl.renderer = SDL_CreateRenderer(sdl.window, nullptr);
    SDL_SetRenderVSync(sdl.renderer, 1);

    if (!sdl.renderer) {
        SDL_Log("Could not create renderer: %s\n", SDL_GetError());
        return false;
    }

    SDL_ShowWindow(sdl.window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();

    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(sdl.window, sdl.renderer);
    ImGui_ImplSDLRenderer3_Init(sdl.renderer);

    return true;
}

bool set_config_from_args(Config& config) {
    config = {
        64,          // CHIP8 original X resolution
        32,          // CHIP8 original Y resolution
        0xFFFFFFFF,  // WHITE (foreground)
        0x000000FF,  // BLACK (background)
        20           // Scale
    };
    return true;
}

bool loadROM(Chip8& chip8, const string& rom) {
    ifstream rom_file(rom, ios::binary | ios::ate);
    if (!rom_file) {
        SDL_Log("Invalid ROM file: %s\n", rom.c_str());
        return false;
    }

    streamsize rom_size = rom_file.tellg();
    const size_t max_size = sizeof(chip8.memory) - entry_point;

    if (rom_size > static_cast<streamsize>(max_size)) {
        SDL_Log("ROM too large to fit in memory: %s\n", rom.c_str());
        return false;
    }

    rom_file.seekg(0, ios::beg);

    if (!rom_file.read(reinterpret_cast<char*>(&chip8.memory[entry_point]), rom_size)) {
        SDL_Log("Failed to read ROM file: %s\n", rom.c_str());
        return false;
    }

    chip8.rom = rom;
    return true;
}

bool init_chip8(Chip8& chip8) {
    const uint8_t font[80] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };

    // Load font
    memcpy(&chip8.memory[0], font, sizeof(font));

    chip8.state = EmulatorState::RUNNING;
    chip8.pc = entry_point;

    return true;
}

void final_cleanup(const Sdl& sdl) {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

void clear_screen(const Sdl& sdl, const Config& config) {
    ImGuiIO& io = ImGui::GetIO();

	// Set the background color from the configuration
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >> 8) & 0xFF;
    const uint8_t a = (config.bg_color >> 0) & 0xFF;

    SDL_SetRenderScale(sdl.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

void update_screen(const Sdl& sdl) {
	// Present the rendered frame to the screen
    SDL_RenderPresent(sdl.renderer);
}

void handle_input(Chip8& chip8, Sdl& sdl) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(sdl.window))) {
            chip8.state = EmulatorState::QUIT;
        }

        if (event.type == SDL_EVENT_KEY_DOWN) {
            switch (event.key.key) {
                case SDLK_ESCAPE:
                    chip8.state = EmulatorState::QUIT;
                    break;

                case SDLK_SPACE:
                    chip8.state = (chip8.state == EmulatorState::RUNNING) ? EmulatorState::PAUSE : EmulatorState::RUNNING;
                    break;

                default:
                    break;
            }
        }
    }
}

void create_main_menu_bar(Chip8& chip8) {
    if (ImGui::BeginMainMenuBar()) {
        // File Menu
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Load ROM", "Ctrl+O")) {
                const char* filterPatterns[1] = { "*.ch8" };
                const char* filePath = tinyfd_openFileDialog(
                    "Select CHIP-8 ROM",     // dialog title
                    "",                      // default path
                    1,                       // number of filter patterns
                    filterPatterns,          // filter patterns
                    "CHIP-8 ROM files",      // filter description
                    0                        // allow multiple selections (0 = single)
                );

                if (filePath && loadROM(chip8, string(filePath))) {
                    SDL_Log("ROM loaded successfully: %s", filePath);
                }
                else if (filePath) {
                    SDL_Log("Failed to load ROM: %s", filePath);
                }
            }

            if (ImGui::MenuItem("Load Test ROM", "Ctrl+T")) {
                // TODO: Load a predefined test ROM
                SDL_Log("Load Test ROM selected");
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                chip8.state = EmulatorState::QUIT;
            }

            ImGui::EndMenu();
        }

        // Debug Menu
        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("Registers", nullptr, &chip8.debug_windows.show_registers);
            ImGui::MenuItem("Memory Viewer", nullptr, &chip8.debug_windows.show_memory);
            ImGui::MenuItem("Stack", nullptr, &chip8.debug_windows.show_stack);
            ImGui::MenuItem("Display Buffer", nullptr, &chip8.debug_windows.show_display);
            ImGui::MenuItem("Keypad State", nullptr, &chip8.debug_windows.show_keypad);

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void create_viewport(Chip8& chip8, const Config& config) {
    if (ImGui::Begin("CHIP-8 Display", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        //Get the content region available for drawing
        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        ImVec2 canvas_sz = ImGui::GetContentRegionAvail();

        //minimum size for visibility
        if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
        if (canvas_sz.y < 50.0f) canvas_sz.y = 50.0f;

        ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

        //Draw background
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(0, 0, 0, 255)); // Black background

        //Calculate pixel size based on available space while maintaining aspect ratio (64x32)
        float scale_x = canvas_sz.x / 64.0f;
        float scale_y = canvas_sz.y / 32.0f;
        float pixel_scale = (scale_x < scale_y) ? scale_x : scale_y; // Use smaller scale to maintain aspect ratio

        //Calculate centered offset
        float display_width = 64.0f * pixel_scale;
        float display_height = 32.0f * pixel_scale;
        float offset_x = (canvas_sz.x - display_width) * 0.5f;
        float offset_y = (canvas_sz.y - display_height) * 0.5f;

        //Draw CHIP-8 display pixels
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 64; x++) {
                if (chip8.display[y * 64 + x]) {
                    float px = canvas_p0.x + offset_x + (x * pixel_scale);
                    float py = canvas_p0.y + offset_y + (y * pixel_scale);

                    ImVec2 pixel_p0 = ImVec2(px, py);
                    ImVec2 pixel_p1 = ImVec2(px + pixel_scale, py + pixel_scale);

                    //Use foreground color from config
                    uint32_t fg_color = config.fg_color;
                    uint8_t r = (fg_color >> 24) & 0xFF;
                    uint8_t g = (fg_color >> 16) & 0xFF;
                    uint8_t b = (fg_color >> 8) & 0xFF;
                    uint8_t a = (fg_color >> 0) & 0xFF;

                    draw_list->AddRectFilled(pixel_p0, pixel_p1, IM_COL32(r, g, b, a));
                }
            }
        }

        //Add a border around the display area
        ImVec2 display_p0 = ImVec2(canvas_p0.x + offset_x, canvas_p0.y + offset_y);
        ImVec2 display_p1 = ImVec2(display_p0.x + display_width, display_p0.y + display_height);
        draw_list->AddRect(display_p0, display_p1, IM_COL32(128, 128, 128, 255)); // Gray border

        // Make the canvas interactable (this reserves the space)
        ImGui::InvisibleButton("canvas", canvas_sz);
    }
    ImGui::End();
}

void create_debug_windows(Chip8& chip8) {
    // Registers Window
    if (chip8.debug_windows.show_registers) {
        if (ImGui::Begin("Registers", &chip8.debug_windows.show_registers)) {
            ImGui::Text("Program Counter: 0x%04X", chip8.pc);
            ImGui::Text("Index Register (I): 0x%04X", chip8.I);
            ImGui::Text("Delay Timer: %d", chip8.delay_timer);
            ImGui::Text("Sound Timer: %d", chip8.sound_timer);

            ImGui::Separator();
            ImGui::Text("General Purpose Registers:");

            for (int i = 0; i < 16; i++) {
                ImGui::Text("V%X: 0x%02X (%d)", i, chip8.V[i], chip8.V[i]);
            }
        }

        ImGui::End();
    }

    // Memory Viewer Window
    if (chip8.debug_windows.show_memory) {
        if (ImGui::Begin("Memory Viewer", &chip8.debug_windows.show_memory)) {
            ImGui::Text("Memory contents (first 512 bytes):");

            for (int i = 0; i < 512; i += 16) {
                ImGui::Text("%04X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                    i,
                    chip8.memory[i], chip8.memory[i + 1], chip8.memory[i + 2], chip8.memory[i + 3],
                    chip8.memory[i + 4], chip8.memory[i + 5], chip8.memory[i + 6], chip8.memory[i + 7],
                    chip8.memory[i + 8], chip8.memory[i + 9], chip8.memory[i + 10], chip8.memory[i + 11],
                    chip8.memory[i + 12], chip8.memory[i + 13], chip8.memory[i + 14], chip8.memory[i + 15]);
            }
        }

        ImGui::End();
    }

    // Stack Window
    if (chip8.debug_windows.show_stack) {
        if (ImGui::Begin("Stack", &chip8.debug_windows.show_stack)) {
            ImGui::Text("Stack contents:");

            for (int i = 0; i < 12; i++) {
                ImGui::Text("Stack[%d]: 0x%04X", i, chip8.stack[i]);
            }
        }
        ImGui::End();
    }

    // Display Buffer Window
    if (chip8.debug_windows.show_display) {
        if (ImGui::Begin("Display Buffer", &chip8.debug_windows.show_display)) {
            ImGui::Text("64x32 Display Buffer:");

            for (int y = 0; y < 32; y++) {
                string row = "";

                for (int x = 0; x < 64; x++) {
                    row += chip8.display[y * 64 + x] ? "#" : ".";
                }

                ImGui::Text("%s", row.c_str());
            }
        }
        ImGui::End();
    }

    // Keypad State Window
    if (chip8.debug_windows.show_keypad) {
        if (ImGui::Begin("Keypad State", &chip8.debug_windows.show_keypad)) {
            ImGui::Text("Keypad state (0x0-0xF):");

            for (int i = 0; i < 16; i++) {
                ImGui::Text("Key %X: %s", i, chip8.keypad[i] ? "PRESSED" : "released");
            }
        }
        ImGui::End();
    }
}

void create_control_panel(Chip8& chip8) {
    // Control Panel Window
    if (ImGui::Begin("Control Panel")) {
        ImGui::Text("ROM: %s", chip8.rom.empty() ? "No ROM loaded" : chip8.rom.c_str());
        ImGui::Text("State: %s",
            (chip8.state == EmulatorState::RUNNING ? "Running" :
                (chip8.state == EmulatorState::PAUSE ? "Paused" : "Quit")));

        if (ImGui::Button("Pause/Resume")) {
            chip8.state = (chip8.state == EmulatorState::RUNNING) ? EmulatorState::PAUSE : EmulatorState::RUNNING;
        }

        ImGui::SameLine();
        if (ImGui::Button("Quit")) {
            chip8.state = EmulatorState::QUIT;
        }
    }
    ImGui::End();
}

void create_imgui_interface(Chip8& chip8, Sdl& sdl, const Config& config) {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Create main menu bar at the top of the application window
    create_main_menu_bar(chip8);

    // Create CHIP-8 display viewport
    create_viewport(chip8, config);

    // Create control panel window
    create_control_panel(chip8);

    // Create debug windows
    create_debug_windows(chip8);

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sdl.renderer);
}

void emulate_instructions(Chip8 *chip8) {
	chip8->inst.opcode = (chip8->memory[chip8->pc] << 8) | chip8->memory[chip8->pc + 1]; //big endian
	chip8->pc += 2; //pre-increment program counter for next opcode

    //Fill out current instruction format
	chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x0FF;
    chip8->inst.N = chip8->inst.opcode & 0x0F;

    //emulate opcode
    switch (chip8->inst.opcode) {

    }
}

int main(int argc, char* argv[]) {
    Config config{};
    if (!set_config_from_args(config))
        return EXIT_FAILURE;

    Sdl sdl{};
    if (!init_sdl(sdl, config))
        return EXIT_FAILURE;

    Chip8 chip8{};
    if (!init_chip8(chip8))
        return EXIT_FAILURE;

    while (chip8.state != EmulatorState::QUIT) {
        if (chip8.state == EmulatorState::PAUSE) continue;

        // Clear screen FIRST, before rendering ImGui
        clear_screen(sdl, config);

        handle_input(chip8, sdl);

		//Emulate CHIP8 instructions here (not implemented)
        emulate_instructions(&chip8);

        // Render ImGui interface AFTER clearing screen
        create_imgui_interface(chip8, sdl, config);

        // Present the final rendered frame
        update_screen(sdl);

        SDL_Delay(16); // ~60fps
    }

    final_cleanup(sdl);
    return EXIT_SUCCESS;
}