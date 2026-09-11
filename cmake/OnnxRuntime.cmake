# Reproducible prebuilt runtime selection. No ONNX Runtime source build is needed.
include(FetchContent)
set(SVAI_ORT_FLAVOR "DIRECTML" CACHE STRING "DIRECTML, CPU or CUDA")
set_property(CACHE SVAI_ORT_FLAVOR PROPERTY STRINGS DIRECTML CPU CUDA)
if(SVAI_ORT_FLAVOR STREQUAL "DIRECTML")
  FetchContent_Declare(svai_ort
    URL https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg
    DOWNLOAD_NAME onnxruntime-directml.zip
    URL_HASH SHA256=57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_Declare(svai_directml
    URL https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.4/microsoft.ai.directml.1.15.4.nupkg
    DOWNLOAD_NAME directml.zip
    URL_HASH SHA256=4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(svai_ort svai_directml)
  set(SVAI_ORT_INCLUDE "${svai_ort_SOURCE_DIR}/build/native/include")
  set(SVAI_ORT_LIB "${svai_ort_SOURCE_DIR}/runtimes/win-x64/native/onnxruntime.lib")
  set(SVAI_ORT_DLL_DIR "${svai_ort_SOURCE_DIR}/runtimes/win-x64/native")
  set(SVAI_ORT_VERSION "1.24.4")
else()
  if(SVAI_ORT_FLAVOR STREQUAL "CUDA")
    set(ort_archive onnxruntime-win-x64-gpu-1.25.0.zip)
    set(ort_hash 125c9fe408f41b9ae1ad7138dac5ebb19a85e65438d1e368d21b50e6abb32f4e)
  elseif(SVAI_ORT_FLAVOR STREQUAL "CPU")
    set(ort_archive onnxruntime-win-x64-1.25.0.zip)
    set(ort_hash da753f762bf2400e7191ec594086b186a7051d5af8dc886f6e2020c2403df738)
  else()
    message(FATAL_ERROR "Unsupported SVAI_ORT_FLAVOR")
  endif()
  FetchContent_Declare(svai_ort
    URL "https://github.com/microsoft/onnxruntime/releases/download/v1.25.0/${ort_archive}"
    URL_HASH "SHA256=${ort_hash}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(svai_ort)
  set(SVAI_ORT_INCLUDE "${svai_ort_SOURCE_DIR}/include")
  set(SVAI_ORT_LIB "${svai_ort_SOURCE_DIR}/lib/onnxruntime.lib")
  set(SVAI_ORT_DLL_DIR "${svai_ort_SOURCE_DIR}/lib")
  set(SVAI_ORT_VERSION "1.25.0")
endif()
add_library(OnnxRuntime SHARED IMPORTED GLOBAL)
set_target_properties(OnnxRuntime PROPERTIES IMPORTED_IMPLIB "${SVAI_ORT_LIB}"
  IMPORTED_LOCATION "${SVAI_ORT_DLL_DIR}/onnxruntime.dll"
  INTERFACE_INCLUDE_DIRECTORIES "${SVAI_ORT_INCLUDE}")
file(GLOB SVAI_ORT_RUNTIME_DLLS "${SVAI_ORT_DLL_DIR}/*.dll")
if(SVAI_ORT_FLAVOR STREQUAL "DIRECTML")
  list(APPEND SVAI_ORT_RUNTIME_DLLS "${svai_directml_SOURCE_DIR}/bin/x64-win/DirectML.dll")
endif()
function(svai_copy_runtime target)
  foreach(runtime IN LISTS SVAI_ORT_RUNTIME_DLLS)
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${runtime}" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
  endforeach()
endfunction()
