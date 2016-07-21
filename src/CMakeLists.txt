find_package(Qt5Network)
find_package(Qt5Widgets)

qt5_wrap_ui(UI_HEADERS
  LoginDialog.ui
  MainWindow.ui
  RoomView.ui
  LabeledProgressBar.ui
  ChatWindow.ui
  )

add_subdirectory(matrix)

add_executable(native-chat WIN32
  main.cpp
  Application.cpp
  LoginDialog.cpp
  MainWindow.cpp
  ChatWindow.cpp
  RoomView.cpp
  LabeledProgressBar.cpp
  TimelineView.cpp
  EntryBox.cpp
  RoomMenu.cpp
  sort.cpp
  ${UI_HEADERS}
  )

target_link_libraries(native-chat
  matrix
  Qt5::Widgets
  Qt5::Network
  )

install(
  TARGETS native-chat
  DESTINATION bin
  )
