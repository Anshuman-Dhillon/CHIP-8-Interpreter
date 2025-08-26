# CHIP-8 Emulator

A functional CHIP-8 interpreter with comprehensive debugging tools, built from scratch in C++ using SDL3 and Dear ImGui.

![CHIP-8 DEMO](demo/CHIP-8_demo.gif)

## What is CHIP-8?

CHIP-8 is a simple virtual machine and programming language that was originally developed in the 1970s for early personal computers. Unlike modern CPUs, CHIP-8 is an interpreted language. It's not actual hardware, but rather a virtual machine that runs on top of real hardware. This makes it an excellent starting point for learning about emulation and computer architecture.

The CHIP-8 system was designed to make it easy to port simple games across different computer platforms. Classic games like Pong, Tetris, Space Invaders, and Pac-Man were all implemented for CHIP-8. These games still run today on CHIP-8 interpreters, making them a perfect test case for emulation projects.

## Why CHIP-8?

I chose to implement a CHIP-8 interpreter because it strikes the perfect balance between simplicity and completeness. The system is small enough to understand fully (since it has only 35 instructions, 4KB of memory, and a 64x32 pixel display) but complex enough to demonstrate real emulation concepts like instruction decoding, memory management, and hardware timing.

My implementation runs CHIP-8 ROMs accurately at configurable speeds (typically 700 instructions per second), handles all original CHIP-8 instructions, and includes extensive debugging capabilities that go far beyond what most CHIP-8 interpreters offer.

## Key Features

**Core Emulation**
- Complete implementation of all 35 CHIP-8 instructions
- Accurate timing with configurable clock rates
- 4KB memory system with proper memory mapping
- 64x32 pixel monochrome display with XOR-based sprite drawing
- 16-key hexadecimal keypad input
- Sound system with configurable square wave generation

**Debugging Infrastructure**
- Real-time register and memory viewer
- Step-by-step instruction execution
- Assembly instruction decoder showing human-readable mnemonics
- Stack trace visualization
- Display buffer analysis with pixel counting and bounding box detection
- Keypad state monitoring
- Comprehensive execution statistics

**User Interface**
- Modern GUI built with Dear ImGui
- Resizable display viewport with automatic scaling
- File browser for loading ROMs
- Audio controls with volume and frequency adjustment
- Multiple debug windows that can be toggled independently

## How It Works

### CPU Instruction Processing

The core of any emulator is instruction processing. CHIP-8 instructions are 16-bit values that encode different operations. My emulator fetches instructions from memory, decodes them into their component parts (opcode, registers, addresses, constants), and executes the corresponding operation.

```cpp
switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
        //rest of the opcodes and their implementation...
}
```

The switch-case structure in `emulate_instructions()` handles all 35 CHIP-8 instructions, from simple register operations to complex sprite drawing.

<details>
<summary><strong>Click to expand for more details about CPU instructions</strong></summary>
<br>
Like I mentioned before, CHIP-8 instructions are 16-bit values that encode different operations using a carefully designed bit layout. In this section, I'll explain how these bits are extracted and interpreted.

Every CHIP-8 instruction follows this bit pattern:
```
15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
|  Opcode  |  X  |  Y  |        N/NN/NNN        |
```

When my emulator fetches an instruction, it extracts these components:
```cpp
chip8->inst.opcode = (chip8->memory[chip8->pc] << 8) | chip8->memory[chip8->pc + 1];
chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;        // 12-bit address (bits 0-11)
chip8->inst.NN = chip8->inst.opcode & 0x0FF;          // 8-bit constant (bits 0-7)
chip8->inst.N = chip8->inst.opcode & 0x0F;            // 4-bit constant (bits 0-3)
chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;     // 4-bit register ID (bits 8-11)
chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;     // 4-bit register ID (bits 4-7)
```

The Role of Each Component:

- NNN: 12-bit memory address used in jump and call instructions (0x000 to 0xFFF)
- NN: 8-bit immediate value for loading constants into registers (0x00 to 0xFF)
- N: 4-bit value used for sprite height and shift operations (0x0 to 0xF)
- X: Source/destination register identifier (V0 to VF)
- Y: Second register identifier for two-register operations

