#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <time.h>

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
	uint32_t clock_rate;    // CHIP8 CPU "clock rate" or Hz
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
    uint16_t NN : 8;  //8 bit constant
    uint16_t N : 4;    //4 bit constant
    uint16_t X : 4;    //4 bit register identifier
    uint16_t Y : 4;    //4 bit register identifier
};

// CHIP-8 Machine object
struct Chip8 {
    EmulatorState state{};
    uint8_t memory[4096]{};     // 4KB of memory
    bool display[64 * 32]{};    // 64x32 pixel display
    uint16_t stack[12]{};       // 12 levels of stack
	uint16_t *sp;                // Stack pointer
    uint8_t V[16]{};            // 16 general-purpose registers
    uint16_t I{};               // Index register
    uint16_t pc{};              // Program counter
    uint8_t delay_timer{};      // Decrements at 60Hz
    uint8_t sound_timer{};      // Decrements at 60Hz
    bool keypad[16]{};          // Hex-based keypad (0x0-0xF)
    string rom;                 // Path to the loaded ROM
    DebugWindows debug_windows; // Debug window visibility flags
	instructions inst{};        // Currently executing instruction

    //debug tracking
    bool debug_mode = false;
    uint32_t instructions_executed = 0;
    uint16_t last_opcode = 0;
    bool sprite_drawn_this_frame = false;
    uint8_t last_sprite_x = 0, last_sprite_y = 0, last_sprite_height = 0;
    uint16_t last_sprite_address = 0;
    bool collision_detected = false;
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
        SDL_WINDOW_RESIZABLE);

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

    //Setup Dear ImGui style
    ImGui::StyleColorsDark();

    //Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();

    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    //Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(sdl.window, sdl.renderer);
    ImGui_ImplSDLRenderer3_Init(sdl.renderer);

    return true;
}

