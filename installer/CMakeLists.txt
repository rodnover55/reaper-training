# Установщик расширения для системы, на которой идёт сборка (openspec
# add-installers, design D5): `cmake --build --preset installer`. Результат —
# в этом каталоге сборки:
#
#   reaper-training-<версия>-windows-x64-setup.exe   Inno Setup
#   reaper-training-<версия>-macos.pkg               pkgbuild и productbuild
#   reaper-training-<версия>-linux-x86_64.run        makeself
#
# Цель не входит в all: обычной сборке инструменты установщика не нужны.

set(installer_base "reaper-training-${PROJECT_VERSION}")

# Заводит цель installer, сборка которой печатает MESSAGE и завершается
# ошибкой. Так нехватка инструмента не ломает конфигурацию, но и не проходит
# молча там, где установщик собирают.
function(training_installer_unavailable message)
  add_custom_target(installer
    COMMAND "${CMAKE_COMMAND}" -E echo "${message}"
    COMMAND "${CMAKE_COMMAND}" -E false
    VERBATIM)
endfunction()

if(WIN32)
  # Inno Setup 6 ставится в Program Files (x86), 7 — в Program Files; с
  # winget — в профиль пользователя.
  set(pf86 "ProgramFiles(x86)")
  find_program(ISCC_EXECUTABLE iscc
    PATHS
      "$ENV{${pf86}}/Inno Setup 6"
      "$ENV{ProgramFiles}/Inno Setup 7"
      "$ENV{${pf86}}/Inno Setup 7"
      "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
      "$ENV{LOCALAPPDATA}/Programs/Inno Setup 7")

  if(NOT ISCC_EXECUTABLE)
    training_installer_unavailable(
      "Установщик: нет Inno Setup (iscc). Поставьте его: winget install JRSoftware.InnoSetup")
    return()
  endif()

  set(installer_name "${installer_base}-windows-x64-setup")
  set(installer_script "${CMAKE_CURRENT_SOURCE_DIR}/windows/reaper-training.iss")
  set(installer_file "${CMAKE_CURRENT_BINARY_DIR}/${installer_name}.exe")

  add_custom_command(
    OUTPUT "${installer_file}"
    COMMAND "${ISCC_EXECUTABLE}" /Qp
      "/DAppVersion=${PROJECT_VERSION}"
      "/DSourceFile=$<SHELL_PATH:$<TARGET_FILE:reaper_training>>"
      "/O$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}>"
      "/F${installer_name}"
      "$<SHELL_PATH:${installer_script}>"
    DEPENDS reaper_training "${installer_script}"
    COMMENT "Установщик для Windows: ${installer_name}.exe"
    VERBATIM)

elseif(APPLE)
  find_program(PKGBUILD_EXECUTABLE pkgbuild)
  find_program(PRODUCTBUILD_EXECUTABLE productbuild)

  if(NOT PKGBUILD_EXECUTABLE OR NOT PRODUCTBUILD_EXECUTABLE)
    training_installer_unavailable(
      "Установщик: нет pkgbuild или productbuild. Поставьте Xcode Command Line Tools: xcode-select --install")
    return()
  endif()

  set(installer_file "${CMAKE_CURRENT_BINARY_DIR}/${installer_base}-macos.pkg")
  set(pkg_identifier "io.github.rodnover55.reaper-training")
  set(pkg_root "${CMAKE_CURRENT_BINARY_DIR}/macos-root")
  set(pkg_components "${CMAKE_CURRENT_BINARY_DIR}/macos-components")
  set(pkg_resources "${CMAKE_CURRENT_SOURCE_DIR}/macos/resources")

  configure_file(macos/distribution.xml.in
    "${CMAKE_CURRENT_BINARY_DIR}/distribution.xml" @ONLY)
  file(GLOB_RECURSE pkg_resource_files CONFIGURE_DEPENDS "${pkg_resources}/*")

  # Путь установки отсчитывается от домашней папки: описание пакета разрешает
  # ставить только туда (design D3), и модуль попадает в
  # ~/Library/Application Support/REAPER/UserPlugins.
  add_custom_command(
    OUTPUT "${installer_file}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${pkg_root}" "${pkg_components}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${pkg_root}" "${pkg_components}"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:reaper_training>" "${pkg_root}"
    COMMAND "${PKGBUILD_EXECUTABLE}"
      --root "${pkg_root}"
      --identifier "${pkg_identifier}"
      --version "${PROJECT_VERSION}"
      --install-location "/Library/Application Support/REAPER/UserPlugins"
      "${pkg_components}/reaper-training.pkg"
    COMMAND "${PRODUCTBUILD_EXECUTABLE}"
      --distribution "${CMAKE_CURRENT_BINARY_DIR}/distribution.xml"
      --resources "${pkg_resources}"
      --package-path "${pkg_components}"
      "${installer_file}"
    DEPENDS reaper_training "${CMAKE_CURRENT_BINARY_DIR}/distribution.xml"
      ${pkg_resource_files}
    COMMENT "Установщик для macOS: ${installer_base}-macos.pkg"
    VERBATIM)

else()
  find_program(MAKESELF_EXECUTABLE NAMES makeself makeself.sh)

  if(NOT MAKESELF_EXECUTABLE)
    training_installer_unavailable(
      "Установщик: нет makeself. Поставьте его: sudo apt install makeself")
    return()
  endif()

  set(installer_file "${CMAKE_CURRENT_BINARY_DIR}/${installer_base}-linux-x86_64.run")
  set(run_root "${CMAKE_CURRENT_BINARY_DIR}/linux-root")
  set(install_script "${CMAKE_CURRENT_SOURCE_DIR}/linux/install.sh")

  # makeself запускает сценарий одной командой в кавычках, поэтому `sh` и путь
  # к сценарию — отдельные аргументы. Через sh, а не напрямую: временная папка
  # бывает смонтирована с noexec.
  add_custom_command(
    OUTPUT "${installer_file}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${run_root}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${run_root}"
    COMMAND "${CMAKE_COMMAND}" -E copy
      "${install_script}" "$<TARGET_FILE:reaper_training>" "${run_root}"
    COMMAND "${MAKESELF_EXECUTABLE}" --quiet --tar-quietly --nox11
      --nomd5 --nocrc --sha256
      "${run_root}" "${installer_file}" "reaper-training ${PROJECT_VERSION}"
      sh ./install.sh
    DEPENDS reaper_training "${install_script}"
    COMMENT "Установщик для Linux: ${installer_base}-linux-x86_64.run"
    VERBATIM)

  # Сценарий проверяется без REAPER и без makeself: подставная домашняя папка,
  # поддельные REAPER и Flatpak.
  if(REAPER_TRAINING_BUILD_TESTS)
    add_test(NAME linux_install_script
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/linux/test_install.sh" "${install_script}")
  endif()
endif()

add_custom_target(installer DEPENDS "${installer_file}")
