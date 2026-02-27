#include "farcal/ui/LuaVmWindow.hpp"

#include "farcal/luavm/LuaVmBase.hpp"
#include "farcal/ui/LuaVmOutputWindow.hpp"
#include "q_lit.hpp"

#include <QAction>
#include <QColor>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidget>

#include <vector>

namespace farcal::ui {
namespace {

struct LuaHighlightRule {
  QRegularExpression pattern;
  QTextCharFormat    format;
};

class LuaSyntaxHighlighter final : public QSyntaxHighlighter {
 public:
  explicit LuaSyntaxHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
    const auto addWordRule = [this](const QStringList& words, const QTextCharFormat& format) {
      if (words.isEmpty()) {
        return;
      }

      QStringList escapedWords;
      escapedWords.reserve(words.size());
      for (const auto& word : words) {
        escapedWords.push_back(QRegularExpression::escape(word));
      }

      m_rules.push_back(
          {QRegularExpression(QString(("\\b(?:%1)\\b")).arg(escapedWords.join(("|")))), format});
    };

    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(("#5EA1FF")));
    keywordFormat.setFontWeight(QFont::Bold);

    const QStringList keywords = {
        ("and"),      ("break"),  ("do"),   ("else"), ("elseif"), ("end"),  ("false"), ("for"),
        ("function"), ("goto"),   ("if"),   ("in"),   ("local"),  ("nil"),  ("not"),   ("or"),
        ("repeat"),   ("return"), ("then"), ("true"), ("until"),  ("while")};
    addWordRule(keywords, keywordFormat);

    QTextCharFormat builtinFormat;
    builtinFormat.setForeground(QColor(("#8BD5CA")));

    const QStringList globals = {
        ("_G"),       ("_VERSION"),     ("arg"),          ("assert"),   ("collectgarbage"),
        ("dofile"),   ("error"),        ("getmetatable"), ("ipairs"),   ("load"),
        ("loadfile"), ("next"),         ("pairs"),        ("pcall"),    ("print"),
        ("rawequal"), ("rawget"),       ("rawlen"),       ("rawset"),   ("require"),
        ("select"),   ("setmetatable"), ("tonumber"),     ("tostring"), ("type"),
        ("warn"),     ("xpcall")};
    addWordRule(globals, builtinFormat);

    const QStringList stdLibFunctions = {
        ("byte"),         ("char"),         ("charpattern"), ("close"),        ("codepoint"),
        ("codes"),        ("clock"),        ("concat"),      ("config"),       ("cos"),
        ("create"),       ("cpath"),        ("date"),        ("debug"),        ("deg"),
        ("difftime"),     ("dump"),         ("execute"),     ("exit"),         ("exp"),
        ("find"),         ("flush"),        ("floor"),       ("fmod"),         ("format"),
        ("getenv"),       ("gmatch"),       ("gsub"),        ("huge"),         ("input"),
        ("insert"),       ("isyieldable"),  ("len"),         ("lines"),        ("loadlib"),
        ("loaded"),       ("log"),          ("lower"),       ("match"),        ("max"),
        ("maxinteger"),   ("min"),          ("mininteger"),  ("modf"),         ("move"),
        ("offset"),       ("open"),         ("output"),      ("pack"),         ("packsize"),
        ("path"),         ("pi"),           ("popen"),       ("preload"),      ("rad"),
        ("random"),       ("randomseed"),   ("read"),        ("remove"),       ("rename"),
        ("rep"),          ("resume"),       ("reverse"),     ("running"),      ("searchers"),
        ("searchpath"),   ("setlocale"),    ("sin"),         ("sort"),         ("sqrt"),
        ("status"),       ("sub"),          ("tan"),         ("time"),         ("tmpfile"),
        ("tmpname"),      ("tointeger"),    ("traceback"),   ("type"),         ("ult"),
        ("unpack"),       ("upper"),        ("wrap"),        ("write"),        ("yield"),
        ("close"),        ("gethook"),      ("getinfo"),     ("getlocal"),     ("getregistry"),
        ("getupvalue"),   ("getuservalue"), ("sethook"),     ("setlocal"),     ("setupvalue"),
        ("setuservalue"), ("upvalueid"),    ("upvaluejoin"), ("getmetatable"), ("setmetatable")};
    addWordRule(stdLibFunctions, builtinFormat);

    QTextCharFormat stdLibTableFormat;
    stdLibTableFormat.setForeground(QColor(("#F2CDCD")));
    const QStringList stdLibTables = {("coroutine"),
                                      ("debug"),
                                      ("io"),
                                      ("math"),
                                      ("os"),
                                      ("package"),
                                      ("string"),
                                      ("table"),
                                      ("utf8")};
    addWordRule(stdLibTables, stdLibTableFormat);

