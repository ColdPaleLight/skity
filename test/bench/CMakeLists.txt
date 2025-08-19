
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/google_benchmark third_party/google_benchmark)

add_executable(skity_benchmarks
    # hw_path_raster_benchmarks.cc
    # matrix_benchmarks.cc
    hw_benchmarks.cc
    sw_benchmarks.cc
    ${CMAKE_SOURCE_DIR}/example/case/basic/example.cc
    ${CMAKE_SOURCE_DIR}/example/case/basic/example.hpp
)

target_sources(skity_benchmarks PRIVATE 
    common/target_skity.cc
    common/target_skity.hpp
    common/target.cc
    common/target.hpp
    common/context.cc
    common/context.hpp
    common/context_skity.cc
    common/context_skity.hpp
)

target_sources(skity_benchmarks PRIVATE 
    common/context_mtl_skity.h
    common/context_mtl_skity.mm
    common/target_mtl_skity.h
    common/target_mtl_skity.mm
    common/context_gl_skity.cc
    common/context_gl_skity.hpp
    common/target_gl_skity.cc
    common/target_gl_skity.hpp
    common/gl_context_mac.h
    common/gl_context_mac.mm
    common/gl_context.hpp
)

target_include_directories(skity_benchmarks
    PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/example
)

target_compile_options(skity_benchmarks PUBLIC -fno-rtti)
target_compile_options(skity_benchmarks PUBLIC -std=c++17)
target_compile_definitions(skity_benchmarks PUBLIC -DDISABLE_SKITY_EXPERIMENTAL_WARNINGS)

target_link_libraries(skity_benchmarks
    PUBLIC
    skity::codec
    skity::skity
    benchmark::benchmark
)

target_link_libraries(
  skity_benchmarks
  PRIVATE "-framework Metal"
          "-framework QuartzCore"
          "-framework Foundation"
          "-framework CoreGraphics"
          "-framework CoreText"
          "-framework Carbon"
          "-framework AppKit"
          "-framework OpenGL")

target_link_libraries(skity_benchmarks PRIVATE glm::glm-header-only)

