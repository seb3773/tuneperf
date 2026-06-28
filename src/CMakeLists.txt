# Extract version from tuneperf.sh at configuration time
file(READ "${CMAKE_SOURCE_DIR}/tuneperf.sh" TUNEPERF_CONTENT)
string(REGEX MATCH "TUNEPERF_VERSION=\"([0-9.]+)\"" _ "${TUNEPERF_CONTENT}")
set(TUNEPERF_VERSION "${CMAKE_MATCH_1}")
if(NOT TUNEPERF_VERSION)
    set(TUNEPERF_VERSION "1.0")
endif()
add_definitions(-DTUNEPERF_VERSION="${TUNEPERF_VERSION}")

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/libs/process
  ${CMAKE_SOURCE_DIR}/libs/concurrent
  ${CMAKE_SOURCE_DIR}/libs/slidingstackedwidget
  ${CMAKE_SOURCE_DIR}/libs/toggle-buttons
  ${CMAKE_SOURCE_DIR}/libs/progress-circle
  ${CMAKE_CURRENT_SOURCE_DIR}/zx0
)

if( STATIC_BUILD )
  add_definitions(-DSTATIC_BUILD)
  # Build statique : headers natifs tq*.h + shim ntq*.h
  include_directories( ${TQT_STATIC_INCLUDE_DIR} ${TQT_NTQ_SHIM_DIR} )
  link_directories( ${TQT_STATIC_LIB_DIR} )
else( STATIC_BUILD )
  # Build dynamique TDE
  include_directories( ${TDE_INCLUDE_DIR} ${TQT_INCLUDE_DIRS} )
  link_directories( ${TQT_LIBRARY_DIRS} )
endif( STATIC_BUILD )

##### tuneperfs-gui (executable) ###############################

set( LIBS_DIR ${CMAKE_SOURCE_DIR}/libs )

set( libs_SRCS
  ${LIBS_DIR}/process/tqtprocess.cpp
  ${LIBS_DIR}/concurrent/tqtconcurrent.cpp
  ${LIBS_DIR}/slidingstackedwidget/tqtslidingstackedwidget.cpp
  ${LIBS_DIR}/toggle-buttons/tqttoggleswitch.cpp
  ${LIBS_DIR}/progress-circle/tqtprogresscircle2.cpp
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tuneperf_sh.h"
    COMMAND xxd -i "tuneperf.sh" "${CMAKE_CURRENT_BINARY_DIR}/tuneperf_sh.h"
    DEPENDS "${CMAKE_SOURCE_DIR}/tuneperf.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Embedding tuneperf.sh script into binary resource"
    VERBATIM
)


