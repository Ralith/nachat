#include <QApplication>
#include <QMessageBox>
#include <QSettings>

#include "Application.hpp"
#include "version.hpp"

int main(int argc, char *argv[]) {
  printf("NaChat %s\n", version::string().toStdString().c_str());

  QCoreApplication::setOrganizationName("nachat");
  QCoreApplication::setApplicationName("nachat");
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

  Application a(argc, argv);

  a.start();

  return a.exec();
}
