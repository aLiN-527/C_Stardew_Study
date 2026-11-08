# =============================================================================
# CMakeLists.txt —— 本项目唯一的构建脚本（真正的“构建入口”）
# -----------------------------------------------------------------------------
# 说明：项目根目录下没有手写的 Makefile。
#   - 这份 CMakeLists.txt 是构建配置的“源文件”；
#   - build/Makefile 是 CMake 根据它**自动生成**的产物（里面是几百行机器生成规则），
#     千万不要手改，因为它每次执行 cmake 都会被覆盖；要改构建行为就改本文件。
#
# 典型用法（在项目根目录执行）：
#   cmake -S . -B build            # 生成构建文件（-S 源码目录，-B 构建目录）
#   cmake --build build            # 编译（Windows 下等价于在 build 里执行 make/mingw32-make）
#   cmake --build build --clean-first   # 改过本文件或改了资源后，干净重编一次
#
# 本文件两条“主干”工作流：
#   [源码] src/main.c  ──┐
#                        ├─→ 可执行文件 build/game.exe ──→ 运行时读 build/assets/
#   [库]   lib/raylib  ──┘                                  （第 18 行负责把资源拷过去）
# =============================================================================

# 要求的最低 CMake 版本。低于它的 cmake 会直接报错退出，避免语法不兼容。
cmake_minimum_required(VERSION 3.18)

# 声明工程名与语言。project(stardew_demo C) 里的 C 表示只启用 C 编译器，
# 不找 C++/Fortran，能加快配置速度；若以后加了 .cpp 文件，要改成 C CXX。
project(stardew_demo C)

# --- C 语言标准 ---------------------------------------------------------------
# 99 是够用又保守的选择（允许 // 注释、变长数组、声明与语句混排）。
# 本项目实际用到的特性：复合字面量 (Rectangle){...} —— C99 起才标准。
# ⚠ 注意：main.c 里字符串字面量用了 u8"..."（UTF-8），严格来说这是 C11 的写法，
#   GCC/Clang 在 -std=c99 下按扩展也能接受；若换到严格模式的编译器报错，
#   把这里的 99 改成 11 即可（对现有代码行为无影响）。
set(CMAKE_C_STANDARD 99)
# ON = 不允许编译器“降级到更老的方言”，保证确实是 C99 而不是 gnu89。
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- 引入 raylib -------------------------------------------------------------
# 把 lib/raylib 作为子工程一起构建：源码就在仓库里，离线可用，不需要联网下载。
# 这一步会把 raylib 的目标 raylib（静态库或动态库）注册进当前工程，
# 下面第 15 行才有东西可链接；add_subdirectory 必须写在 target_link_libraries 之前。
add_subdirectory(lib/raylib)

# --- 生成可执行文件 -----------------------------------------------------------
# 目标名 game → Windows 上产出 build/game.exe。
# src/main.c 是相对 CMAKE_SOURCE_DIR（即本文件所在目录）的路径；
# 以后拆模块（player.c / map.c ...）在这里追加，例如：
#   add_executable(game src/main.c src/player.c src/map.c)
add_executable(game src/main.c)

# --- 链接库 -------------------------------------------------------------------
# 把 raylib 链到 game 上（Windows 下 raylib 自身还会顺带带出 opengl32/gdi32/winmm 等系统库）。
# 只在 target_link_libraries 里写库名，不用手动写 -lraylib -L...，
# 因为 add_subdirectory 已经把“库在哪、头文件在哪”都登记好了。
target_link_libraries(game raylib)

# --- 资源拷贝（Windows 便捷项）--------------------------------------------------
# 把源码目录里的 assets/ 整份复制到构建目录（build/assets/），每次“配置阶段”执行一次。
# 目的：exe 运行时用相对路径 "assets/xxx.png" 找素材，让 build 目录自成一体、可直接双击运行。
# CMAKE_SOURCE_DIR = 项目根目录；CMAKE_BINARY_DIR = build 目录。
# 注意两点：
#   1) file(COPY ...) 在“配置”时执行、不是每次编译：新增/修改了图片后，
#      要么重跑一次 cmake，要么删掉 build/assets 再配置，否则看到的是旧图；
#   2) 它只拷不删，旧游戏里删掉的素材仍会残留在 build/assets 里。
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})