The bit shifting operations extract specific fields from the 16-bit instruction:

- `>> 8` shifts right by 8 bits, moving bits 8-15 to positions 0-7
- `& 0x0F` masks to keep only the lowest 4 bits (binary 1111)
- `& 0x0FFF` masks to keep the lowest 12 bits (binary 111111111111)

Fetch-Decode-Execute Cycle:

- Fetch: Read 16-bit instruction from memory at PC address (big-endian format)
- Decode: Extract opcode and operands using bit masks and shifts
- Execute: Use switch-case structure to jump to appropriate instruction handler
- Update: Increment PC and update any affected registers or memory

The switch-case structure uses the upper 4 bits (opcode) to determine instruction type, with some instructions requiring additional decoding of the lower bits for variants.

</details>

### Memory Architecture

CHIP-8 systems have a straightforward memory layout that I've implemented:

- **0x000-0x1FF**: Reserved area (where I store the font data)
- **0x200-0xFFF**: Program ROM and RAM space
- The system includes 16 general-purpose 8-bit registers (V0-VF), where VF serves double duty as a flags register
- A 16-bit index register (I) for memory addressing
- A program counter (PC) that tracks the current instruction
- A stack for subroutine calls with up to 12 levels of nesting

### Graphics System

The display system was one of the more interesting challenges. CHIP-8 has a 64x32 pixel monochrome display where sprites are drawn using XOR operations. This means drawing the same sprite twice in the same location will erase it - a feature that many classic games rely on for animation.

My implementation uses SDL3 for the underlying graphics, but I render the CHIP-8 display pixel-by-pixel in the ImGui viewport. Each CHIP-8 pixel becomes a rectangle that scales with the window size, maintaining the original 2:1 aspect ratio.

<details>
<summary><strong>Click to expand for more detail about the graphics/display system</strong></summary>

The CHIP-8 graphics system is simple but elegant. The 64x32 pixel monochrome display uses XOR-based drawing, which creates unique visual effects that many classic games depend on.

###XOR Drawing Algorithm:###

When a sprite is drawn, each pixel is XORed with the corresponding display buffer pixel:
```cpp
*pixel ^= sprite_pixel;
```
  
How XOR Works in Practice:
```
Display Pixel | Sprite Pixel | Result | Visual Effect
      0       |      0       |   0    | Background stays off
      0       |      1       |   1    | Pixel turns on
      1       |      0       |   1    | Pixel stays on
      1       |      1       |   0    | Pixel turns off (collision)
```

This creates several important behaviors:

- Drawing the same sprite twice in the same location erases it completely
- Overlapping sprites create "holes" where they intersect
- Games can create flashing effects by rapidly redrawing sprites
- Collision detection happens automatically when pixels turn off

###Sprite Drawing Process:####

```cpp
for (int row = 0; row < chip8->inst.N; row++) {
    const uint8_t sprite_data = chip8->memory[chip8->I + row];
    for (int col = 7; col >= 0; col--) {
        bool* pixel = &chip8->display[y * config.window_width + x];
        const bool sprite_pixel = (sprite_data & (1 << col));
        
        if (sprite_pixel && *pixel) {
            chip8->V[0xF] = 1; // Collision detected
        }
        *pixel ^= sprite_pixel;
    }
}
```

###Memory to Display Mapping:###

- Each sprite row is stored as a single byte in memory
- Bit 7 (leftmost) represents the first pixel
- Bit 0 (rightmost) represents the eighth pixel
- Sprites wrap around screen edges automatically

The VF register is set to 1 when any "on" pixel in the sprite overlaps with an already "on" pixel in the display buffer. This happens before the XOR operation, so games can detect when sprites would collide and react accordingly.

</details>

### Audio Implementation

The audio system generates a square wave beep when the sound timer is active. I use SDL3's audio streaming API to generate the waveform in real-time:

```cpp
void generate_square_wave(float* buffer, int sample_count, uint32_t sample_rate, 
                         uint32_t frequency, uint16_t volume, uint32_t* phase)
```