bool set_config_from_args(Config& config) {
    config = {
        64,             // CHIP8 original X resolution
        32,             // CHIP8 original Y resolution
        0xFFFFFFFF,     // WHITE (foreground)
        0x000000FF,     // BLACK (background)
        35,             // Scale
		700             // Number of instructions to emulate per second
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

    //clear ROM area for new ROM to load
    memset(&chip8.memory[entry_point], 0, sizeof(chip8.memory) - entry_point);

    rom_file.seekg(0, ios::beg);

    if (!rom_file.read(reinterpret_cast<char*>(&chip8.memory[entry_point]), rom_size)) {
        SDL_Log("Failed to read ROM file: %s\n", rom.c_str());
        return false;
    }

    chip8.rom = rom;
    chip8.state = EmulatorState::RUNNING;

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

    //Clear all memory first
    memset(chip8.memory, 0, sizeof(chip8.memory));

    //Load font
    memcpy(&chip8.memory[0], font, sizeof(font));

    //Clear display
    memset(chip8.display, 0, sizeof(chip8.display));

    //Clear registers
    memset(chip8.V, 0, sizeof(chip8.V));

    //Clear stack
    memset(chip8.stack, 0, sizeof(chip8.stack));

    //Clear keypad
    memset(chip8.keypad, 0, sizeof(chip8.keypad));

    //Reset all other state
    chip8.state = EmulatorState::PAUSE;
    chip8.pc = entry_point;
    chip8.sp = &chip8.stack[0];
    chip8.I = 0;
    chip8.delay_timer = 0;
    chip8.sound_timer = 0;

    //Reset debug state
    chip8.instructions_executed = 0;
    chip8.last_opcode = 0;
    chip8.sprite_drawn_this_frame = false;
    chip8.last_sprite_x = 0;
    chip8.last_sprite_y = 0;
    chip8.last_sprite_height = 0;
    chip8.last_sprite_address = 0;
    chip8.collision_detected = false;

    //Reset instruction format
    memset(&chip8.inst, 0, sizeof(chip8.inst));

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

void update_screen(const Sdl& sdl, const Chip8 chip8) {
	// Present the rendered frame to the screen
    SDL_RenderPresent(sdl.renderer);
}

//Handle user input
//CHIP8 KEYPAD      QWERTY
//123C              1234
//456D              QWER
//789E              ASDF
//A0BF              ZXCV
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

				// Map QWERTY keys to CHIP-8 keypad
				case SDLK_1: chip8.keypad[0x1] = true; break;
                case SDLK_2: chip8.keypad[0x2] = true; break;
                case SDLK_3: chip8.keypad[0x3] = true; break;
                case SDLK_4: chip8.keypad[0xC] = true; break;

                case SDLK_Q: chip8.keypad[0x4] = true; break;
                case SDLK_W: chip8.keypad[0x5] = true; break;
                case SDLK_E: chip8.keypad[0x6] = true; break;
                case SDLK_R: chip8.keypad[0xD] = true; break;

                case SDLK_A: chip8.keypad[0x7] = true; break;
                case SDLK_S: chip8.keypad[0x8] = true; break;
                case SDLK_D: chip8.keypad[0x9] = true; break;
                case SDLK_F: chip8.keypad[0xE] = true; break;

                case SDLK_Z: chip8.keypad[0xA] = true; break;
                case SDLK_X: chip8.keypad[0x0] = true; break;
                case SDLK_C: chip8.keypad[0xB] = true; break;
                case SDLK_V: chip8.keypad[0xF] = true; break;

                default:
                    break;
            }
        }
        if (event.type == SDL_EVENT_KEY_UP) {
            switch (event.key.key) {
                // Map QWERTY keys to CHIP-8 keypad
                case SDLK_1: chip8.keypad[0x1] = false; break;
                case SDLK_2: chip8.keypad[0x2] = false; break;
                case SDLK_3: chip8.keypad[0x3] = false; break;
                case SDLK_4: chip8.keypad[0xC] = false; break;

                case SDLK_Q: chip8.keypad[0x4] = false; break;
                case SDLK_W: chip8.keypad[0x5] = false; break;
                case SDLK_E: chip8.keypad[0x6] = false; break;
                case SDLK_R: chip8.keypad[0xD] = false; break;

                case SDLK_A: chip8.keypad[0x7] = false; break;
                case SDLK_S: chip8.keypad[0x8] = false; break;
                case SDLK_D: chip8.keypad[0x9] = false; break;
                case SDLK_F: chip8.keypad[0xE] = false; break;

                case SDLK_Z: chip8.keypad[0xA] = false; break;
                case SDLK_X: chip8.keypad[0x0] = false; break;
                case SDLK_C: chip8.keypad[0xB] = false; break;
                case SDLK_V: chip8.keypad[0xF] = false; break;

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

                if (filePath) {
                    // Reset the CHIP-8 system to initial state FIRST
                    if (init_chip8(chip8)) {
                        if (loadROM(chip8, string(filePath))) {
                            SDL_Log("ROM loaded successfully: %s", filePath);
                        }
                        else {
                            SDL_Log("Failed to load ROM: %s", filePath);
                        }
                    }
                }
            }

            if (ImGui::MenuItem("Load Test ROM", "Ctrl+T")) {
                if (init_chip8(chip8)) {
                    if (loadROM(chip8, string("C:\\Users\\Anshuman Dhillon\\Desktop\\Projects\\CHIP-8\\chip8-roms-master\\programs\\IBM Logo.ch8"))) {
                        SDL_Log("Test ROM loaded successfully");
                    }
                }
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
        float scale_x = canvas_sz.x / (float)config.window_width;
        float scale_y = canvas_sz.y / (float)config.window_height;
        float pixel_scale = (scale_x < scale_y) ? scale_x : scale_y; // Use smaller scale to maintain aspect ratio

        //Calculate centered offset
        float display_width = (float)config.window_width * pixel_scale;
        float display_height = (float)config.window_height * pixel_scale;
        float offset_x = (canvas_sz.x - display_width) * 0.5f;
        float offset_y = (canvas_sz.y - display_height) * 0.5f;

        //Draw CHIP-8 display pixels
        for (int y = 0; y < config.window_height; y++) {
            for (int x = 0; x < config.window_width; x++) {
                if (chip8.display[y * config.window_width + x]) {
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

// function to decode instructions into assembly mnemonics
std::string decode_instruction(uint16_t opcode) {
    switch ((opcode >> 12) & 0x0F) {
        case 0x0:
            if (opcode == 0x00E0) return "CLS";
            if (opcode == 0x00EE) return "RET";
            return "SYS " + std::to_string(opcode & 0x0FFF);

        case 0x1: return "JP " + std::to_string(opcode & 0x0FFF);
        case 0x2: return "CALL " + std::to_string(opcode & 0x0FFF);
        case 0x3: return "SE V" + std::to_string((opcode >> 8) & 0x0F) + ", " + std::to_string(opcode & 0xFF);
        case 0x4: return "SNE V" + std::to_string((opcode >> 8) & 0x0F) + ", " + std::to_string(opcode & 0xFF);
        case 0x5: return "SE V" + std::to_string((opcode >> 8) & 0x0F) + ", V" + std::to_string((opcode >> 4) & 0x0F);
        case 0x6: return "LD V" + std::to_string((opcode >> 8) & 0x0F) + ", " + std::to_string(opcode & 0xFF);
        case 0x7: return "ADD V" + std::to_string((opcode >> 8) & 0x0F) + ", " + std::to_string(opcode & 0xFF);

        case 0x8: {
            std::string reg1 = "V" + std::to_string((opcode >> 8) & 0x0F);
            std::string reg2 = "V" + std::to_string((opcode >> 4) & 0x0F);

            switch (opcode & 0x0F) {
                case 0: return "LD " + reg1 + ", " + reg2;
                case 1: return "OR " + reg1 + ", " + reg2;
                case 2: return "AND " + reg1 + ", " + reg2;
                case 3: return "XOR " + reg1 + ", " + reg2;
                case 4: return "ADD " + reg1 + ", " + reg2;
                case 5: return "SUB " + reg1 + ", " + reg2;
                case 6: return "SHR " + reg1;
                case 7: return "SUBN " + reg1 + ", " + reg2;
                case 0xE: return "SHL " + reg1;
                default: return "8XY?";
            }
        }

        case 0x9: return "SNE V" + std::to_string((opcode >> 8) & 0x0F) + ", V" + std::to_string((opcode >> 4) & 0x0F);
        case 0xA: return "LD I, " + std::to_string(opcode & 0x0FFF);
        case 0xB: return "JP V0, " + std::to_string(opcode & 0x0FFF);
        case 0xC: return "RND V" + std::to_string((opcode >> 8) & 0x0F) + ", " + std::to_string(opcode & 0xFF);
        case 0xD: return "DRW V" + std::to_string((opcode >> 8) & 0x0F) + ", V" + std::to_string((opcode >> 4) & 0x0F) + ", " + std::to_string(opcode & 0x0F);

        case 0xE:
            if ((opcode & 0xFF) == 0x9E) return "SKP V" + std::to_string((opcode >> 8) & 0x0F);
            if ((opcode & 0xFF) == 0xA1) return "SKNP V" + std::to_string((opcode >> 8) & 0x0F);
            return "EX??";

        case 0xF:
            switch (opcode & 0xFF) {
                case 0x07: return "LD V" + std::to_string((opcode >> 8) & 0x0F) + ", DT";
                case 0x0A: return "LD V" + std::to_string((opcode >> 8) & 0x0F) + ", K";
                case 0x15: return "LD DT, V" + std::to_string((opcode >> 8) & 0x0F);
                case 0x18: return "LD ST, V" + std::to_string((opcode >> 8) & 0x0F);
                case 0x1E: return "ADD I, V" + std::to_string((opcode >> 8) & 0x0F);
                case 0x29: return "LD F, V" + std::to_string((opcode >> 8) & 0x0F);
                case 0x33: return "LD B, V" + std::to_string((opcode >> 8) & 0x0F);
                case 0x55: return "LD [I], V" + std::to_string((opcode >> 8) & 0x0F);
                case 0x65: return "LD V" + std::to_string((opcode >> 8) & 0x0F) + ", [I]";
                default: return "FX??";
            }

        default: return "????";
    }
}

//debugging windows for better CHIP-8 emulator debugging
void create_debug_windows(Chip8& chip8) {
    // Registers Window - enhanced with execution tracking
    if (chip8.debug_windows.show_registers) {
        if (ImGui::Begin("Registers & Current Instruction", &chip8.debug_windows.show_registers)) {
            // Execution tracking
            ImGui::Text("Instructions Executed: %u", chip8.instructions_executed);
            ImGui::Text("Last Executed Opcode: 0x%04X", chip8.last_opcode);

            ImGui::Separator();

            // Current instruction being executed
            ImGui::Text("Next Opcode: 0x%04X", chip8.inst.opcode);

            //Show assembly instruction
            std::string assembly = decode_instruction(chip8.inst.opcode);
            ImGui::Text("Assembly: %s", assembly.c_str());

            ImGui::Text("Instruction Parts:");
            ImGui::Indent();
            ImGui::Text("NNN (12-bit addr): 0x%03X (%d)", chip8.inst.NNN, chip8.inst.NNN);
            ImGui::Text("NN (8-bit const): 0x%02X (%d)", chip8.inst.NN, chip8.inst.NN);
            ImGui::Text("N (4-bit const): 0x%01X (%d)", chip8.inst.N, chip8.inst.N);
            ImGui::Text("X (register): V%X", chip8.inst.X);
            ImGui::Text("Y (register): V%X", chip8.inst.Y);
            ImGui::Unindent();

            ImGui::Separator();

            ImGui::Text("Program Counter: 0x%04X", chip8.pc);
            ImGui::Text("Index Register (I): 0x%04X", chip8.I);
            ImGui::Text("Stack Pointer: %d", (int)(chip8.sp - &chip8.stack[0]));
            ImGui::Text("Delay Timer: %d", chip8.delay_timer);
            ImGui::Text("Sound Timer: %d", chip8.sound_timer);

            ImGui::Separator();
            ImGui::Text("General Purpose Registers:");

            // Display registers in a 4x4 grid
            for (int row = 0; row < 4; row++) {
                for (int col = 0; col < 4; col++) {
                    int i = row * 4 + col;
                    ImGui::Text("V%X: 0x%02X (%3d)", i, chip8.V[i], chip8.V[i]);
                    if (col < 3) ImGui::SameLine();
                }
            }

            // Sprite drawing info
            ImGui::Separator();
            ImGui::Text("Last Sprite Info:");
            if (chip8.sprite_drawn_this_frame) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Sprite drawn this frame!");
                ImGui::Text("Position: (%d, %d)", chip8.last_sprite_x, chip8.last_sprite_y);
                ImGui::Text("Height: %d", chip8.last_sprite_height);
                ImGui::Text("Memory Address: 0x%03X", chip8.last_sprite_address);
                ImGui::Text("Collision: %s", chip8.collision_detected ? "YES" : "NO");
            }
            else {
                ImGui::Text("No sprite drawn this frame");
            }
        }
        ImGui::End();
    }

    // Enhanced Memory Viewer
    if (chip8.debug_windows.show_memory) {
        if (ImGui::Begin("Memory Viewer", &chip8.debug_windows.show_memory)) {
            static int goto_address = 0x200;
            ImGui::Text("Navigate to address:");
            ImGui::InputInt("Address (hex)", &goto_address, 1, 16, ImGuiInputTextFlags_CharsHexadecimal);
            if (goto_address < 0) goto_address = 0;
            if (goto_address > 4095) goto_address = 4095;
            goto_address &= 0xFFF0;

            ImGui::SameLine();
            if (ImGui::Button("-")) {
                goto_address -= 16;
                if (goto_address < 0) goto_address = 0;
                goto_address &= 0xFFF0;
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                // Auto-repeat while holding
                static float repeat_timer = 0.0f;
                repeat_timer += ImGui::GetIO().DeltaTime;
                if (repeat_timer > 0.1f) { // Repeat every 100ms
                    goto_address -= 16;
                    if (goto_address < 0) goto_address = 0;
                    goto_address &= 0xFFF0;
                    repeat_timer = 0.0f;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("+")) {
                goto_address += 16;
                if (goto_address > 4095) goto_address = 4095;
                goto_address &= 0xFFF0;
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                // Auto-repeat while holding
                static float repeat_timer2 = 0.0f;
                repeat_timer2 += ImGui::GetIO().DeltaTime;
                if (repeat_timer2 > 0.1f) { // Repeat every 100ms
                    goto_address += 16;
                    if (goto_address > 4095) goto_address = 4095;
                    goto_address &= 0xFFF0;
                    repeat_timer2 = 0.0f;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Go to PC")) {
                goto_address = chip8.pc & 0xFFF0;
            }
            ImGui::SameLine();
            if (ImGui::Button("Go to I")) {
                goto_address = chip8.I & 0xFFF0;
            }
            ImGui::SameLine();
            if (ImGui::Button("Font Area")) {
                goto_address = 0x00; // Your font starts at 0x00
            }
            ImGui::Separator();
            // Quick memory region buttons
            if (ImGui::Button("Font (0x00-0x4F)")) goto_address = 0x00;
            ImGui::SameLine();
            if (ImGui::Button("ROM Start (0x200)")) goto_address = 0x200;
            ImGui::Text("Memory around 0x%04X:", goto_address);
            // Display memory with better highlighting
            for (int i = 0; i < 16; i++) {
                int addr = goto_address + (i * 16);
                if (addr >= 4096) break;
                ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // White default
                // Highlight different regions
                if (addr <= chip8.pc && chip8.pc < addr + 16) {
                    color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow for PC
                }
                else if (addr <= chip8.I && chip8.I < addr + 16) {
                    color = ImVec4(0.0f, 1.0f, 1.0f, 1.0f); // Cyan for I register
                }
                else if (addr < 0x50) {
                    color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f); // Orange for font area
                }
                else if (addr >= 0x200) {
                    color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f); // Light green for ROM area
                }
                ImGui::TextColored(color,
                    "%04X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                    addr,
                    chip8.memory[addr], chip8.memory[addr + 1], chip8.memory[addr + 2], chip8.memory[addr + 3],
                    chip8.memory[addr + 4], chip8.memory[addr + 5], chip8.memory[addr + 6], chip8.memory[addr + 7],
                    chip8.memory[addr + 8], chip8.memory[addr + 9], chip8.memory[addr + 10], chip8.memory[addr + 11],
                    chip8.memory[addr + 12], chip8.memory[addr + 13], chip8.memory[addr + 14], chip8.memory[addr + 15]);
            }
            ImGui::Separator();
            ImGui::Text("Colors: Yellow=PC, Cyan=I, Orange=Font, Green=ROM");
        }
        ImGui::End();
    }

    // Enhanced Display Buffer Window
    if (chip8.debug_windows.show_display) {
        if (ImGui::Begin("Display Buffer Analysis", &chip8.debug_windows.show_display)) {
            // Count active pixels
            int active_pixels = 0;
            for (int i = 0; i < 64 * 32; i++) {
                if (chip8.display[i]) active_pixels++;
            }

            ImGui::Text("Active Pixels: %d / %d", active_pixels, 64 * 32);

            if (active_pixels > 0) {
                // Find bounding box of active pixels
                int min_x = 64, max_x = 0, min_y = 32, max_y = 0;
                for (int y = 0; y < 32; y++) {
                    for (int x = 0; x < 64; x++) {
                        if (chip8.display[y * 64 + x]) {
                            min_x = (x < min_x) ? x : min_x;
                            max_x = (x > max_x) ? x : max_x;
                            min_y = (y < min_y) ? y : min_y;
                            max_y = (y > max_y) ? y : max_y;
                        }
                    }
                }
                ImGui::Text("Bounding box: (%d,%d) to (%d,%d)", min_x, min_y, max_x, max_y);
                ImGui::Text("Size: %dx%d", max_x - min_x + 1, max_y - min_y + 1);
            }

            ImGui::Separator();
            ImGui::Text("64x32 Display Buffer (# = pixel on, . = pixel off):");

            // Use monospace font for better alignment
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Default font should be monospace-ish

            for (int y = 0; y < 32; y++) {
                string row = "";
                for (int x = 0; x < 64; x++) {
                    row += chip8.display[y * 64 + x] ? "#" : ".";
                }

                // Highlight rows with active pixels
                if (row.find('#') != string::npos) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%2d: %s", y, row.c_str());
                }
                else {
                    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "%2d: %s", y, row.c_str());
                }
            }

            ImGui::PopFont();
        }
        ImGui::End();
    }

    // Stack Window (unchanged, but keeping for completeness)
    if (chip8.debug_windows.show_stack) {
        if (ImGui::Begin("Stack", &chip8.debug_windows.show_stack)) {
            int current_depth = chip8.sp - &chip8.stack[0];
            ImGui::Text("Stack Depth: %d/12", current_depth);
            ImGui::Text("Stack contents (newest at top):");
            ImGui::Separator();

            for (int i = 11; i >= 0; i--) {
                if (i < current_depth) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                        "Stack[%2d]: 0x%04X %s", i, chip8.stack[i],
                        (i == current_depth - 1) ? "<-- Current SP" : "");
                }
                else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                        "Stack[%2d]: 0x%04X (empty)", i, chip8.stack[i]);
                }
            }
        }
        ImGui::End();
    }

    // Keypad State Window (unchanged)
    if (chip8.debug_windows.show_keypad) {
        if (ImGui::Begin("Keypad State", &chip8.debug_windows.show_keypad)) {
            ImGui::Text("CHIP-8 Keypad Layout:");
            ImGui::Text("1 2 3 C");
            ImGui::Text("4 5 6 D");
            ImGui::Text("7 8 9 E");
            ImGui::Text("A 0 B F");
            ImGui::Separator();

            ImGui::Text("Current key states:");

            const int keypad_layout[4][4] = {
                {0x1, 0x2, 0x3, 0xC},
                {0x4, 0x5, 0x6, 0xD},
                {0x7, 0x8, 0x9, 0xE},
                {0xA, 0x0, 0xB, 0xF}
            };

            for (int row = 0; row < 4; row++) {
                for (int col = 0; col < 4; col++) {
                    int key = keypad_layout[row][col];
                    if (chip8.keypad[key]) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%X", key);
                    }
                    else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%X", key);
                    }
                    if (col < 3) ImGui::SameLine();
                }
            }
        }
        ImGui::End();
    }
}

