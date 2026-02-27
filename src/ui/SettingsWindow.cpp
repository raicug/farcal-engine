#include "farcal/ui/SettingsWindow.hpp"
#include "q_lit.hpp"

#include <QAbstractItemView>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace farcal::ui {
namespace {

int keyPartCount(const QKeyCombination combo) {
  int                         partCount = 0;
  const Qt::KeyboardModifiers modifiers = combo.keyboardModifiers();
  if (modifiers.testFlag(Qt::ControlModifier)) {
    ++partCount;
  }
  if (modifiers.testFlag(Qt::ShiftModifier)) {
    ++partCount;
  }
  if (modifiers.testFlag(Qt::AltModifier)) {
    ++partCount;
  }
  if (modifiers.testFlag(Qt::MetaModifier)) {
    ++partCount;
  }
  if (combo.key() != Qt::Key_unknown) {
    ++partCount;
  }
  return partCount;
}

void configureKeybindEdit(QKeySequenceEdit* edit, QObject* owner) {
  if (edit == nullptr || owner == nullptr) {
    return;
  }

  edit->setMaximumSequenceLength(1);
  edit->setProperty("last_valid_sequence", QString());

  QObject::connect(
      edit, &QKeySequenceEdit::keySequenceChanged, owner, [edit](const QKeySequence& sequence) {
        const QSignalBlocker blocker(edit);

        if (sequence.isEmpty()) {
          edit->setProperty("last_valid_sequence", QString());
          return;
        }

        const QKeyCombination first    = sequence[0];
        const int             parts    = keyPartCount(first);
        const QString         previous = edit->property("last_valid_sequence").toString();

        if (parts > 3) {
          if (previous.isEmpty()) {
            edit->setKeySequence(QKeySequence());
          } else {
            edit->setKeySequence(QKeySequence(previous, QKeySequence::PortableText));
          }
          return;
        }

        const QKeySequence clamped(first);
        if (sequence != clamped) {
          edit->setKeySequence(clamped);
        }
        edit->setProperty("last_valid_sequence", clamped.toString(QKeySequence::PortableText));
      });
}

}  // namespace

SettingsWindow::SettingsWindow(QWidget* parent) : QDialog(parent) {
  setWindowTitle(("Farcal Engine Settings"));
  setModal(false);
  setMinimumSize(760, 470);
  resize(820, 520);

  setStyleSheet((R"(
QDialog {
  background-color: #22242a;
  color: #e8eaed;
  font-size: 12px;
}
QFrame#settingsBody {
  background-color: #2b2e36;
  border: 1px solid #4a4e58;
}
QListWidget {
  background-color: #1b1d22;
  color: #c7ccd6;
  border: 1px solid #4a4e58;
  outline: none;
}
QListWidget::item {
  padding: 6px 9px;
}
QListWidget::item:selected {
  background-color: #3d5f94;
  color: #ffffff;
}
QGroupBox {
  border: 1px solid #4f5560;
  margin-top: 10px;
  font-weight: 600;
  background-color: #262932;
}
QGroupBox::title {
  subcontrol-origin: margin;
  left: 8px;
  padding: 0 4px;
}
QLabel {
  color: #e8eaed;
}
QKeySequenceEdit {
  background-color: #171920;
  color: #edf1f9;
  border: 1px solid #4e5668;
  padding: 3px;
  min-height: 24px;
}
QPushButton {
  background-color: #444851;
  border: 1px solid #656a76;
  color: #f2f4f7;
  border-radius: 2px;
  padding: 4px 14px;
  min-height: 24px;
}
QPushButton:hover {
  background-color: #525762;
}
QPushButton:pressed {
  background-color: #3a3e47;
}
QPushButton:default {
  border: 1px solid #6b91cf;
}
QLabel#sectionTitle {
  font-size: 16px;
  font-weight: 700;
}
QLabel#sectionSubTitle {
  color: #aeb6c4;
}
QFrame#buttonSeparator {
  background-color: #4d515c;
  min-height: 1px;
  max-height: 1px;
}
)"));

  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(8, 8, 8, 8);
  rootLayout->setSpacing(8);

  auto* body = new QFrame(this);
  body->setObjectName(("settingsBody"));
  auto* bodyLayout = new QHBoxLayout(body);
  bodyLayout->setContentsMargins(8, 8, 8, 8);
  bodyLayout->setSpacing(8);

  m_categoryList = new QListWidget(body);
  m_categoryList->setFixedWidth(210);
  m_categoryList->addItem(("Hotkeys"));
  m_categoryList->setSelectionMode(QAbstractItemView::SingleSelection);
  bodyLayout->addWidget(m_categoryList);

  m_pages = new QStackedWidget(body);
  m_pages->addWidget(buildKeybindPage());
  bodyLayout->addWidget(m_pages, 1);

  rootLayout->addWidget(body, 1);

  auto* separator = new QFrame(this);
  separator->setObjectName(("buttonSeparator"));
  rootLayout->addWidget(separator);

  auto* buttonsLayout = new QHBoxLayout();
  buttonsLayout->setContentsMargins(0, 0, 0, 0);
  buttonsLayout->addStretch(1);

  auto* defaultsButton = new QPushButton(("Defaults"), this);
  auto* applyButton    = new QPushButton(("Apply"), this);
  auto* cancelButton   = new QPushButton(("Cancel"), this);
  auto* okButton       = new QPushButton(("OK"), this);
  okButton->setDefault(true);
  okButton->setAutoDefault(true);

  buttonsLayout->addWidget(defaultsButton);
  buttonsLayout->addWidget(applyButton);
  buttonsLayout->addWidget(cancelButton);
  buttonsLayout->addWidget(okButton);
  rootLayout->addLayout(buttonsLayout);

  connect(
      m_categoryList, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(defaultsButton, &QPushButton::clicked, this, [this]() {
    setKeybindSettings(KeybindSettings::defaults());
  });
  connect(applyButton, &QPushButton::clicked, this, [this]() {
    emit keybindsSaved(keybindSettings());
  });
  connect(okButton, &QPushButton::clicked, this, [this]() {
    emit keybindsSaved(keybindSettings());
    accept();
  });

  m_categoryList->setCurrentRow(0);
  setKeybindSettings(KeybindSettings::defaults());
}