    QTextCharFormat apiFormat;
    apiFormat.setForeground(QColor(("#CBA6F7")));
    m_rules.push_back({QRegularExpression(("\\b(?:glm|memory)\\b")), apiFormat});
    m_rules.push_back(
        {QRegularExpression(
             ("\\b(?:read|read_type|read_typed|module_base|current_pid|read_[A-Za-z0-9_]+)\\b")),
         apiFormat});
    m_rules.push_back(
        {QRegularExpression(("\\b(?:vec[1-4]|dvec[1-4]|ivec[1-4]|uvec[1-4]|bvec[1-4]|"
                             "mat[234](?:x[234])?|dmat[234](?:x[234])?|quat|dquat)\\b")),
         apiFormat});

    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(("#F5A97F")));
    m_rules.push_back(
        {QRegularExpression(("\\b(?:0x[0-9A-Fa-f]+|\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?)\\b")),
         numberFormat});

    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(("#A6E3A1")));
    m_rules.push_back({QRegularExpression(("\"(?:\\\\.|[^\"\\\\])*\"")), stringFormat});
    m_rules.push_back({QRegularExpression(("'(?:\\\\.|[^'\\\\])*'")), stringFormat});

    m_lineCommentFormat.setForeground(QColor(("#6C7086")));
    m_lineComment = QRegularExpression(("--[^\\n]*"));
  }

 protected:
  void highlightBlock(const QString& text) override {
    for (const auto& rule : m_rules) {
      auto matchIterator = rule.pattern.globalMatch(text);
      while (matchIterator.hasNext()) {
        const auto match = matchIterator.next();
        setFormat(match.capturedStart(), match.capturedLength(), rule.format);
      }
    }

    auto lineComments = m_lineComment.globalMatch(text);
    while (lineComments.hasNext()) {
      const auto match = lineComments.next();
      setFormat(match.capturedStart(), match.capturedLength(), m_lineCommentFormat);
    }
  }

 private:
  std::vector<LuaHighlightRule> m_rules;
  QRegularExpression            m_lineComment;
  QTextCharFormat               m_lineCommentFormat;
};

}  // namespace

LuaVmWindow::LuaVmWindow(QWidget* parent) : QMainWindow(parent) {
  m_vm           = std::make_unique<farcal::luavm::BasicLuaVm>();
  m_outputWindow = std::make_unique<LuaVmOutputWindow>(this);
  applyTheme();
  configureWindow();
}

LuaVmWindow::~LuaVmWindow() = default;

void LuaVmWindow::applyTheme() {
  setStyleSheet((R"(QMainWindow {
  background-color: #22242a;
  color: #e8eaed;
}
QFrame#panel {
  background-color: #2b2e36;
  border: 1px solid #4a4e58;
  border-radius: 6px;
}
QLabel {
  color: #e8eaed;
}
QPushButton {
  background-color: #444851;
  border: 1px solid #656a76;
  border-radius: 4px;
  color: #f2f4f7;
  padding: 4px 10px;
}
QPushButton:hover {
  background-color: #525762;
}
QPushButton:pressed {
  background-color: #3a3e47;
}
QPlainTextEdit {
  background-color: #121419;
  color: #e8eaed;
  border: 1px solid #4a4e58;
  selection-background-color: #4e5f82;
})"));
}

void LuaVmWindow::configureWindow() {
  resize(900, 620);
  setWindowTitle(("LuaVM"));
  createMenuBar();
  setCentralWidget(buildCentralArea());
}

void LuaVmWindow::createMenuBar() {
  auto* topMenu     = new QMenuBar(this);
  auto* scriptsMenu = topMenu->addMenu(("Scripts"));
  auto* logsMenu    = topMenu->addMenu(("Logs"));

  auto* loadAction   = scriptsMenu->addAction(("Load"));
  auto* saveAction   = scriptsMenu->addAction(("Save"));
  auto* outputAction = logsMenu->addAction(("LuaVM Output"));

  connect(loadAction, &QAction::triggered, this, &LuaVmWindow::loadLuaScript);
  connect(saveAction, &QAction::triggered, this, &LuaVmWindow::saveLuaScript);
  connect(outputAction, &QAction::triggered, this, &LuaVmWindow::showOutputWindow);

  setMenuBar(topMenu);
}

QWidget* LuaVmWindow::buildCentralArea() {
  auto* root       = new QWidget(this);
  auto* rootLayout = new QVBoxLayout(root);
  rootLayout->setContentsMargins(10, 10, 10, 10);
  rootLayout->setSpacing(8);

  auto* panel = new QFrame(root);
  panel->setObjectName(("panel"));
  auto* panelLayout = new QVBoxLayout(panel);
  panelLayout->setContentsMargins(10, 10, 10, 10);
  panelLayout->setSpacing(8);

  m_editor = new QPlainTextEdit(panel);
  m_editor->setPlaceholderText(("-- Write Lua script here..."));
  m_editor->setTabStopDistance(32.0);
  m_editor->setPlainText(("-- LUAVM script\nprint('hello from LUAVM')\n"));
  new LuaSyntaxHighlighter(m_editor->document());
  panelLayout->addWidget(m_editor, 1);

  auto* controls = new QHBoxLayout();
  controls->setSpacing(8);

  auto* executeButton = new QPushButton(("Execute"), panel);
  auto* clearButton   = new QPushButton(("Clear"), panel);

  controls->addWidget(executeButton);
  controls->addWidget(clearButton);
  controls->addStretch(1);
  panelLayout->addLayout(controls);

  m_statusLabel = new QLabel(("Ready."), panel);
  panelLayout->addWidget(m_statusLabel);

  connect(executeButton, &QPushButton::clicked, this, &LuaVmWindow::executeLuaScript);
  connect(clearButton, &QPushButton::clicked, this, &LuaVmWindow::clearLuaScript);

  rootLayout->addWidget(panel, 1);
  return root;
}