void emulate_instructions(Chip8 *chip8, Config config) {
    chip8->sprite_drawn_this_frame = false; // Reset flag
    chip8->last_opcode = chip8->inst.opcode; // Store last opcode

	chip8->inst.opcode = (chip8->memory[chip8->pc] << 8) | chip8->memory[chip8->pc + 1]; //big endian
	chip8->pc += 2; //pre-increment program counter for next opcode

    //Fill out current instruction format
    //DXYN
	chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x0FF;
    chip8->inst.N = chip8->inst.opcode & 0x0F;
	chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
	chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

    chip8->instructions_executed++; // Track execution count

    //emulate opcode
    switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
            if (chip8->inst.opcode == 0x00E0) {
                //00E0: Clear the screen
                memset(chip8->display, 0, sizeof(chip8->display));
            }
            else if (chip8->inst.opcode == 0x00EE) {
                //00EE: Return from subroutine
				//Set program counter to last address on subroutine stack ("pop it off the stack")
				chip8->pc = *--chip8->sp;
            }
            else {
                //0xNNN invalid opcode, may be 0xNNN for calling machine code routine for RCA1802
            }

            break;

        case 0x01:
            //0x1NNN: Jump to address NNN
            chip8->pc = chip8->inst.NNN;
			break;

        case 0x02:
            //0x2NNN: Call subroutine at NNN
            //Store current address to return to on subroutine stack and set program counter to subroutine address so that the next opcode is retrieved from there
			*chip8->sp++ = chip8->pc;
			chip8->pc = chip8->inst.NNN;
			break;

        case 0x03:
            //0x3XNN: Skip next instruction if VX == NN
            if (chip8->V[chip8->inst.X] == chip8->inst.NN) {
                chip8->pc += 2;
			}
            break;

        case 0x04:
            //0x4XNN: Skip next instruction if VX != NN
            if (chip8->V[chip8->inst.X] != chip8->inst.NN) {
                chip8->pc += 2;
            }
			break;

        case 0x05:
            //0x5XY0: Skip next instruction if VX == VY
            if (chip8->inst.N != 0) break; //invalid opcode
            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]) {
                chip8->pc += 2;
			}
            break;

        case 0x06:
            //0x6XNN: Set register VX to NN
            chip8->V[chip8->inst.X] = chip8->inst.NN;
			break;

        case 0x07:
            //0x6XNN: Set register VX += NN
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;

        case 0x08: {
            //0x8XYN: Arithmetic and logic operations between VX and VY
            uint8_t* Vx = &chip8->V[chip8->inst.X];
            uint8_t Vy = chip8->V[chip8->inst.Y];

            switch (chip8->inst.N) {
                case 0: //VX = VY
                    *Vx = Vy;
                    break;

                case 1: //VX = VX OR VY
                    *Vx |= Vy;
                    break;

                case 2: //VX = VX AND VY
                    *Vx &= Vy;
                    break;

                case 3: //VX = VX XOR VY
                    *Vx ^= Vy;
                    break;

                case 4: { //VX = VX + VY, set VF = carry
                    uint16_t sum = *Vx + Vy;
                    chip8->V[0xF] = (sum > 0xFF) ? 1 : 0; //set carry flag
                    *Vx = sum & 0xFF; //keep only lower 8 bits

                    break;
                }

                case 5: //VX = VX - VY, set VF = NOT borrow
                    chip8->V[0xF] = (*Vx >= Vy) ? 1 : 0; //set NOT borrow flag
                    *Vx -= Vy;

                    break;

                case 6: //VX = VX SHR 1, set VF to least significant bit before shift
                    chip8->V[0xF] = (*Vx & 0x1); //store least significant bit in VF
                    *Vx >>= 1;

                    break;

                case 7: //VX = VY - VX, set VF = NOT borrow
                    chip8->V[0xF] = (Vy >= *Vx) ? 1 : 0; //set NOT borrow flag
                    *Vx = Vy - *Vx;

                    break;

                case 0xE: //VX = VX SHL 1, set VF to most significant bit before shift
                    chip8->V[0xF] = (*Vx >> 7) & 0x1; //store
					*Vx <<= 1;
                    break;

                default:
                    //invalid opcode
                    break;
            }

            break;
		}

        case 0x09:
            //0x9XY0: Skip next instruction if VX != VY
            if (chip8->inst.N != 0) break; //invalid opcode
            if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y]) {
                chip8->pc += 2;
            }
			break;

        case 0x0A:
            //0xANNN: Set index register I to NNN
            chip8->I = chip8->inst.NNN;
            break;

        case 0x0B:
            //0xBNNN: Jump to address NNN + V0
            chip8->pc = chip8->inst.NNN + chip8->V[0];
			break;

        case 0x0C:
            //0xCXNN: Set VX = random byte (bitwise AND) NN
			chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;

        case 0x0D: {
            //0xDXYN: Draw N-height sprite at coords (VX, VY) from memory location I
            //Screen pixels are XOR'd with sprite bits
            //VF (carry flag) is set if any screen pixels are set off (useful for collision detection)
            uint8_t x = chip8->V[chip8->inst.X] % config.window_width; //wrap around if going off screen
            uint8_t y = chip8->V[chip8->inst.Y] % config.window_height; //wrap around if going off screen

            chip8->sprite_drawn_this_frame = true;
            chip8->last_sprite_x = x;
            chip8->last_sprite_y = y;
            chip8->last_sprite_height = chip8->inst.N;
            chip8->last_sprite_address = chip8->I;

            const uint8_t original_x = x;

            chip8->V[0xF] = 0; //reset collision flag

            for (int row = 0; row < chip8->inst.N; row++) {
                //get next byte/row of sprite data from memory
                const uint8_t sprite_data = chip8->memory[chip8->I + row];
                x = original_x; //reset x for next row to draw

                for (int col = 7; col >= 0; col--) {
                    //If sprite pixel/bit is on and display pixel is on, set carry flag
                    bool* pixel = &chip8->display[y * config.window_width + x];
                    const bool sprite_pixel = (sprite_data & (1 << col));

                    if (sprite_pixel && *pixel) {
                        chip8->V[0xF] = 1; //collision detected
                        chip8->collision_detected = true;
                    }

                    //XOR display pixel with sprite pixel to set it on or off
                    *pixel ^= sprite_pixel;

                    //Stop drawing this row if hit right edge of screen
                    if (++x >= config.window_width) break;
                }
                //Stop drawing entire sprite if hit bottom edge of screen
                if (++y >= config.window_height) break;
            }

            break;
        }

        case 0x0E:
            if (chip8->inst.NN == 0x9E) {
                //0xEX9E: Skip next instruction if key in VX is pressed
                if (chip8->keypad[chip8->V[chip8->inst.X]]) {
                    chip8->pc += 2;
                }
            }
            else if (chip8->inst.NN == 0xA1) {
                //0xEXA1: Skip next instruction if key in VX is not pressed
                if (!chip8->keypad[chip8->V[chip8->inst.X]]) {
                    chip8->pc += 2;
                }
			}
            break;

        case 0x0F: {
            switch (chip8->inst.NN) {
                case 0x0A: { //0xFX0A: Wait for a key press, store in VX
                    bool key_pressed = false;
                    for (int i = 0; i < 16; i++) {
                        if (chip8->keypad[i]) {
                            chip8->V[chip8->inst.X] = i;
                            key_pressed = true;
                            break;
                        }
                    }
                    //If no key pressed, repeat this instruction by decrementing program counter by 2
                    if (!key_pressed) {
                        chip8->pc -= 2;
                    }
                    break;
                }

                case 0x1E: //0xFX1E: Set I += VX
                    chip8->I += chip8->V[chip8->inst.X];
                    break;

                case 0x07: //0xFX07: Set VX = delay timer value
                    chip8->V[chip8->inst.X] = chip8->delay_timer;
                    break;

                case 0x15: //0xFX15: Set delay timer = VX
                    chip8->delay_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x18: //0xFX18: Set sound timer = VX
                    chip8->sound_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x29: //0xFX29: Set I to location of sprite for digit VX (font characters 0-F)
                    chip8->I = chip8->V[chip8->inst.X] * 5; //each font char is 5 bytes long starting at memory location 0
                    break;

                case 0x33: { //0xFX33: Store BCD representation of VX in memory locations I, I+1, I+2
                    uint8_t bcd = chip8->V[chip8->inst.X];
                    chip8->memory[chip8->I + 2] = bcd % 10;
					bcd /= 10;
                    chip8->memory[chip8->I + 1] = bcd % 10;
					bcd /= 10;
                    chip8->memory[chip8->I] = bcd;
                    break;
                }

                case 0x55: //0xFX55: Store registers V0 through VX in memory starting at location I
                    for (int i = 0; i <= chip8->inst.X; i++) {
                        chip8->memory[chip8->I + i] = chip8->V[i];
                    }
                    //Original CHIP-8 behavior: I is incremented by X + 1 after operation
                    //chip8->I += chip8->inst.X + 1;
					break;

                case 0x65: //0xFX65: Read registers V0 through VX from memory starting at location I
                    for (int i = 0; i <= chip8->inst.X; i++) {
                        chip8->V[i] = chip8->memory[chip8->I + i];
                    }
                    //Original CHIP-8 behavior: I is incremented by X + 1 after operation
					//chip8->I += chip8->inst.X + 1;
                    break;

                default:
                    //invalid opcode
                    break;
            }

            break;
        }

        default:
            //SDL_Log("Unknown opcode: 0x%04X", chip8->inst.opcode);
		    break;
    }
}

