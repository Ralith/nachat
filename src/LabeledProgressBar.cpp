#include "LabeledProgressBar.hpp"
#include "ui_LabeledProgressBar.h"

LabeledProgressBar::LabeledProgressBar(QWidget *parent)
    : QWidget(parent), ui(new Ui::LabeledProgressBar) {
  ui->setupUi(this);
}

LabeledProgressBar::~LabeledProgressBar() { delete ui; }

void LabeledProgressBar::set_text(const QString &text) {
  ui->label->setText(text);
}

void LabeledProgressBar::set_minimum(int x) { ui->progress_bar->setMinimum(x); }

void LabeledProgressBar::set_maximum(int x) { ui->progress_bar->setMaximum(x); }

void LabeledProgressBar::set_value(int x) { ui->progress_bar->setValue(x); }
