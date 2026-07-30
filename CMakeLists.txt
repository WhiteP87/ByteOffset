cmake_minimum_required(VERSION 3.21)

# Фикс для сборки Hyperscan под новыми CMake
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(ByteOffset LANGUAGES CXX)

if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

include(CheckCXXSourceRuns)

#единый CRT для всего решения и подпроектов
if (MSVC)
  cmake_policy(SET CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
      CACHE STRING "" FORCE)    # /MD в Release, /MDd в Debug

  # Проверяем поддержку AVX2 на текущем процессоре
  set(CMAKE_REQUIRED_FLAGS "/arch:AVX2")
  check_cxx_source_runs("
    #include <immintrin.h>
    int main() { __m256i a = _mm256_setzero_si256(); return 0; }" CPU_HAS_AVX2)
  unset(CMAKE_REQUIRED_FLAGS)

  if(CPU_HAS_AVX2)
    add_compile_options(/arch:AVX2)
    message(STATUS "AVX2 enabled (Windows)")
  else()
    message(STATUS "AVX2 not supported, building for legacy CPU (Windows)")
  endif()

  add_definitions(-DBOOST_NO_EXCEPTIONS) 
  add_compile_options(/EHsc)
else()

  add_definitions(-DHAVE_STD_MAKE_UNIQUE)

  # Linux (GCC/Clang) доверяем native
  add_compile_options(-march=native -fPIC)
  message(STATUS "Using -march=native (Linux)")

  set(CMAKE_REQUIRED_FLAGS "-march=native")
  
  # Для Hyperscan проверяем, выставил ли native поддержку AVX2
  check_cxx_source_runs("
    int main() {
    #if !defined(__AVX2__)
    #error No AVX2
    #endif
    return 0; }" CPU_HAS_AVX2)
  unset(CMAKE_REQUIRED_FLAGS)

  if(CPU_HAS_AVX2)
    message(STATUS "AVX2 detected via -march=native (Linux)")
  else()
    message(WARNING "AVX2 NOT detected! Hyperscan might be slow.")
  endif()
endif()

#Стандарт C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#Автогенерация Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

#Куда складывать готовые бинарники (exe/dll/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/dist)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/dist)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/dist)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

include(FetchContent)

#минимальная сборка capstone без тестов, только нужные архитектуры
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "" FORCE)
set(CAPSTONE_X86_SUPPORT ON CACHE BOOL "" FORCE)
set(CAPSTONE_ARM64_SUPPORT ON CACHE BOOL "" FORCE)
set(CAPSTONE_ARM_SUPPORT  ON CACHE BOOL "" FORCE)

set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)   
set(CAPSTONE_BUILD_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)

#линковка статической capstone в .so на Linux
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(PROJECT_VERSION "5.0.7")

#путь к архиву
FetchContent_Declare(capstone
  URL ${CMAKE_SOURCE_DIR}/src/external/capstone-5.0.7.tar.xz
  # URL_HASH SHA256=<хэш>
)
FetchContent_MakeAvailable(capstone)

unset(PROJECT_VERSION)

#избежать коллизию имён Capstone на Windows
set(_CAPSTONE_TGT "")

if (TARGET capstone_shared)
  # будем линковаться к shared
  set(_CAPSTONE_TGT capstone_shared)

  # import-lib от DLL не должен называться capstone.lib, иначе конфликтует со статиком
  if (WIN32)
    set_target_properties(capstone_shared PROPERTIES ARCHIVE_OUTPUT_NAME capstone_imp) # capstone_imp.lib
  endif()

  # статик выключаем из default-сборки, чтобы он не порождал свой capstone.lib
  if (TARGET capstone_static)
    set_target_properties(capstone_static PROPERTIES
      EXCLUDE_FROM_ALL ON
      EXCLUDE_FROM_DEFAULT_BUILD ON
      OUTPUT_NAME capstone_static) #переименуем в capstone_static.lib
  endif()

elseif (TARGET capstone)
  get_target_property(_ty capstone TYPE)
  if (_ty STREQUAL "SHARED_LIBRARY")
    set(_CAPSTONE_TGT capstone)
    if (WIN32)
      set_target_properties(capstone PROPERTIES ARCHIVE_OUTPUT_NAME capstone_imp)
    endif()
  else()
    # fallback: статик
    set(_CAPSTONE_TGT capstone)
  endif()