void create_control_panel(Chip8& chip8, const Config& config) {
    //Control Panel with step debugging capabilities
    if (ImGui::Begin("Control Panel")) {
        ImGui::Text("ROM: %s", chip8.rom.empty() ? "No ROM loaded" : chip8.rom.c_str());
        ImGui::Text("State: %s",
            (chip8.state == EmulatorState::RUNNING ? "Running" :
                (chip8.state == EmulatorState::PAUSE ? "Paused" : "Quit")));

        //Basic control buttons
        if (ImGui::Button("Pause/Resume")) {
            if (!chip8.debug_mode) {
                if (!chip8.rom.empty()) {
                    chip8.state = (chip8.state == EmulatorState::RUNNING) ? EmulatorState::PAUSE : EmulatorState::RUNNING;
                }
            } else {
				SDL_Log("Cannot resume from pause while in debug mode. Disable debug mode first.");
            }
        }
        ImGui::SameLine();

        //Step button only works when paused
        if (chip8.state == EmulatorState::PAUSE) {
            if (ImGui::Button("Step One Instruction")) {
                if (!chip8.rom.empty()) {
					// Execute exactly one instruction while paused and if a ROM is loaded
                    emulate_instructions(&chip8, config);
                }
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Reset")) {
            //Reset the CHIP-8 system to initial state
            init_chip8(chip8);
			
            //Reload the current ROM if one was loaded
            if (!chip8.rom.empty()) {
                loadROM(chip8, chip8.rom);
            }
        }
        ImGui::SameLine();

        if (ImGui::Button("Quit")) {
            chip8.state = EmulatorState::QUIT;
        }

        ImGui::Separator();

        ImGui::Checkbox("Debug Mode", &chip8.debug_mode);
        ImGui::SameLine();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Auto-pause after each instruction for step debugging");
        }

        //helpful statistics for debugging
        ImGui::Separator();
        ImGui::Text("FPS: %d", 60);
        ImGui::Text("Current memory usage: %d bytes", 4096);

        //Quick access to toggle all debug windows
        ImGui::Separator();
        if (ImGui::Button("Show All Debug Windows")) {
            chip8.debug_windows.show_registers = true;
            chip8.debug_windows.show_memory = true;
            chip8.debug_windows.show_stack = true;
            chip8.debug_windows.show_display = true;
            chip8.debug_windows.show_keypad = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Hide All Debug Windows")) {
            chip8.debug_windows.show_registers = false;
            chip8.debug_windows.show_memory = false;
            chip8.debug_windows.show_stack = false;
            chip8.debug_windows.show_display = false;
            chip8.debug_windows.show_keypad = false;
        }
    }
    ImGui::End();
}

