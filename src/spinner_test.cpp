#include <QApplication>
#include <QMessageBox>
#include <QSettings>

#include "Spinner.hpp"

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);

  Spinner spinner;
  spinner.show();

  return a.exec();
}