elseif (TARGET capstone_static)
  #есть только статик
  set(_CAPSTONE_TGT capstone_static)

else()
  message(FATAL_ERROR "Не найден capstone")
endif()

# --- Ragel (нужен для Hyperscan) ---
option(USE_SYSTEM_RAGEL "Use ragel from PATH (do not auto-download on Windows)" OFF)

set(RAGEL_VERSION "6.10" CACHE STRING "Ragel version for Windows auto-download")
set(RAGEL_WIN_URL "https://github.com/PolarGoose/Ragel-for-Windows/releases/download/ragel-${RAGEL_VERSION}/Ragel.zip"
    CACHE STRING "Ragel zip URL for Windows auto-download")

set(_RAGEL_DIR     "${CMAKE_BINARY_DIR}/_deps/ragel-${RAGEL_VERSION}")
set(_RAGEL_ZIP     "${CMAKE_BINARY_DIR}/_deps/ragel-${RAGEL_VERSION}.zip")

if (WIN32)
  if (USE_SYSTEM_RAGEL)
    find_program(RAGEL_BIN NAMES ragel ragel.exe REQUIRED)
  else()
    # 1) попробуем найти в PATH
    find_program(RAGEL_BIN NAMES ragel.exe ragel)

    # 2) если не нашли — качаем и распаковываем
    if (NOT RAGEL_BIN)
      message(STATUS "Ragel not found. Downloading ${RAGEL_WIN_URL}")
      file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")

      file(DOWNLOAD "${RAGEL_WIN_URL}" "${_RAGEL_ZIP}"
           SHOW_PROGRESS
           TLS_VERIFY ON
           STATUS _dl_status)

      list(GET _dl_status 0 _dl_code)
      if (NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        message(FATAL_ERROR "Failed to download Ragel: ${_dl_msg}")
      endif()

      file(MAKE_DIRECTORY "${_RAGEL_DIR}")
      file(ARCHIVE_EXTRACT INPUT "${_RAGEL_ZIP}" DESTINATION "${_RAGEL_DIR}")

      # Внутри Ragel.zip обычно лежит Ragel.exe (или ragel.exe) в корне
      find_program(RAGEL_BIN NAMES Ragel.exe ragel.exe ragel
                   PATHS "${_RAGEL_DIR}" "${_RAGEL_DIR}/bin"
                   NO_DEFAULT_PATH)

      if (NOT RAGEL_BIN)
        message(FATAL_ERROR "Ragel downloaded but executable not found in ${_RAGEL_DIR}")
      endif()
    endif()
  endif()
else()
  # Linux/macOS: ragel должен быть в системе
  find_program(RAGEL_BIN NAMES ragel REQUIRED)
endif()

set(RAGEL "${RAGEL_BIN}" CACHE FILEPATH "Path to Ragel" FORCE)
message(STATUS "Using ragel: ${RAGEL}")

# --- ПОДГОТОВКА ОКРУЖЕНИЯ ДЛЯ HYPERSCAN ---
# Поиск Boost
set(Boost_USE_MULTITHREADED ON) 
if (WIN32)
  set(Boost_USE_STATIC_LIBS ON)
  set(Boost_USE_STATIC_RUNTIME OFF) # Важно: /MD для совместимости с Qt 
endif()

# Находим Boost. Нам нужны заголовки и exception 
find_package(Boost REQUIRED COMPONENTS exception) 

# Настройки Hyperscan
set(FAT_RUNTIME OFF CACHE BOOL "" FORCE) 
if(CPU_HAS_AVX2)
  set(HAVE_AVX2 ON CACHE BOOL "" FORCE)
  message(STATUS "Hyperscan will build with AVX2 support")
else()
  set(HAVE_AVX2 OFF CACHE BOOL "" FORCE)
endif()

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_UNITTESTS OFF CACHE BOOL "" FORCE) 
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)

# Принудительно включаем сборку DLL/Shared
set(_old_bsl "${BUILD_SHARED_LIBS}") 
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE) 

FetchContent_Declare(hyperscan
  GIT_REPOSITORY https://github.com/intel/hyperscan.git
  GIT_TAG        v5.4.2
)

