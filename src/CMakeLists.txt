find_package(Qt5Network)
find_package(Qt5Widgets)

qt5_wrap_ui(UI_HEADERS
  LoginDialog.ui
  MainWindow.ui
  RoomView.ui
  ChatWindow.ui
  RedactDialog.ui
  )

add_subdirectory(matrix)

add_executable(native-chat WIN32
  main.cpp
  Application.cpp
  LoginDialog.cpp
  MainWindow.cpp
  ChatWindow.cpp
  RoomView.cpp
  TimelineView.cpp
  EntryBox.cpp
  RoomMenu.cpp
  sort.cpp
  RoomViewList.cpp
  MemberList.cpp
  Spinner.cpp
  RedactDialog.cpp
  ${UI_HEADERS}
  )

target_link_libraries(native-chat
  matrix
  Qt5::Widgets
  Qt5::Network
  )

add_executable(spinner-test WIN32
  spinner_test.cpp
  Spinner.cpp
  )

target_link_libraries(spinner-test
  Qt5::Widgets
  )

install(
  TARGETS native-chat
  DESTINATION bin
  )