void LuaVmWindow::executeLuaScript() {
  if (m_editor == nullptr || m_statusLabel == nullptr || m_vm == nullptr) {
    return;
  }

  const QString script = m_editor->toPlainText();
  if (script.trimmed().isEmpty()) {
    m_statusLabel->setText(("Nothing to execute."));
    appendLuaOutput(("[LUAVM] Nothing to execute."));
    return;
  }

  appendLuaOutput(("[LUAVM] Execute started."));

  const auto result = m_vm->execute(script.toStdString(), [this](std::string_view line) {
    appendLuaOutput(QString::fromStdString(std::string(line)));
  });

  if (result.success) {
    const int lineCount = script.count('\n') + 1;
    m_statusLabel->setText(QString(("Executed %1 line(s).")).arg(lineCount));
    appendLuaOutput(QString(("[LUAVM] Execute finished successfully (%1 lines).")).arg(lineCount));
  } else {
    const QString message = QString::fromStdString(result.message);
    m_statusLabel->setText(QString(("Lua error: %1")).arg(message));
    appendLuaOutput(QString(("[LUAVM] Error: %1")).arg(message));
  }
}

void LuaVmWindow::clearLuaScript() {
  if (m_editor == nullptr || m_statusLabel == nullptr) {
    return;
  }

  m_editor->clear();
  m_statusLabel->setText(("Editor cleared."));
  appendLuaOutput(("[LUAVM] Script editor cleared."));
}

void LuaVmWindow::loadLuaScript() {
  if (m_editor == nullptr || m_statusLabel == nullptr) {
    return;
  }

  const QString startPath = m_currentFilePath.isEmpty() ? (".") : m_currentFilePath;
  const QString filePath  = QFileDialog::getOpenFileName(
      this, ("Load Lua Script"), startPath, ("Lua Files (*.lua);;All Files (*.*)"));
  if (filePath.isEmpty()) {
    return;
  }

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::warning(
        this, ("Load Lua Script"), QString(("Failed to open file:\n%1")).arg(filePath));
    return;
  }

  const QString contents = QString::fromUtf8(file.readAll());
  m_editor->setPlainText(contents);
  m_currentFilePath = filePath;
  m_statusLabel->setText(QString(("Loaded %1")).arg(QFileInfo(filePath).fileName()));
  appendLuaOutput(QString(("[LUAVM] Loaded script: %1")).arg(filePath));
}

void LuaVmWindow::saveLuaScript() {
  if (m_editor == nullptr || m_statusLabel == nullptr) {
    return;
  }

  const QString defaultPath = m_currentFilePath.isEmpty() ? ("script.lua") : m_currentFilePath;
  const QString filePath    = QFileDialog::getSaveFileName(
      this, ("Save Lua Script"), defaultPath, ("Lua Files (*.lua);;All Files (*.*)"));
  if (filePath.isEmpty()) {
    return;
  }

  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    QMessageBox::warning(
        this, ("Save Lua Script"), QString(("Failed to save file:\n%1")).arg(filePath));
    return;
  }

  const QByteArray encoded      = m_editor->toPlainText().toUtf8();
  const auto       bytesWritten = file.write(encoded);
  if (bytesWritten != encoded.size()) {
    QMessageBox::warning(
        this, ("Save Lua Script"), QString(("Failed to write full file:\n%1")).arg(filePath));
    return;
  }

  m_currentFilePath = filePath;
  m_statusLabel->setText(QString(("Saved %1")).arg(QFileInfo(filePath).fileName()));
  appendLuaOutput(QString(("[LUAVM] Saved script: %1")).arg(filePath));
}

void LuaVmWindow::showOutputWindow() {
  if (m_outputWindow != nullptr) {
    m_outputWindow->show();
    m_outputWindow->raise();
    m_outputWindow->activateWindow();
  }
}

void LuaVmWindow::appendLuaOutput(const QString& line) {
  if (line.isEmpty()) {
    return;
  }

  const QString timestamp = QDateTime::currentDateTime().toString(("HH:mm:ss"));
  const QString formatted = QString(("[%1] %2")).arg(timestamp, line);

  if (m_outputWindow != nullptr) {
    m_outputWindow->appendLine(formatted);
  }
}

}  // namespace farcal::ui