void SettingsWindow::setKeybindSettings(const KeybindSettings& settings) {
  if (m_structureDissectorKeybind != nullptr) {
    m_structureDissectorKeybind->setKeySequence(settings.openStructureDissector);
  }
  if (m_luaVmKeybind != nullptr) {
    m_luaVmKeybind->setKeySequence(settings.openLuaVm);
  }
  if (m_rttiKeybind != nullptr) {
    m_rttiKeybind->setKeySequence(settings.openRttiScanner);
  }
  if (m_stringScannerKeybind != nullptr) {
    m_stringScannerKeybind->setKeySequence(settings.openStringScanner);
  }
  if (m_attachProcessKeybind != nullptr) {
    m_attachProcessKeybind->setKeySequence(settings.attachToProcess);
  }
  if (m_attachSavedProcessKeybind != nullptr) {
    m_attachSavedProcessKeybind->setKeySequence(settings.attachSavedProcess);
  }
}

KeybindSettings SettingsWindow::keybindSettings() const {
  KeybindSettings settings = KeybindSettings::defaults();

  if (m_structureDissectorKeybind != nullptr) {
    settings.openStructureDissector = m_structureDissectorKeybind->keySequence();
  }
  if (m_luaVmKeybind != nullptr) {
    settings.openLuaVm = m_luaVmKeybind->keySequence();
  }
  if (m_rttiKeybind != nullptr) {
    settings.openRttiScanner = m_rttiKeybind->keySequence();
  }
  if (m_stringScannerKeybind != nullptr) {
    settings.openStringScanner = m_stringScannerKeybind->keySequence();
  }
  if (m_attachProcessKeybind != nullptr) {
    settings.attachToProcess = m_attachProcessKeybind->keySequence();
  }
  if (m_attachSavedProcessKeybind != nullptr) {
    settings.attachSavedProcess = m_attachSavedProcessKeybind->keySequence();
  }

  return settings;
}

QWidget* SettingsWindow::buildKeybindPage() {
  auto* page   = new QWidget(this);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);

  auto* titleLabel = new QLabel(("Hotkeys"), page);
  titleLabel->setObjectName(("sectionTitle"));
  QFont titleFont = titleLabel->font();
  titleFont.setBold(true);
  titleFont.setPointSize(15);
  titleLabel->setFont(titleFont);
  layout->addWidget(titleLabel);

  auto* subtitleLabel =
      new QLabel(("Configure global shortcuts for main pages and attach actions."), page);
  subtitleLabel->setObjectName(("sectionSubTitle"));
  layout->addWidget(subtitleLabel);

  auto* group = new QGroupBox(("Keyboard Shortcuts"), page);
  auto* grid  = new QGridLayout(group);
  grid->setContentsMargins(10, 10, 10, 10);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(8);

  int row = 0;

  grid->addWidget(new QLabel(("Structure Dissector"), group), row, 0);
  m_structureDissectorKeybind = new QKeySequenceEdit(group);
  configureKeybindEdit(m_structureDissectorKeybind, this);
  grid->addWidget(m_structureDissectorKeybind, row++, 1);

  grid->addWidget(new QLabel(("LuaVM"), group), row, 0);
  m_luaVmKeybind = new QKeySequenceEdit(group);
  configureKeybindEdit(m_luaVmKeybind, this);
  grid->addWidget(m_luaVmKeybind, row++, 1);

  grid->addWidget(new QLabel(("RTTI Scanner"), group), row, 0);
  m_rttiKeybind = new QKeySequenceEdit(group);
  configureKeybindEdit(m_rttiKeybind, this);
  grid->addWidget(m_rttiKeybind, row++, 1);

  grid->addWidget(new QLabel(("String Scanner"), group), row, 0);
  m_stringScannerKeybind = new QKeySequenceEdit(group);
  configureKeybindEdit(m_stringScannerKeybind, this);
  grid->addWidget(m_stringScannerKeybind, row++, 1);

  grid->addWidget(new QLabel(("Attach To Process"), group), row, 0);
  m_attachProcessKeybind = new QKeySequenceEdit(group);
  configureKeybindEdit(m_attachProcessKeybind, this);
  grid->addWidget(m_attachProcessKeybind, row++, 1);

  grid->addWidget(new QLabel(("Attach Last Process"), group), row, 0);
  m_attachSavedProcessKeybind = new QKeySequenceEdit(group);
  configureKeybindEdit(m_attachSavedProcessKeybind, this);
  grid->addWidget(m_attachSavedProcessKeybind, row++, 1);

  grid->setColumnStretch(0, 0);
  grid->setColumnStretch(1, 1);

  layout->addWidget(group);
  layout->addStretch(1);
  return page;
}

}  // namespace farcal::ui
