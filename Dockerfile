# syntax=docker/dockerfile:1

FROM node:22-alpine AS web-radar-builder

WORKDIR /web-radar
COPY web-radar/package.json web-radar/package-lock.json ./
RUN npm ci
COPY web-radar/index.html web-radar/vite.config.ts web-radar/tsconfig*.json ./
COPY web-radar/src/ src/
COPY web-radar/public/ public/
COPY web-radar/scripts/ scripts/
RUN npm test \
    && npm run build \
    && node scripts/validate-bundle.mjs dist

FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        binutils-mingw-w64-x86-64 \
        gcc-mingw-w64-x86-64-posix \
        g++ \
        g++-mingw-w64-x86-64-posix \
        python3 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# MSVC/Windows resolves headers case-insensitively; Linux-hosted MinGW does not.
RUN ln -s windows.h /usr/x86_64-w64-mingw32/include/Windows.h \
    && ln -s tlhelp32.h /usr/x86_64-w64-mingw32/include/TlHelp32.h

WORKDIR /workspace
COPY external-cheat-base/ external-cheat-base/
COPY tests/ tests/
COPY vendor/ vendor/
COPY scripts/generate_fixed_map_catalog.py scripts/generate_fixed_map_catalog.py
COPY --from=web-radar-builder /web-radar/dist/ web-radar/dist/
COPY --from=web-radar-builder /web-radar/public/maps/ web-radar/public/maps/

RUN python3 scripts/generate_fixed_map_catalog.py --check

RUN g++ \
        -std=c++20 \
        -O2 \
        -Wall \
        -Wextra \
        -Werror \
        -Iexternal-cheat-base/src \
        tests/viewport_math_tests.cpp \
        -o /tmp/viewport_math_tests \
    && /tmp/viewport_math_tests

RUN g++ \
        -std=c++20 \
        -O2 \
        -Wall \
        -Wextra \
        -Werror \
        -Iexternal-cheat-base/src \
        tests/web_radar_model_tests.cpp \
        external-cheat-base/src/core/game/web_radar_json.cpp \
        -o /tmp/web_radar_model_tests \
    && /tmp/web_radar_model_tests

RUN g++ \
        -std=c++20 \
        -O2 \
        -Wall \
        -Wextra \
        -Wpedantic \
        -Werror \
        -Iexternal-cheat-base/src \
        external-cheat-base/src/features/web_radar/tests/public_relay_config_tests.cpp \
        -o /tmp/public_relay_config_tests \
    && /tmp/public_relay_config_tests

RUN mkdir -p /tmp/web-radar-smoke \
    && gcc \
        -std=c11 \
        -O2 \
        -DNO_SSL \
        -DNO_CGI \
        -DUSE_WEBSOCKET \
        -DUSE_IPV6 \
        -Ivendor/civetweb/include \
        -c vendor/civetweb/src/civetweb.c \
        -o /tmp/web-radar-smoke/civetweb.o \
    && g++ \
        -std=c++20 \
        -O2 \
        -DNO_SSL \
        -DNO_CGI \
        -DUSE_WEBSOCKET \
        -DUSE_IPV6 \
        -Ivendor/civetweb/include \
        -c vendor/civetweb/src/CivetServer.cpp \
        -o /tmp/web-radar-smoke/CivetServer.o \
    && g++ \
        -std=c++20 \
        -O2 \
        -Wall \
        -Wextra \
        -Werror \
        -DNO_SSL \
        -DNO_CGI \
        -DUSE_WEBSOCKET \
        -DUSE_IPV6 \
        -Ivendor/civetweb/include \
        -Iexternal-cheat-base/src \
        -Iexternal-cheat-base/src/features/web_radar \
        external-cheat-base/src/features/web_radar/tests/web_radar_service_smoke_tests.cpp \
        external-cheat-base/src/features/web_radar/web_radar_service.cpp \
        /tmp/web-radar-smoke/civetweb.o \
        /tmp/web-radar-smoke/CivetServer.o \
        -pthread \
        -ldl \
        -o /tmp/web-radar-smoke/web_radar_service_smoke_tests \
    && /tmp/web-radar-smoke/web_radar_service_smoke_tests

# CivetWeb is third-party C/C++ code. Compile the C implementation explicitly
# as C and keep its warning policy separate from project-owned sources.
RUN mkdir -p /tmp/third-party \
    && x86_64-w64-mingw32-gcc-posix \
        -std=c11 \
        -O2 \
        -DNO_SSL \
        -DNO_CGI \
        -DUSE_WEBSOCKET \
        -DUSE_IPV6 \
        -D_WIN32_WINNT=0x0A00 \
        -Ivendor/civetweb/include \
        -c vendor/civetweb/src/civetweb.c \
        -o /tmp/third-party/civetweb.o \
    && x86_64-w64-mingw32-g++-posix \
        -std=c++20 \
        -O2 \
        -DNO_SSL \
        -DNO_CGI \
        -DUSE_WEBSOCKET \
        -DUSE_IPV6 \
        -D_WIN32_WINNT=0x0A00 \
        -Ivendor/civetweb/include \
        -c vendor/civetweb/src/CivetServer.cpp \
        -o /tmp/third-party/CivetServer.o

