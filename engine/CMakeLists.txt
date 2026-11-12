cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(opensky C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "OpenSKY")
set(VITA_TITLEID  "OSKY00001")
set(VITA_VERSION  "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -ftree-vectorize -mfpu=neon -mcpu=cortex-a9")

add_executable(${PROJECT_NAME}
  src/main.c
  src/dtree.c
  src/sktex.c
  src/skmesh.c
  src/lighting.c
  src/terrain.c
  src/objects.c
  src/collision.c
  src/music.c
  src/stream.c
  src/fileio.c
  src/shader.c
  src/post.c
  src/atmosphere.c
  src/water.c
  src/navmesh.c
  src/ways.c
  src/people.c
  src/wordfx.c
  src/actors.c
  src/fish.c
  src/vitals.c
  src/skills.c
  src/magic.c
  src/spellfx.c
  src/doors.c
  src/items.c
  src/loot.c
  src/locks.c
  src/crime.c
  src/followers.c
  src/topics.c
  src/services.c
  src/brawl.c
  src/riding.c
  src/beast.c
  src/companions.c
  src/questkit.c
  src/college.c
  src/thieves.c
  src/lockpick.c
  src/alchemy.c
  src/effects.c
  src/enchant.c
  src/autotest.c
  src/quests.c
  src/intro.c
  src/name.c
  src/stones.c
  src/chargen.c
  src/racemenu.c
  src/perf.c
  src/gamelog.c
  src/gametext.c
  src/equip.c
  src/preview.c
  src/worlds.c
  src/races.c
  src/activators.c
  src/furniture.c
  src/sfx.c
  src/weather.c
  src/creatures.c
  src/perks.c
  src/projectiles.c
  src/traps.c
  src/worldmap.c
  src/shouts.c
  src/save.c
  src/anim.c
  src/adpcm.c
  src/voice.c
  src/ui.c
  src/talk.c
  src/interior.c
  src/menu.c
)

# vitaGL rebuilt from third_party/vitaGL (pinned to vdpm's commit 2bdbe89) with
#  - NO_SPLASHSCREEN=1: the boot splash thread crashes Vita3K on macOS
#  - HAVE_SHADER_CACHE=1: compiled shaders kept on the card (ux0:data/opensky_shaders): the
#    game's every shader compiled only at its first start, not at each (the Nov 8 boot's black)
#  - patches/vitagl.patch: CPU-swizzled DXT uploads, full mip chain allocation, an 8 MB
#    uniform pool (+ a usage counter)
# To rebuild: git -C third_party/vitaGL apply ../../patches/vitagl.patch
#             make -C third_party/vitaGL NO_SPLASHSCREEN=1 HAVE_SHADER_CACHE=1 libvitaGL.a
set(VITAGL_LIB "${CMAKE_SOURCE_DIR}/../third_party/vitaGL/libvitaGL.a")
if(NOT EXISTS ${VITAGL_LIB})
  message(FATAL_ERROR "Build vitaGL first (see the comment above VITAGL_LIB in engine/CMakeLists.txt)")
endif()

target_link_libraries(${PROJECT_NAME}
  ${VITAGL_LIB} vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub
  SceKernelDmacMgr_stub SceCommonDialog_stub SceGxm_stub SceDisplay_stub ScePower_stub SceTouch_stub
  SceAppMgr_stub SceCtrl_stub SceAudio_stub SceRtc_stub stdc++ m c
)
# (room after the code for vita-elf-create: see sce_room.ld)
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-T,${CMAKE_SOURCE_DIR}/sce_room.ld")
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/sce_room.ld)

# The pack (converted Skyrim data) is deliberately NOT packaged into the VPK;
# the engine reads it from ux0:data/opensky/ at runtime.
vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
)
