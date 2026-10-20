cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# STM32F103 (SPL V3.6.x) bare-metal project
#
# Configure with:  cmake --preset debug      (or `release`)
# Tweak options in:  ccmake build/debug
# Build with:      cmake --build --preset debug
# Flash with:      cmake --build --preset debug --target flash
# ---------------------------------------------------------------------------

project(stm32f103 C ASM)

# Guard against accidental host-gcc configuration (must use the ARM toolchain
# file, which the CMake presets do automatically).
if(NOT CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "Host compiler detected! Configure with: cmake --preset debug")
endif()

# ------------------------------- options ------------------------------------
set(DEVICE "STM32F103C8T6" CACHE STRING "Target chip (sets linker script)")
set_property(CACHE DEVICE PROPERTY STRINGS "STM32F103C8T6" "STM32F103CBT6")

set(OPENOCD_ADAPTER "stlink" CACHE STRING "Debug adapter for flash/debug-server targets")
set_property(CACHE OPENOCD_ADAPTER PROPERTY STRINGS "stlink" "cmsis-dap")

# ---------------------------- device settings ------------------------------
if(DEVICE STREQUAL "STM32F103C8T6")
  set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/ld/STM32F103C8T6_FLASH.ld)
elseif(DEVICE STREQUAL "STM32F103CBT6")
  set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/ld/STM32F103CBT6_FLASH.ld)
else()
  message(FATAL_ERROR "Unknown DEVICE '${DEVICE}' (edit with: ccmake build/debug)")
endif()

# Medium density line (STM32F103C8/CB) — selects peripheral set in stm32f10x.h
add_compile_definitions(STM32F10X_MD USE_STDPERIPH_DRIVER)

# -------------------------------- sources -----------------------------------
# Official GNU-syntax startup file (SPL V3.6.x, gcc_ride7 flavor).
set(STARTUP_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Start/startup_stm32f10x_md.s)

set(START_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/Start/system_stm32f10x.c
  ${CMAKE_CURRENT_SOURCE_DIR}/Start/core_cm3.c
)

# All SPL drivers compile under STM32F10X_MD (stm32f10x.h defines every
# peripheral unconditionally); unused ones are stripped by --gc-sections.
file(GLOB LIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Library/*.c)

# Application code: add new files to User/, Hardware/ or System/ and reconfigure
file(GLOB APP_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/User/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/Hardware/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/System/*.c
)

add_executable(${PROJECT_NAME}.elf
  ${STARTUP_SOURCE}
  ${START_SOURCES}
  ${LIB_SOURCES}
  ${APP_SOURCES}
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Start
  ${CMAKE_CURRENT_SOURCE_DIR}/Library
  ${CMAKE_CURRENT_SOURCE_DIR}/User
  ${CMAKE_CURRENT_SOURCE_DIR}/Hardware
  ${CMAKE_CURRENT_SOURCE_DIR}/System
)

# ----------------------------- compile options ------------------------------
# SPL is 2011-era C89 code: pin gnu11 (not GCC 15+ default C23) and
# relax strict aliasing (SPL casts register addresses to struct pointers).
set(MCU_FLAGS -mcpu=cortex-m3 -mthumb)

# Language-scoped options: C gets gnu11 + warnings, ASM only gets MCU flags.
# (Scope them explicitly — a bare list would also leak C flags into ASM and
# CMAKE_ASM_FLAGS joins list items with ';', breaking the assembler call.)
target_compile_options(${PROJECT_NAME}.elf PRIVATE
  $<$<COMPILE_LANGUAGE:C>:
    ${MCU_FLAGS}
    -std=gnu11
    -Wall
    -fno-strict-aliasing
    -ffunction-sections
    -fdata-sections
    -fno-common
  >
  $<$<COMPILE_LANGUAGE:ASM>:${MCU_FLAGS}>
)

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os")
set(CMAKE_ASM_FLAGS_DEBUG "-g3")

# ------------------------------ link options --------------------------------
target_link_options(${PROJECT_NAME}.elf PRIVATE
  -T${LINKER_SCRIPT}
  ${MCU_FLAGS}
  --specs=nano.specs
  --specs=nosys.specs
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
)

# --------------------------- post-build artifacts ---------------------------
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
  COMMENT "Generating .hex/.bin and printing size"
  VERBATIM
)

# ------------------------------- openocd hooks ------------------------------
set(OPENOCD_ARGS
  -f ${CMAKE_CURRENT_SOURCE_DIR}/openocd/${OPENOCD_ADAPTER}.cfg
  -f ${CMAKE_CURRENT_SOURCE_DIR}/openocd/stm32f103.cfg
)

# Flash the chip: cmake --build --preset debug --target flash
add_custom_target(flash
  COMMAND openocd ${OPENOCD_ARGS}
          -c "program $<TARGET_FILE:${PROJECT_NAME}.elf> verify reset exit"
  DEPENDS ${PROJECT_NAME}.elf
  USES_TERMINAL
)

# Flash without relying on the NRST pin (4-wire SWD boards):
# programs + verifies, then resets via SYSRESETREQ (AIRCR write).
# Requires `reset_config none` in openocd/stm32f103.cfg.
add_custom_target(flash-soft-reset
  COMMAND openocd ${OPENOCD_ARGS}
          -c "program $<TARGET_FILE:${PROJECT_NAME}.elf> verify"
          -c "mww 0xE000ED0C 0x05FA0004"
          -c "shutdown"
  DEPENDS ${PROJECT_NAME}.elf
  USES_TERMINAL
)

# Start a GDB server on :3333: cmake --build --preset debug --target debug-server
add_custom_target(debug-server
  COMMAND openocd ${OPENOCD_ARGS}
  USES_TERMINAL
)
