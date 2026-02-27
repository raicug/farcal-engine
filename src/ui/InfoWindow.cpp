#include "farcal/ui/InfoWindow.hpp"
#include "q_lit.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace farcal::ui {

InfoWindow::InfoWindow(QWidget* parent) : QMainWindow(parent) {
  applyTheme();
  configureWindow();
}

InfoWindow::~InfoWindow() = default;

void InfoWindow::applyTheme() {
  setStyleSheet((R"(QMainWindow {
  background-color: #1f1f1f;
  color: #f0f0f0;
}
QFrame#surface {
  background-color: #1f1f1f;
  border: 1px solid #6a6a6a;
}
QLabel#title {
  color: #f8f8f8;
  font-size: 15px;
  font-weight: 600;
}
QLabel#madeBy {
  color: #f2f2f2;
  font-size: 11px;
  font-weight: 500;
}
QLabel#author {
  color: #f2f2f2;
  font-size: 11px;
  font-weight: 600;
}
QLabel#link {
  color: #f2f2f2;
  font-size: 11px;
}
QLabel#link a {
  color: #50d6f4;
  text-decoration: underline;
}
QLabel#line {
  color: #ffffff;
  font-size: 11px;
}
QLabel#section {
  color: #ffffff;
  font-size: 11px;
  font-weight: 600;
}
QLabel#thanks {
  color: #ffffff;
  font-size: 11px;
})"));
}

void InfoWindow::configureWindow() {
  setWindowTitle(("About Farcal Engine"));
  setCentralWidget(buildCentralArea());
  const QSize minSize = minimumSizeHint();
  setMinimumSize(minSize);
  resize(minSize);
}

QWidget* InfoWindow::buildCentralArea() {
  auto* root       = new QWidget(this);
  auto* rootLayout = new QVBoxLayout(root);
  rootLayout->setContentsMargins(8, 8, 8, 8);

  auto* surface = new QFrame(root);
  surface->setObjectName(("surface"));

  auto* layout = new QVBoxLayout(surface);
  layout->setContentsMargins(14, 10, 14, 10);
  layout->setSpacing(7);

  auto* topRow = new QHBoxLayout();
  auto* title  = new QLabel(("Farcal Engine 2.0"), surface);
  title->setObjectName(("title"));
  topRow->addWidget(title);
  topRow->addStretch();

  auto* madeByColumn = new QVBoxLayout();
  madeByColumn->setSpacing(2);

  auto* madeBy = new QLabel(("Made by:"), surface);
  madeBy->setObjectName(("madeBy"));
  madeByColumn->addWidget(madeBy, 0, Qt::AlignRight);

  auto* author = new QLabel(("aperitif"), surface);
  author->setObjectName(("author"));
  madeByColumn->addWidget(author, 0, Qt::AlignRight);
  topRow->addLayout(madeByColumn);
  layout->addLayout(topRow);

  auto* website = new QLabel(("<a href=\"https://farcal.com\">Website</a>"), surface);
  website->setObjectName(("link"));
  website->setTextFormat(Qt::RichText);
  website->setTextInteractionFlags(Qt::TextBrowserInteraction);
  website->setOpenExternalLinks(true);
  layout->addWidget(website, 0, Qt::AlignLeft);

  auto* powered = new QLabel(("Memory tooling powered by C++ and Qt"), surface);
  powered->setObjectName(("line"));
  layout->addWidget(powered);

  auto* specialThanksTitle = new QLabel(("Special thanks to:"), surface);
  specialThanksTitle->setObjectName(("section"));
  layout->addWidget(specialThanksTitle);

  auto* thanksNames = new QLabel(("jonah (skidded his RTTI demangler), trinyxt (faggot)"), surface);
  thanksNames->setObjectName(("thanks"));
  layout->addWidget(thanksNames);

  layout->addStretch();

  rootLayout->addWidget(surface, 1);
  return root;
}

}  // namespace farcal::ui
