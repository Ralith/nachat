find_package(Qt5Network)
find_package(Qt5Widgets)

qt5_wrap_ui(UI_HEADERS
  logindialog.ui
  mainwindow.ui
  chatwindow.ui
  roomview.ui
  LabeledProgressBar.ui
  )

add_subdirectory(matrix)

add_executable(native-chat WIN32
  main.cpp
  application.cpp
  logindialog.cpp
  mainwindow.cpp
  chatwindow.cpp
  roomview.cpp
  LabeledProgressBar.cpp
  ${UI_HEADERS})

target_link_libraries(native-chat matrix Qt5::Widgets Qt5::Network)