Users can adjust both the frequency (100-2000 Hz) and volume of the beep sound, or mute it entirely.

<details>
<summary><strong>Click to expand for more detail about the sound system</strong></summary>
The CHIP-8 audio system generates a simple square wave tone when the sound timer is active. Understanding the mathematics behind square wave generation reveals how digital audio synthesis works at a fundamental level.
Square Wave Mathematics:
A square wave alternates between two amplitude levels at regular intervals. For a frequency f, the wave period T = 1/f. In digital audio, we generate samples at a fixed sample rate (44,100 Hz for CD quality).

```cpp
cppconst float samples_per_cycle = (float)sample_rate / frequency;
```

Sample Generation Algorithm:

```cpp
for (int i = 0; i < sample_count; i++) {
    float sample = ((*phase / (uint32_t)(samples_per_cycle / 2)) % 2) ? amplitude : -amplitude;
    buffer[i] = sample;

    (*phase)++;

    if (*phase >= (uint32_t)samples_per_cycle) {
        *phase = 0;
    }
}
```
How This Works:

samples_per_cycle: How many audio samples make up one complete wave cycle
samples_per_cycle / 2: Half-cycle duration (when the wave should flip)
*phase: Current position within the wave cycle
The modulo operation creates the alternating pattern

Volume Control:
Volume in digital audio is controlled by amplitude scaling:
cppconst float amplitude = volume / 32767.0f; // Normalize 16-bit range to [-1.0, 1.0]
The division by 32,767 (maximum 16-bit signed integer value) converts the integer volume setting into a floating-point amplitude between -1.0 and 1.0, which is the standard range for digital audio samples.
Phase Tracking:
The phase variable tracks our position within the current wave cycle. This ensures smooth audio even when the sound starts and stops, preventing audio pops and clicks that would occur if we always started from phase 0.
SDL3 Audio Pipeline:

Generate square wave samples in floating-point format
Fill SDL audio buffer with sample data
SDL handles conversion to hardware audio format
Audio device plays samples at precisely timed intervals

This approach provides clean, configurable audio with minimal CPU overhead.
</details>

### Timing and Synchronization

CHIP-8 systems traditionally run at 60Hz for the display refresh and timer updates, while the CPU can run at various speeds. My emulator separates these concerns:

- Display updates and timer decrements happen at 60Hz
- CPU instruction execution happens at a configurable rate (default 700 instructions per second)
- The main loop uses SDL's timing functions to maintain consistent frame rates

### Debugging Infrastructure

The debugging system I built goes well beyond what's typical for CHIP-8 emulators. The register window shows execution statistics, instruction decode information, and sprite drawing details. The memory viewer provides navigation tools, syntax highlighting for different memory regions, and quick jumps to important addresses.

The step debugger allows single-instruction execution while paused, making it easy to trace through program execution and understand how CHIP-8 games work.

## Technical Implementation Details

### Architecture Decisions

I had initially chosen C for its low-level control, but later switched to C++ due to compatibility issues with dear imgui. The code is structured around several key components:

- `Chip8` struct contains all emulator state
- `Config` struct holds user-configurable settings
- `Sdl` struct manages SDL resources
- Separate functions handle initialization, instruction execution, and UI rendering

### Library Choices

**SDL3**: I selected SDL3 over alternatives like GLFW or raw Win32/X11 because it provides excellent cross-platform support for graphics, audio, and input in a single library. SDL3's new audio streaming API made implementing the square wave generation straightforward.

**Dear ImGui**: This immediate-mode GUI library was perfect for the debugging interface. It integrates cleanly with SDL3, provides professional-looking UI elements, and makes it easy to create interactive debugging tools.

**TinyFileDialogs**: A lightweight, cross-platform file dialog library that integrates seamlessly for ROM loading.

### Code Organization

The emulator follows a clear separation of concerns:

- Initialization and cleanup functions set up SDL, ImGui, and CHIP-8 state
- The main loop handles input, executes instructions, and renders the UI
- Instruction emulation is contained in `emulate_instructions()`
- UI creation is split across multiple functions for different windows
- Helper functions handle tasks like instruction decoding and audio generation

