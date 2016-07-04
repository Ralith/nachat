find_package(Qt5Network)

add_library(matrix
  matrix.cpp
  Room.cpp
  Member.cpp
  proto.cpp
  parse.cpp)

target_link_libraries(matrix
  Qt5::Network)