file(GLOB ICON_PNGS "${CMAKE_SOURCE_DIR}/icons/*.png")
file(GLOB TXT_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/translations/*.txt")

set(ASSET_POOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/asset_pool")
set(ZX0EM_COMPRESSOR "/home/cdef/_PROJETS/zx0em/binaries/zx0em_pack")

add_custom_command(
    OUTPUT
         "${CMAKE_CURRENT_SOURCE_DIR}/embedded_images_data.h"
         "${CMAKE_CURRENT_SOURCE_DIR}/embedded_images_data.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/embedded_icons.h"
         "${CMAKE_CURRENT_SOURCE_DIR}/embedded_assets_compat.cpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSET_POOL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy ${ICON_PNGS} ${TXT_ASSETS} "${ASSET_POOL_DIR}/"
    COMMAND python3 "${CMAKE_SOURCE_DIR}/scripts/build_assets.py" "${ASSET_POOL_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "${ZX0EM_COMPRESSOR}"
    DEPENDS ${ICON_PNGS} ${TXT_ASSETS} "${CMAKE_SOURCE_DIR}/scripts/build_assets.py"
    COMMENT "Compressing and packaging embedded assets using zx0em"
)

add_custom_target(embedded_assets DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded_images_data.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded_images_data.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded_icons.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded_assets_compat.cpp"
)

set( src_SRCS
  mainwindow.cpp
  translationmanager.cpp
  tuneperf_runner.cpp
  customtabwidget.cpp
  passworddialog.cpp
  zx0/zx0_decompress.c
  ${CMAKE_CURRENT_BINARY_DIR}/tuneperf_sh.h
  embedded_images_data.cpp
  embedded_assets_compat.cpp
)

set( tuneperfs_gui_SRCS
  ${libs_SRCS}
  ${src_SRCS}
)

if( STATIC_BUILD )
  ############################################################
  # Build statique : TQt3 lié statiquement (libtqt-mt.a), pas de TDE
  ############################################################

  # Point d'entrée
  list( APPEND tuneperfs_gui_SRCS main.cpp )

  # MOC : chaque header contenant TQ_OBJECT a son .moc inclus à la fin
  # du .cpp correspondant. On génère les .moc avec le tqmoc natif TQt3.
  # La convention est : <basename>.moc à partir de <basename>.h.
  set( _moc_headers
    mainwindow
    tuneperf_runner
    customtabwidget
    passworddialog
    tqtconcurrent
    tqtprocess
    tqtslidingstackedwidget
    tqttoggleswitch
    tqtprogresscircle2
  )
  foreach( _h ${_moc_headers} )
    set( _out    ${CMAKE_CURRENT_BINARY_DIR}/${_h}.moc )
    # résolution du chemin du header (src/ ou libs/...)
    unset( _src_file CACHE )
    find_file( _src_file NAMES ${_h}.h
      PATHS ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/libs/process
            ${CMAKE_SOURCE_DIR}/libs/concurrent
            ${CMAKE_SOURCE_DIR}/libs/slidingstackedwidget
            ${CMAKE_SOURCE_DIR}/libs/toggle-buttons
            ${CMAKE_SOURCE_DIR}/libs/progress-circle
      NO_DEFAULT_PATH )
    add_custom_command(
      OUTPUT  ${_out}
      COMMAND ${TQT_MOC_EXECUTABLE} ${_src_file} -o ${_out}
      DEPENDS ${_src_file}
      COMMENT "TQt3 static MOC: ${_h}.h"
      VERBATIM
    )
    list( APPEND _generated_mocs ${_out} )
  endforeach()

  # Dépendance factice : les .cpp incluent leur .moc en fin de fichier.
  # On ajoute les .moc générés comme sources de l'exécutable pour piloter le build.
  add_executable( tuneperfs-gui ${tuneperfs_gui_SRCS} ${_generated_mocs} )

  # Les headers natifs tq*.h ne fournissent que TQ_SIGNAL/TQ_SLOT (pas SIGNAL/SLOT).
  # On force-inclut un header de compat qui définit les alias anciens style.
  target_compile_options( tuneperfs-gui PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/tqt_compat_static.h )

  target_link_libraries( tuneperfs-gui
    ${TQT_STATIC_LIB}
    SM ICE X11 Xext Xrender Xrandr Xcursor Xinerama
    Xft fontconfig freetype
    uuid png jpeg z
    m dl pthread
  )

else( STATIC_BUILD )
  ############################################################
  # Build dynamique TDE (inchangé)
  ############################################################

  list( APPEND tuneperfs_gui_SRCS main.cpp )

  # Run MOC on all sources
  tde_automoc( ${tuneperfs_gui_SRCS} )

  # Disable tdelfeditor to prevent harmless but annoying errors about ELF resources
  set(_TDELFEDITOR_EXECUTABLE ${TDELFEDITOR_EXECUTABLE})
  set(TDELFEDITOR_EXECUTABLE "")

  tde_add_executable( tuneperfs-gui AUTOMOC
    SOURCES ${tuneperfs_gui_SRCS}
    LINK
      tdecore-shared tdeui-shared tdeio-shared
      pthread
    DESTINATION ${BIN_INSTALL_DIR}
  )

  # Restore tdelfeditor
  set(TDELFEDITOR_EXECUTABLE ${_TDELFEDITOR_EXECUTABLE})
  add_dependencies( tuneperfs-gui embedded_assets )
endif( STATIC_BUILD )

if (SSSTRIP_EXECUTABLE)
    add_custom_command(TARGET tuneperfs-gui POST_BUILD
        COMMAND ${SSSTRIP_EXECUTABLE} $<TARGET_FILE:tuneperfs-gui>
        VERBATIM
    )
endif()
