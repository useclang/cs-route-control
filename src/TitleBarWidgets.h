#pragma once

#include <QCheckBox>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTableWidgetItem>
#include <functional>

class TitleBarButton : public QPushButton {
  Q_OBJECT
public:
  explicit TitleBarButton(QWidget *parent = nullptr) : QPushButton(parent) {
    setFixedSize(40, 30);
    setFlat(true);
    setCursor(Qt::ArrowCursor);
  }

protected:
  void enterEvent(QEnterEvent *event) override {
    m_hovered = true;
    update();
    QPushButton::enterEvent(event);
  }
  void leaveEvent(QEvent *event) override {
    m_hovered = false;
    m_pressed = false;
    update();
    QPushButton::leaveEvent(event);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      m_pressed = true;
      update();
    }
    QPushButton::mousePressEvent(event);
  }
  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      m_pressed = false;
      update();
    }
    QPushButton::mouseReleaseEvent(event);
  }

  bool m_hovered = false;
  bool m_pressed = false;
};

class MinButton : public TitleBarButton {
  Q_OBJECT
public:
  explicit MinButton(QWidget *parent = nullptr) : TitleBarButton(parent) {}

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_pressed)
      p.fillRect(rect(), QColor("#d0d0d0"));
    else if (m_hovered)
      p.fillRect(rect(), QColor("#e5e5e5"));

    p.setPen(QPen(Qt::black, 1.5, Qt::SolidLine, Qt::RoundCap));
    const int cx = width() / 2, cy = height() / 2, r = 5;
    p.drawLine(cx - r, cy, cx + r, cy);
  }
};

class CloseButton : public TitleBarButton {
  Q_OBJECT
public:
  explicit CloseButton(QWidget *parent = nullptr) : TitleBarButton(parent) {}

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_pressed) {
      p.fillRect(rect(), QColor("#8B0000"));
      p.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap));
    } else if (m_hovered) {
      p.fillRect(rect(), QColor("#B22222"));
      p.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap));
    } else
      p.setPen(QPen(Qt::black, 1.5, Qt::SolidLine, Qt::RoundCap));

    const int cx = width() / 2, cy = height() / 2, r = 5;
    p.drawLine(cx - r, cy - r, cx + r, cy + r);
    p.drawLine(cx - r, cy + r, cx + r, cy - r);
  }
};

class RegionCheckBox : public QCheckBox {
  Q_OBJECT
public:
  explicit RegionCheckBox(QWidget *parent = nullptr) : QCheckBox(parent) {}

  std::function<void()> onExclusiveToggle;

protected:
  void mousePressEvent(QMouseEvent *event) override {
    const bool isMiddle = event->button() == Qt::MiddleButton;
    const bool isAltLeft = event->button() == Qt::LeftButton &&
                           (event->modifiers() & Qt::AltModifier);

    if ((isMiddle || isAltLeft) && onExclusiveToggle) {
      onExclusiveToggle();
      event->accept();
      return;
    }
    QCheckBox::mousePressEvent(event);
  }
};

class PingTableWidgetItem : public QTableWidgetItem {
public:
  using QTableWidgetItem::QTableWidgetItem;

  bool operator<(const QTableWidgetItem &other) const override {
    return data(Qt::UserRole).toInt() < other.data(Qt::UserRole).toInt();
  }
};
