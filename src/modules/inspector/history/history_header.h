#pragma once

#include <QHeaderView>

class HistoryHeader : public QHeaderView {
 public:
  HistoryHeader(Qt::Orientation orientation, QWidget* parent = nullptr);
  QSize sectionSizeFromContents(int logicalIndex) const override;
  void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const;
};
