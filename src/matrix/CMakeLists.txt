add_library(matrix
  utils.cpp
  Matrix.cpp
  Session.cpp
  Room.cpp
  Member.cpp
  proto.cpp
  parse.cpp
  Content.cpp
  )

target_include_directories(matrix
  PUBLIC "${PROJECT_SOURCE_DIR}/deps/lmdbxx"
  )


target_link_libraries(matrix
  Qt5::Network
  lmdb
  )