# Treat warnings in project-owned Windows sources as errors. Third-party SDL2
# and ImGui sources are linked in the next step without inheriting that policy.
RUN set -eu; \
    mkdir -p /tmp/strict \
    && for source in \
        external-cheat-base/src/features/esp.cpp \
        external-cheat-base/src/features/aimbot.cpp \
        external-cheat-base/src/features/local_radar/local_fixed_radar.cpp \
        external-cheat-base/src/features/web_radar/public_relay_producer.cpp \
        external-cheat-base/src/features/web_radar/web_radar_service.cpp \
        external-cheat-base/src/main.cpp \
        external-cheat-base/src/core/game/web_radar_json.cpp \
        external-cheat-base/src/core/memory/memory.cpp \
        external-cheat-base/src/core/renderer/sdl_renderer.cpp; \
    do \
        x86_64-w64-mingw32-g++-posix \
            -std=c++20 \
            -pthread \
            -O2 \
            -DNDEBUG \
            -DUNICODE \
            -D_UNICODE \
            -DSDL_MAIN_HANDLED \
            -DNO_SSL \
            -DNO_CGI \
            -DUSE_WEBSOCKET \
            -DUSE_IPV6 \
            -D_WIN32_WINNT=0x0A00 \
            -Wall \
            -Wextra \
            -Wpedantic \
            -Werror \
            -Wno-unknown-pragmas \
            -Iexternal-cheat-base/vendor/SDL2/include \
            -Iexternal-cheat-base/vendor/imgui \
            -Ivendor/civetweb/include \
            -Iexternal-cheat-base/src \
            -Iexternal-cheat-base/src/core \
            -Iexternal-cheat-base/src/features \
            -Iexternal-cheat-base/src/utils \
            -Iexternal-cheat-base/generated \
            -c "$source" \
            -o "/tmp/strict/$(basename "$source").o"; \
    done

RUN mkdir -p /artifacts \
    && x86_64-w64-mingw32-g++-posix \
        -std=c++20 \
        -pthread \
        -O2 \
        -DNDEBUG \
        -DUNICODE \
        -D_UNICODE \
        -DSDL_MAIN_HANDLED \
        -DNO_SSL \
        -DNO_CGI \
        -DUSE_WEBSOCKET \
        -DUSE_IPV6 \
        -D_WIN32_WINNT=0x0A00 \
        -Wno-unknown-pragmas \
        -Iexternal-cheat-base/vendor/SDL2/include \
        -Iexternal-cheat-base/vendor/imgui \
        -Ivendor/civetweb/include \
        -Iexternal-cheat-base/src \
        -Iexternal-cheat-base/src/core \
        -Iexternal-cheat-base/src/features \
        -Iexternal-cheat-base/src/utils \
        -Iexternal-cheat-base/generated \
        external-cheat-base/src/features/esp.cpp \
        external-cheat-base/src/features/aimbot.cpp \
        external-cheat-base/src/features/local_radar/local_fixed_radar.cpp \
        external-cheat-base/src/features/web_radar/public_relay_producer.cpp \
        external-cheat-base/src/features/web_radar/web_radar_service.cpp \
        external-cheat-base/src/main.cpp \
        external-cheat-base/src/core/game/web_radar_json.cpp \
        external-cheat-base/src/core/memory/memory.cpp \
        external-cheat-base/src/core/renderer/sdl_renderer.cpp \
        external-cheat-base/vendor/imgui/imgui.cpp \
        external-cheat-base/vendor/imgui/imgui_demo.cpp \
        external-cheat-base/vendor/imgui/imgui_draw.cpp \
        external-cheat-base/vendor/imgui/imgui_tables.cpp \
        external-cheat-base/vendor/imgui/imgui_widgets.cpp \
        external-cheat-base/vendor/imgui/imgui_impl_sdl2.cpp \
        external-cheat-base/vendor/imgui/imgui_impl_sdlrenderer2.cpp \
        /tmp/third-party/civetweb.o \
        /tmp/third-party/CivetServer.o \
        external-cheat-base/vendor/SDL2/lib/x64/SDL2.lib \
        -ldwmapi \
        -lgdi32 \
        -lws2_32 \
        -lshell32 \
        -lbcrypt \
        -lwinhttp \
        -lole32 \
        -lwindowscodecs \
        -luuid \
        -pthread \
        -static \
        -static-libgcc \
        -static-libstdc++ \
        -o /artifacts/external-cheat-base.exe \
    && cp external-cheat-base/vendor/SDL2/lib/x64/SDL2.dll /artifacts/SDL2.dll \
    && mkdir -p /artifacts/web-radar \
    && cp -a web-radar/dist /artifacts/web-radar/dist \
    && cp vendor/civetweb/LICENSE.md /artifacts/CIVETWEB-LICENSE.md

FROM scratch AS artifacts
COPY --from=builder /artifacts/ /
