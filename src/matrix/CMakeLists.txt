add_library(matrix
  utils.cpp
  matrix.cpp
  Session.cpp
  Room.cpp
  Member.cpp
  proto.cpp
  parse.cpp
  Content.cpp
  )

target_link_libraries(matrix
  Qt5::Network
  )