void create_imgui_interface(Chip8& chip8, Sdl& sdl, const Config& config) {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    //main menu bar at the top of the application window
    create_main_menu_bar(chip8);

    //CHIP-8 display viewport
    create_viewport(chip8, config);

    //control panel window
    create_control_panel(chip8, config);

    //debug windows
    create_debug_windows(chip8);

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sdl.renderer);
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

    //Seed random number generator
	srand(time(NULL));

    while (chip8.state != EmulatorState::QUIT) {
        handle_input(chip8, sdl);

        clear_screen(sdl, config);

        //Get_time before running instructions
		const uint64_t start_frame_time = SDL_GetPerformanceCounter();
        
        if (chip8.state == EmulatorState::RUNNING) {
            //decrement timers at 60Hz
            if (chip8.delay_timer > 0) {
                chip8.delay_timer--;
            }
            if (chip8.sound_timer > 0) {
                chip8.sound_timer--;
                //TODO: sound
            }

            if (chip8.debug_mode) {
                //If in debug mode, pause for step debugging
                chip8.state = EmulatorState::PAUSE;
            } else {
                //Emulate CHIP8 instructions for this emulator "frame" (60Hz)
                for (uint32_t i = 0; i < config.clock_rate / 60; i++) {
                    emulate_instructions(&chip8, config);
				}
            }
        }

        //Get time elapsed after running instructions
		const uint64_t end_frame_time = SDL_GetPerformanceCounter();

        //Render ImGui interface
        create_imgui_interface(chip8, sdl, config);

        //Delay for approximately 60hz/60fps (16.67ms) or actual time elapsed
		const double time_elapsed = (double)((end_frame_time - start_frame_time) / 1000) / SDL_GetPerformanceFrequency();
        SDL_Delay(16.67f > time_elapsed ? 16.67f - time_elapsed : 0); // 60fps

        //update window with changes
        update_screen(sdl, chip8);
    }

    final_cleanup(sdl);
    return EXIT_SUCCESS;
}