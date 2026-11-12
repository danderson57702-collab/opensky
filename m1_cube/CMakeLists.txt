cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(m1_cube C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Skyrim-lite M1 Cube")
set(VITA_TITLEID  "OSKY00000")
set(VITA_VERSION  "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -ftree-vectorize -mfpu=neon -mcpu=cortex-a9")

add_executable(${PROJECT_NAME} src/main.c)

# vitaGL rebuilt with NO_SPLASHSCREEN=1 (third_party/vitaGL, pinned to vdpm's
# commit 2bdbe89): the boot splash thread crashes Vita3K on macOS.
set(VITAGL_LIB "${CMAKE_SOURCE_DIR}/../third_party/vitaGL/libvitaGL.a")
if(NOT EXISTS ${VITAGL_LIB})
  message(FATAL_ERROR "Build vitaGL first: make -C third_party/vitaGL NO_SPLASHSCREEN=1 libvitaGL.a")
endif()

target_link_libraries(${PROJECT_NAME}
  ${VITAGL_LIB} vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub
  SceKernelDmacMgr_stub SceCommonDialog_stub SceGxm_stub SceDisplay_stub
  SceAppMgr_stub SceCtrl_stub stdc++ m c
)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)
