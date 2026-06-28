################################################
# tuneperfs-gui - Linux Performance Tuner GUI
################################################
cmake_minimum_required(VERSION 3.5)
project(tuneperfs-gui)

option( STATIC_BUILD "Link TQt3 statically (libtqt-mt.a), drop TDE dependency" OFF )

### TDE setup ###############################################
# Le setup TDE n'est nécessaire que pour le build dynamique standard.
# En STATIC_BUILD on utilise la lib statique intégrée au projet (libs/tqt3-static/).
if( NOT STATIC_BUILD )

if( NOT DEFINED TDE_PREFIX )
  set( TDE_PREFIX "/opt/trinity" )
endif()
list( APPEND CMAKE_MODULE_PATH "${TDE_PREFIX}/share/cmake/modules" )

include( TDESetupPaths )
tde_setup_paths( )

include( TDEMacros )

include( FindPkgConfig )

##### check for TDE ########################################
find_package( TQt )
find_package( TDE )

tde_setup_architecture_flags( )
tde_setup_largefiles( )

else( NOT STATIC_BUILD )

##### static TQt3 setup #####################################
# Lib TQt3 statique embarquée dans le projet (cf. setup-tqt3-static.sh)
set( TQT_STATIC_DIR "${CMAKE_SOURCE_DIR}/libs/tqt3-static" )
set( TQT_STATIC_INCLUDE_DIR "${TQT_STATIC_DIR}/include" )
set( TQT_STATIC_LIB_DIR "${TQT_STATIC_DIR}/lib" )
set( TQT_STATIC_LIB "${TQT_STATIC_LIB_DIR}/libtqt-mt.a" )
set( TQT_MOC_EXECUTABLE "${TQT_STATIC_DIR}/bin/tqmoc" )
# Shim de redirection ntq*.h -> tq*.h (la lib statique ne fournit que tq*.h)
set( TQT_NTQ_SHIM_DIR "${TQT_STATIC_DIR}/ntq-shim" )

# Vérification que le setup a bien été exécuté
if( NOT EXISTS "${TQT_STATIC_LIB}" )
  message( FATAL_ERROR
    "libtqt-mt.a introuvable : ${TQT_STATIC_LIB}\n"
    "Lancez d'abord : bash setup-tqt3-static.sh" )
endif()
if( NOT EXISTS "${TQT_MOC_EXECUTABLE}" )
  message( FATAL_ERROR
    "tqmoc introuvable : ${TQT_MOC_EXECUTABLE}\n"
    "Lancez d'abord : bash setup-tqt3-static.sh" )
endif()

endif( NOT STATIC_BUILD )

##### compiler flags #######################################
if( NOT STATIC_BUILD )
  add_definitions(
    -DHAVE_CONFIG_H
    -UTQT_NO_COMPAT
  )
endif( NOT STATIC_BUILD )

set(CMAKE_CXX_FLAGS "-Os -flto=auto -fstrict-aliasing -ffunction-sections -fdata-sections -fno-ident -fno-plt -fomit-frame-pointer -fno-stack-protector -fno-math-errno -fvisibility=hidden -fno-threadsafe-statics -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -Wno-deprecated-declarations -DQT_SHARED -DQT_TABLET_SUPPORT -DTQT_NO_DEBUG -DQT_NO_EXCEPTIONS -DTQT_THREAD_SUPPORT -D_REENTRANT -fpermissive -std=gnu++17")
set(CMAKE_EXE_LINKER_FLAGS "-Wl,-O1 -Wl,--gc-sections -Wl,--as-needed -s")

# Ensure no per-config C++ flags are appended by CMake.
set(CMAKE_CXX_FLAGS_DEBUG "")
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "")
set(CMAKE_CXX_FLAGS_MINSIZEREL "")

find_program(SSSTRIP_EXECUTABLE NAMES sstrip)

##### subdirectories #######################################
add_subdirectory( src )
