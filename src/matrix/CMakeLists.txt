include(FindLMDB)
find_package(LMDB REQUIRED)

add_library(matrix
  utils.cpp
  Matrix.cpp
  Session.cpp
  Room.cpp
  proto.cpp
  Content.cpp
  Event.cpp
  TimelineWindow.cpp
  MemberListModel.cpp
  pixmaps.cpp
  )

target_include_directories(matrix
  PUBLIC "${PROJECT_SOURCE_DIR}/deps/lmdbxx" "${LMDB_INCLUDE_DIR}"
  )


target_link_libraries(matrix
  Qt5::Network
  Qt5::Gui
  ${LMDB_LIBRARY}
  )