# ПРИНУДИТЕЛЬНО 
if (NOT MSVC)
    # На Linux заставляем Hyperscan думать, что он в 2011 году
    set(CMAKE_CXX_STANDARD 11)
endif()

FetchContent_MakeAvailable(hyperscan) 

# Возвращаем C++17 для основного проекта 
set(CMAKE_CXX_STANDARD 17)

# Возвращаем флаг сборки обратно 
set(BUILD_SHARED_LIBS "${_old_bsl}" CACHE BOOL "" FORCE) 
unset(_old_bsl) 

# ФИКС ЛИНКОВКИ И ИМЕН (Windows)
if (WIN32)
    # Создаем файл заглушку для обхода LNK2001/LNK2005 (только MSVC) 
    set(BOOST_FIX_FILE "${CMAKE_BINARY_DIR}/boost_exception_fix.cpp")
    file(WRITE "${BOOST_FIX_FILE}" "
    #include <exception>
    namespace boost {
        struct source_location;
        void __vectorcall throw_exception(std::exception const & e) { (void)e; }
        void __vectorcall throw_exception(std::exception const & e, source_location const &) { (void)e; }
    }
    ")
endif()

# Список всех возможных целей Hyperscan
set(_HS_INTERNAL_TARGETS hs hs_shared hs_runtime hs_runtime_shared hs_compile hs_compile_shared)

foreach(t ${_HS_INTERNAL_TARGETS})
  if (TARGET ${t})
    # Линкуем Boost ко всем целям Hypersca
    target_link_libraries(${t} PRIVATE Boost::boost Boost::exception) 

    # Понижаем стандарт до C++11 только для Hyperscan, чтобы убрать std::make_unique>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (NOT WIN32)
        set_target_properties(${t} PROPERTIES 
            CXX_STANDARD 11 
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
    endif()

    if (WIN32)
        target_compile_definitions(${t} PRIVATE BOOST_NO_EXCEPTIONS) #
        
        # Добавляем исходник фикса только в основные DLL цели 
        if (t STREQUAL "hs_shared" OR t STREQUAL "hs_runtime_shared")
            target_sources(${t} PRIVATE "${BOOST_FIX_FILE}")
        endif()

        # Разводим имена импортных и статических библиотек для Ninja 
        get_target_property(_t_type ${t} TYPE)
        if (_t_type STREQUAL "SHARED_LIBRARY")
            set_target_properties(${t} PROPERTIES ARCHIVE_OUTPUT_NAME "${t}_imp")
        elseif(_t_type STREQUAL "STATIC_LIBRARY")
            set_target_properties(${t} PROPERTIES OUTPUT_NAME "${t}_static")
        endif()
    endif()

    # Универсальный вывод в dist (работает и для .dll, и для .so) 
    set_target_properties(${t} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
    )
  endif()
endforeach()

# Определение финального таргета для линковки проекта
set(_HS_TGT hs)
if (TARGET hs_shared)
  set(_HS_TGT hs_shared)
endif()
#-----------------------------------------------------

#Пути к исходникам
set(APP_SOURCES
  src/app/main.cpp
)

#Виджет HexEditor
set(WIDGET_SOURCES
  src/widgets/hexeditor/HexEditor.cpp
  src/widgets/hexeditor/HexEditor.h
  src/widgets/hexeditor/HexEditorTypes.h
)

#UI
set(UI_SOURCES
  src/ui/MainWindow.cpp
  src/ui/MainWindow.h
  src/ui/MainWindow.ui
  src/ui/dialogs/GotoDialog.cpp
  src/ui/dialogs/GotoDialog.h
  src/ui/dialogs/GotoDialog.ui
  src/ui/dialogs/AddBlockDialog.cpp
  src/ui/dialogs/AddBlockDialog.h
  src/ui/dialogs/AddBlockDialog.ui
  src/ui/dialogs/AddBookmarkDialog.cpp
  src/ui/dialogs/AddBookmarkDialog.h
  src/ui/dialogs/AddBookmarkDialog.ui
  src/ui/dialogs/BookmarksDialog.cpp
  src/ui/dialogs/SearchDialog.h
  src/ui/dialogs/SearchDialog.cpp
  src/ui/dialogs/SearchDialog.ui
  src/ui/codepagetool/CodepageTool.cpp
  src/ui/codepagetool/CodepageTool.h
  src/ui/codepagetool/CodepageTool.ui
  src/ui/lookups/BookmarksDialog.h
  src/ui/lookups/BookmarksDialog.ui
  src/ui/uiUtils.h
  src/ui/disasm/DisasmModel.h
  src/ui/disasm/DisasmModel.cpp
  src/ui/disasm/DisasmWindow.h
  src/ui/disasm/DisasmWindow.cpp
  src/ui/disasm/DisasmWindow.ui
  src/ui/disasm/DisasmDelegate.h
  src/ui/disasm/DisasmDelegate.cpp
  src/ui/disasm/DisasmDelegate.cpp
  src/ui/dialogs/HexInputTextEdit.h
  src/ui/dialogs/HexInputTextEdit.cpp
  src/ui/dialogs/AutoIncreaseEdit.h
  src/ui/dialogs/AutoIncreaseEdit.cpp
)

#Пути к исходникам core
set(CORE_SOURCES
  # data / provider
  src/core/data/provider/IDataProvider.h
  src/core/data/provider/MMapDataProvider.cpp
  src/core/data/provider/MMapDataProvider.h

  # data / src_reader
  src/core/data/src_reader/ISrcReader.h
  src/core/data/src_reader/MMapSrcReader.cpp
  src/core/data/src_reader/MMapSrcReader.h

  # data / pagestore
  src/core/data/pagestore/IPageStore.h
  src/core/data/pagestore/TempMMapPageStore.cpp
  src/core/data/pagestore/TempMMapPageStore.h

  # data / overlay
  src/core/data/overlay/IOverlay.h
  src/core/data/overlay/OverlayChangesFormat.h
  src/core/data/overlay/OverlayChangesReader.cpp
  src/core/data/overlay/OverlayChangesReader.h
  src/core/data/overlay/OverlayChangesWriter.cpp
  src/core/data/overlay/OverlayChangesWriter.h
  src/core/data/overlay/PagedPieceOverlay.cpp
  src/core/data/overlay/PagedPieceOverlay.h

  # annotations
  src/core/annotations/BlockManager.cpp
  src/core/annotations/BlockManager.h
  src/core/annotations/BlockManagerTypes.h
  src/core/annotations/FieldManager.cpp
  src/core/annotations/FieldManager.h
  src/core/annotations/FieldManagerTypes.h
  src/core/annotations/BookmarkManager.cpp
  src/core/annotations/BookmarkManager.h

  # configs
  src/core/configs/LayoutConfig.cpp
  src/core/configs/LayoutConfig.h
  src/core/configs/VisualConfig.cpp
  src/core/configs/VisualConfig.h

  # screen buffer
  src/core/screen_buf/ScreenBuffer.cpp
  src/core/screen_buf/ScreenBuffer.h

  # text / encoding
  src/core/text/TextCache.cpp
  src/core/text/TextCache.h
  src/core/text/DataTextTranslator.cpp
  src/core/text/DataTextTranslator.h
  src/core/text/TranslatorFactory.cpp
  src/core/text/TranslatorFactory.h
  src/core/text/TranslatorsImpl.cpp
  src/core/text/TranslatorsImpl.h
  src/core/text/CodepageRegistry.cpp
  src/core/text/CodepageRegistry.h
  src/core/text/SbcLoader.cpp
  src/core/text/SbcLoader.h
  src/core/text/EncodingUtils.h

  # analysis
  src/core/analysis/EntropyAnalyzer.cpp
  src/core/analysis/EntropyAnalyzer.h
  src/core/analysis/DisasmEngine.cpp
  src/core/analysis/DisasmEngine.h
  src/core/analysis/DisasmWorker.h

  # undo
  src/core/undo/EditorUndoController.cpp
  src/core/undo/EditorUndoController.h

  # formats
  src/core/formats/IBinaryConfig.cpp
  src/core/formats/IBinaryConfig.h
  src/core/formats/UnifiedProjectFormat.h
  src/core/formats/UnifiedProjectReader.cpp
  src/core/formats/UnifiedProjectReader.h
  src/core/formats/UnifiedProjectWriter.cpp
  src/core/formats/UnifiedProjectWriter.h
)

set(SRCS
  ${APP_SOURCES}
  ${WIDGET_SOURCES}
  ${UI_SOURCES}
  ${CORE_SOURCES}
)

#Таргет приложения
qt_add_executable(${PROJECT_NAME}
  MANUAL_FINALIZATION
  ${SRCS}
)

#Оптимизации таргета в Release/RelWithDebInfo
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
if(_ipo_ok)
  set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
else()
  message(STATUS "IPO disabled: ${_ipo_msg}")
endif()

if (MSVC)
  #Компилятор релизные флаги
  target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/O2 /GL /Gw /Gy /Zc:inline>
    #$<$<CONFIG:RelWithDebInfo>:/O2 /Gw /Gy /Zc:inline>
    $<$<CONFIG:RelWithDebInfo>:/O2 /Zi /Gw /Gy /Zc:inline>
  )
  #Линковщик LTCG и запрет инкрементальной линковки в релизах
  target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/LTCG /INCREMENTAL:NO>
    #$<$<CONFIG:RelWithDebInfo>:/INCREMENTAL:NO>
    $<$<CONFIG:RelWithDebInfo>:/DEBUG /INCREMENTAL:NO>
  )