## Project Structure

```
CHIP-8/
└── CHIP-8/
|    ├── .git/
|    ├── .vs/
|    ├── imgui/                         # Dear ImGui headers
|    └── CHIP-8/
|        ├── main.cpp                   # Main emulator implementation (~1,400 lines)
|        ├── Dependencies/
|            ├── include/               # Header files for all libraries
|            │   ├── SDL3/              # SDL3 headers
|            │   └── tinyfiledialogs/   # File dialog headers
|            └── lib/                   # Library binaries
|                └── x64/               # 64-bit libraries
|
└── chip8-roms-master/                  # Test ROM collection
```

The entire emulator is implemented in a single `main.cpp` file (approximately 1,400 lines). While this might seem monolithic, it makes the project easy to understand and compile, and the functions are well-organized by purpose.

## Building the Project

### Prerequisites

- C++17 compatible compiler (Visual Studio 2019+, GCC 9+, or Clang 10+)
- Git for cloning the repository (optional)

### Running the Program

1. Clone the repository or download as a .zip by clicking the green code button
2. Open file explorer and navigate to x64 -> Release -> Run CHIP-8.exe

## Usage

1. Launch the emulator
2. Load a CHIP-8 ROM using File → Load ROM or Ctrl+O. You may also load the test ROM, which is currently set to as the IBM logo ROM.
3. Use the Control Panel to start/pause emulation
4. Access debug windows through the Debug menu
5. Control games using the keyboard mapping:

```
CHIP-8 Keypad    Keyboard
1 2 3 C          1 2 3 4
4 5 6 D          Q W E R  
7 8 9 E          A S D F
A 0 B F          Z X C V
```

Press Space to pause/resume, or Escape to quit.

## What I Learned

Building this emulator taught me about low-level computer architecture, instruction set design, and the challenges of accurate timing in emulated systems. The debugging infrastructure was particularly educational as creating tools that help visualize what the emulated system is doing requires deep understanding of both the target system and user interface design.

The project also reinforced the importance of clean code organization and comprehensive testing. Having a suite of test ROMs made it possible to verify that each instruction worked correctly and that timing was accurate.

## Known Issues
- Display flickers consistently, my guess is it happens because you are clearing the screen each time. Could be fixed through linear interpolation, which I'll add in later as this is not a critical issue, just annoying.

## Future Improvements

While the current implementation is fully functional, there are several areas I'd like to expand:

- Support for SUPER-CHIP extensions (128x64 display, additional instructions)
- Save states for preserving game progress
- Rewind functionality for debugging
- Performance profiling tools
- Network play capabilities for multiplayer games

## References

- [COSMAC VIP Instruction Manual 1978](https://ia903208.us.archive.org/29/items/bitsavers_rcacosmacCManual1978_6956559/COSMAC_VIP_Instruction_Manual_1978.pdf) - Original RCA COSMAC VIP documentation
- [Handmade Penguin CHIP-8 Tutorial](https://davidgow.net/handmadepenguin/ch8.html) - David Gow's CHIP-8 implementation guide
- [SDL3 Releases](https://github.com/libsdl-org/SDL/releases) - SDL3 library downloads and documentation
- [Write a CHIP-8 Emulator](https://tobiasvl.github.io/blog/write-a-chip-8-emulator/) - Tobias V. Langhoff's comprehensive CHIP-8 guide
- [SDL3 Wiki](https://wiki.libsdl.org/SDL3/FrontPage) - Official SDL3 documentation
- [CHIP-8 Wikipedia](https://en.wikipedia.org/wiki/CHIP-8) - Historical background and system overview
- [CHIP-8 ROM Collection](https://github.com/kripod/chip8-roms) - Curated collection of CHIP-8 games and programs
- [CHIP-8 Technical Reference](https://github.com/mattmikolay/chip-8/wiki/CHIP%E2%80%908-Technical-Reference) - Matt Mikolay's detailed technical specification
