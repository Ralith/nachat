#include <QApplication>
#include <QMessageBox>
#include <QSettings>

#include "Application.hpp"

int main(int argc, char *argv[]) {
  QCoreApplication::setOrganizationName("nachat");
  QCoreApplication::setApplicationName("nachat");

  Application a(argc, argv);

  a.start();

  return a.exec();
}
