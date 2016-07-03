#include <QApplication>
#include <QMessageBox>
#include <QSettings>

#include "application.h"

int main(int argc, char *argv[]) {
  QCoreApplication::setOrganizationName("Native Chat");
  QCoreApplication::setApplicationName("Native Chat");

  Application a(argc, argv);

  a.start();

  return a.exec();
}
