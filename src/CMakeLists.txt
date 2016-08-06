find_package(Qt5Network 5.6.0 REQUIRED)
find_package(Qt5Widgets 5.6.0 REQUIRED)

qt5_wrap_ui(UI_HEADERS
  LoginDialog.ui
  MainWindow.ui
  RoomView.ui
  ChatWindow.ui
  RedactDialog.ui
  JoinDialog.ui
  )

add_subdirectory(matrix)

add_executable(nachat WIN32
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
  EventView.cpp
  JoinDialog.cpp
  ${UI_HEADERS}
  )

target_link_libraries(nachat
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

if(WIN32)
  target_link_libraries(nachat Qt5::WinMain)
  target_link_libraries(spinner-test Qt5::WinMain)
endif(WIN32)

install(
  TARGETS nachat
  DESTINATION bin
  )
