include(AddVersion)

find_package(Qt5Network 5.6.0 REQUIRED)
find_package(Qt5Widgets 5.6.0 REQUIRED)

qt5_wrap_ui(UI_HEADERS
  LoginDialog.ui
  MainWindow.ui
  RoomView.ui
  ChatWindow.ui
  RedactDialog.ui
  JoinDialog.ui
  EventSourceView.ui
  )

add_subdirectory(matrix)

add_version(version.cpp)

add_executable(nachat WIN32
  main.cpp
  LoginDialog.cpp
  MainWindow.cpp
  ChatWindow.cpp
  RoomView.cpp
  TimelineView.cpp
  EntryBox.cpp
  RoomMenu.cpp
  sort.cpp
  RoomViewList.cpp
  Spinner.cpp
  RedactDialog.cpp
  JoinDialog.cpp
  version.cpp
  version_string.cpp
  MessageBox.cpp
  EventSourceView.cpp
  ContentCache.cpp
  JoinedRoomListModel.cpp
  ${UI_HEADERS}
  )

target_link_libraries(nachat
  matrix
  Qt5::Widgets
  Qt5::Network
  )

if(BUILD_DEMOS)
add_executable(spinner-test WIN32
  spinner_test.cpp
  Spinner.cpp
  )

target_link_libraries(spinner-test
  Qt5::Widgets
  )

add_executable(timeline-view-test WIN32
  timeline_view_test.cpp
  TimelineView.cpp
  ContentCache.cpp
  Spinner.cpp
  RedactDialog.cpp
  EventSourceView.cpp
  ${UI_HEADERS}
  )

target_link_libraries(timeline-view-test
  matrix
  Qt5::Widgets
  )
endif(BUILD_DEMOS)

if(WIN32)
  target_link_libraries(nachat Qt5::WinMain)
  target_link_libraries(spinner-test Qt5::WinMain)
  if(BUILD_DEMOS)
    target_link_libraries(timeline-view-test Qt5::WinMain)
  endif(BUILD_DEMOS)
endif(WIN32)

install(
  TARGETS nachat
  DESTINATION bin
  )
