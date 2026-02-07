#pragma once

#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

class DBCHighlighter : public QSyntaxHighlighter {
public:
  DBCHighlighter(QTextDocument *parent = nullptr) : QSyntaxHighlighter(parent) {
    setupRules();
  }

protected:
  void highlightBlock(const QString &text) override {
    for (const HighlightingRule &rule : std::as_const(highlightingRules)) {
      QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
      while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), rule.format);
      }
    }
  }

private:
  struct HighlightingRule {
    QRegularExpression pattern;
    QTextCharFormat format;
  };
  QVector<HighlightingRule> highlightingRules;

  void setupRules() {
    HighlightingRule rule;

    // 1. Keywords (BO_, SG_, CM_) - Bold Blue
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor("#2980b9"));
    keywordFormat.setFontWeight(QFont::Bold);
    QStringList keywords = {"BO_", "SG_", "CM_", "VAL_", "BA_", "BA_DEF_"};
    for (const QString &kw : keywords) {
      rule.pattern = QRegularExpression("\\b" + kw + "\\b");
      rule.format = keywordFormat;
      highlightingRules.append(rule);
    }

    // 2. Bit Layout (e.g., 8|16@1+) - Dark Orange
    QTextCharFormat layoutFormat;
    layoutFormat.setForeground(QColor("#d35400"));
    rule.pattern = QRegularExpression(R"(\d+\|\d+@[01][+-])");
    rule.format = layoutFormat;
    highlightingRules.append(rule);

    // 3. Scale/Offset/Min/Max (e.g., (0.1,0) [0|100]) - Green
    QTextCharFormat mathFormat;
    mathFormat.setForeground(QColor("#27ae60"));
    rule.pattern = QRegularExpression(R"(\([\d\.-]+,[\d\.-]+\)|\[[\d\.-]+\|[\d\.-]+\])");
    rule.format = mathFormat;
    highlightingRules.append(rule);

    // 4. Units and Comments - Grey/Italic
    QTextCharFormat commentFormat;
    commentFormat.setForeground(Qt::gray);
    commentFormat.setFontItalic(true);
    rule.pattern = QRegularExpression(R"("[^"]*")");
    rule.format = commentFormat;
    highlightingRules.append(rule);
  }
};

class DBCEditor : public QWidget {
  Q_OBJECT
 public:
  explicit DBCEditor(QWidget* parent = nullptr);
  void setSignalText(const QString& text);
  QString getSignalText() const;

 signals:
  void signalTextChanged(const QString& new_text);

 private slots:
  void onTextChanged();

 private:
  QPlainTextEdit* text_edit;
};