else()
  # GCC/Clang оптимизации, видимость, вычищение неиспользуемых секций
  target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:-O3 -DNDEBUG -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections>
  )
  target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:-Wl,--gc-sections>
    $<$<CONFIG:RelWithDebInfo>:-Wl,--gc-sections>
  )
endif()

#Выключение Qt-логирование в релизной сборке
target_compile_definitions(${PROJECT_NAME} PRIVATE
  $<$<CONFIG:Release>:QT_NO_DEBUG_OUTPUT QT_NO_WARNING_OUTPUT>
)

#Windows GUI subsystem (чтобы не было окна консоли)
if (WIN32)
  set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

#AUTOUIC ищет .ui в src/ui и подпапках
set_property(TARGET ${PROJECT_NAME} PROPERTY AUTOUIC_SEARCH_PATHS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")

  message(STATUS "HS_SRC: ${hyperscan_SOURCE_DIR}")
message(STATUS "HS_BIN: ${hyperscan_BINARY_DIR}")
#Include-пути (добавлены все подпапки, чтобы не указывать полный путь)
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app

  # widgets
  ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets
  ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets/hexeditor

  # ui
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dialogs
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/codepagetool
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/lookups
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/disasm

  # core (каждый подпуть с заголовками)
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/analysis
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/annotations
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/configs
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/formats
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/screen_buf
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/text

  # core/data/*
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/data
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/data/provider
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/data/src_reader
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/data/pagestore
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/data/overlay

  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/undo

  # Добавляем пути для Hyperscan (он сам не всегда их пробрасывает)
  ${hyperscan_SOURCE_DIR}/src
  ${hyperscan_BINARY_DIR}
  ${Boost_INCLUDE_DIRS} 

  # autogen
  ${CMAKE_CURRENT_BINARY_DIR}
  $<TARGET_PROPERTY:${PROJECT_NAME},AUTOGEN_BUILD_DIR>
  $<TARGET_PROPERTY:${PROJECT_NAME},AUTOUIC_INCLUDE_DIRS>
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent ${_CAPSTONE_TGT} ${_HS_TGT} ${_HSRT_TGT})

#таблицы кодировок рядом с exe 
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/encodings"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/encodings"
          "$<TARGET_FILE_DIR:${PROJECT_NAME}>/encodings"
  VERBATIM)

#разложить Qt DLL и подчистить ненужное 
if (WIN32)
  # Основной деплой
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "$ENV{QTDIR}/bin/windeployqt.exe"
            --no-compiler-runtime
            --no-translations
            --no-opengl-sw
            --no-system-d3d-compiler
            --dir "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            "$<TARGET_FILE:${PROJECT_NAME}>"
    COMMENT "Running windeployqt into $<TARGET_FILE_DIR:${PROJECT_NAME}>"
    VERBATIM)

  # Удаление мусора после деплоя
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/generic"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/iconengines"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/imageformats"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/networkinformation"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/tls"
    COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/d3dcompiler_47.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/dxcompiler.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/dxil.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/opengl32sw.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6Networkd.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6Svgd.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6Network.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6Svg.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/capstone.lib"
    COMMENT "Cleaning up unused Qt plugins and DLLs"
    VERBATIM)
endif()

qt_finalize_executable(${PROJECT_NAME})
