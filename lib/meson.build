lib_includes = include_directories('../include')

lib_sources = files(
    'environment.c',
    'logging.c',
)

lib_target = static_library('libkatana', lib_sources,
  include_directories: lib_includes)

libkatana = declare_dependency(
  link_with: lib_target,
  include_directories: lib_includes)
