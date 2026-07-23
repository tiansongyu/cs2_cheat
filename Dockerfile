# syntax=docker/dockerfile:1

FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        binutils-mingw-w64-x86-64 \
        g++-mingw-w64-x86-64-posix \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# MSVC/Windows resolves headers case-insensitively; Linux-hosted MinGW does not.
RUN ln -s windows.h /usr/x86_64-w64-mingw32/include/Windows.h \
    && ln -s tlhelp32.h /usr/x86_64-w64-mingw32/include/TlHelp32.h

WORKDIR /workspace
COPY external-cheat-base/ external-cheat-base/

RUN mkdir -p /artifacts \
    && x86_64-w64-mingw32-g++-posix \
        -std=c++20 \
        -O2 \
        -DNDEBUG \
        -DUNICODE \
        -D_UNICODE \
        -DSDL_MAIN_HANDLED \
        -Wno-unknown-pragmas \
        -Iexternal-cheat-base/vendor/SDL2/include \
        -Iexternal-cheat-base/vendor/imgui \
        -Iexternal-cheat-base/src \
        -Iexternal-cheat-base/src/core \
        -Iexternal-cheat-base/src/features \
        -Iexternal-cheat-base/src/utils \
        -Iexternal-cheat-base/generated \
        external-cheat-base/src/features/esp.cpp \
        external-cheat-base/src/features/aimbot.cpp \
        external-cheat-base/src/main.cpp \
        external-cheat-base/src/core/memory/memory.cpp \
        external-cheat-base/src/core/renderer/sdl_renderer.cpp \
        external-cheat-base/vendor/imgui/imgui.cpp \
        external-cheat-base/vendor/imgui/imgui_demo.cpp \
        external-cheat-base/vendor/imgui/imgui_draw.cpp \
        external-cheat-base/vendor/imgui/imgui_tables.cpp \
        external-cheat-base/vendor/imgui/imgui_widgets.cpp \
        external-cheat-base/vendor/imgui/imgui_impl_sdl2.cpp \
        external-cheat-base/vendor/imgui/imgui_impl_sdlrenderer2.cpp \
        external-cheat-base/vendor/SDL2/lib/x64/SDL2.lib \
        -ldwmapi \
        -lgdi32 \
        -static \
        -static-libgcc \
        -static-libstdc++ \
        -o /artifacts/external-cheat-base.exe \
    && cp external-cheat-base/vendor/SDL2/lib/x64/SDL2.dll /artifacts/SDL2.dll

FROM scratch AS artifacts
COPY --from=builder /artifacts/ /
