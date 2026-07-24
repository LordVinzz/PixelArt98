#include "ui/QtMainWindow.hpp"

#include "io/ProjectIO.hpp"
#include "core/DocumentTransforms.hpp"
#include "depth/DepthMapExtractor.hpp"
#include "ui/AdjustmentWidgets.hpp"
#include "ui/AnimationExport.hpp"
#include "ui/GraphEffectWidget.hpp"
#include "ui/QtCanvasWidget.hpp"
#include "ui/QtModelPreviewWidget.hpp"
#include "ui/TextRasterizer.hpp"
#include "PixelArtVersion.hpp"

#include <QAction>
#include <QActionGroup>
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDataStream>
#include <QDateTime>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QEventLoop>
#include <QFormLayout>
#include <QFontComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QOpenGLContext>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QProgressDialog>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QToolBar>
#include <QToolButton>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QVersionNumber>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <optional>
#include <thread>

namespace px {

namespace {

constexpr int kWindowStateVersion = 1;
constexpr auto kSelectionMimeType = "application/x-pixelart98-selection";
constexpr quint32 kSelectionClipboardMagic = 0x50583938U;
constexpr quint32 kSelectionClipboardVersion = 1U;

QByteArray bundled_default_window_state() {
    // Captured from the maintainer's reference workspace. Geometry is deliberately
    // excluded so the main window still adapts to each user's screen.
    return QByteArray::fromBase64(QByteArrayLiteral(
        "AAAA/wAAAAH9AAAABAAAAAAAAADoAAADYvwCAAAAA/sAAAASAFQAbwBvAGwAcwBEAG8A"
        "YwBrAQAAABgAAANiAAABhgD////8AAAAMgAAAisAAAAAAP////wBAAAAAvsAAAAQAFQA"
        "ZQB4AHQARABvAGMAawAAAAAAAAABMwAAAUYA////+wAAAB4AQQBkAGoAdQBzAHQAbQBl"
        "AG4AdABzAEQAbwBjAGsAAAAAYQAAARkAAAAAAAAAAPsAAAASAE0AbwBkAGUAbABEAG8A"
        "YwBrAgAAAAIAAAG0AAABXQAAAWwAAAABAAABXQAAA2L8AgAAAAP7AAAAFABDAG8AbABv"
        "AHIAcwBEAG8AYwBrAQAAABgAAAH2AAABigD////8AAACDwAAAWsAAAFGAP////wBAAAA"
        "AvsAAAAUAEwAYQB5AGUAcgBzAEQAbwBjAGsBAAAEiwAAALYAAAC1AP////sAAAAWAEgA"
        "aQBzAHQAbwByAHkARABvAGMAawEAAAVCAAAApgAAAJYA////+wAAACAATQBvAGQAZQBs"
        "AFAAcgBlAHYAaQBlAHcARABvAGMAawgAAAAYAAACiwAAAQYA////AAAAAgAABegAAACz"
        "/AEAAAAB+wAAABoAQQBuAGkAbQBhAHQAaQBvAG4ARABvAGMAawgAAAAAAAAF6AAAAQ4A"
        "////AAAAAwAABegAAADW/AEAAAAC+wAAAB4AVABpAGwAZQBQAHIAZQB2AGkAZQB3AEQA"
        "bwBjAGsIAAAAAAAABegAAADAAP////sAAAAgAEUAcgByAG8AcgBDAG8AbgBzAG8AbABl"
        "AEQAbwBjAGsAAAAAAP////8AAABNAP///wAAA6EAAANiAAAABAAAAAQAAAAIAAAACPwA"
        "AAABAAAAAgAAAAEAAAAaAEMAYQBuAHYAYQBzAFQAbwBvAGwAYgBhAHIBAAAAAP////8A"
        "AAAAAAAAAA=="));
}

struct ClipboardSelection {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::vector<Pixel> pixels;
    std::vector<std::uint8_t> mask;
};

QByteArray encode_clipboard_selection(const ClipboardSelection& selection) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << kSelectionClipboardMagic << kSelectionClipboardVersion
           << static_cast<qint32>(selection.x) << static_cast<qint32>(selection.y)
           << static_cast<qint32>(selection.width) << static_cast<qint32>(selection.height);
    for (Pixel pixel : selection.pixels) stream << static_cast<quint32>(pixel);
    for (std::uint8_t selected : selection.mask) stream << static_cast<quint8>(selected);
    return bytes;
}

std::optional<ClipboardSelection> decode_clipboard_selection(const QByteArray& bytes) {
    QDataStream stream(bytes);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0;
    quint32 version = 0;
    qint32 x = 0;
    qint32 y = 0;
    qint32 width = 0;
    qint32 height = 0;
    stream >> magic >> version >> x >> y >> width >> height;
    const qint64 pixel_count = static_cast<qint64>(width) * static_cast<qint64>(height);
    if (stream.status() != QDataStream::Ok || magic != kSelectionClipboardMagic ||
        version != kSelectionClipboardVersion || width <= 0 || height <= 0 ||
        pixel_count <= 0 || pixel_count > 268435456LL) {
        return std::nullopt;
    }
    ClipboardSelection selection;
    selection.x = x;
    selection.y = y;
    selection.width = width;
    selection.height = height;
    selection.pixels.resize(static_cast<std::size_t>(pixel_count));
    selection.mask.resize(static_cast<std::size_t>(pixel_count));
    for (Pixel& pixel : selection.pixels) {
        quint32 encoded = 0;
        stream >> encoded;
        pixel = static_cast<Pixel>(encoded);
    }
    for (std::uint8_t& selected : selection.mask) {
        quint8 encoded = 0;
        stream >> encoded;
        selected = encoded != 0 ? 1 : 0;
    }
    if (stream.status() != QDataStream::Ok) return std::nullopt;
    return selection;
}

QString image_filter() { return QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif);;All files (*)"); }
QString project_filter() { return QStringLiteral("PixelArt98 Projects (*.pixart);;All files (*)"); }

QString qstring_from_filesystem_path(const std::filesystem::path& path) {
#if defined(_WIN32)
    return QString::fromStdWString(path.wstring());
#else
    const std::u8string text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()),
                             static_cast<qsizetype>(text.size()));
#endif
}

QVersionNumber semantic_version(QString version) {
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) version.remove(0, 1);
    return QVersionNumber::fromString(version);
}

QColor qcolor(Pixel pixel) { return QColor(r(pixel), g(pixel), b(pixel), a(pixel)); }
Pixel pixel_color(const QColor& color) { return rgba(static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()), static_cast<std::uint8_t>(color.blue()), static_cast<std::uint8_t>(color.alpha())); }
float percentage(int value) { return static_cast<float>(value) / 100.0f; }

std::filesystem::path filesystem_path(const QString& path) {
#if defined(_WIN32)
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

QWidget* vertical_panel(std::initializer_list<QWidget*> widgets) {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    for (QWidget* widget : widgets) layout->addWidget(widget);
    layout->addStretch(1);
    return panel;
}

QString tool_icon_path(ToolType tool) {
    const char* file = "";
    switch (tool) {
        case ToolType::Pencil: file = "pencil.png"; break;
        case ToolType::Brush: file = "brush.png"; break;
        case ToolType::Eraser: file = "eraser.png"; break;
        case ToolType::Line: file = "line.png"; break;
        case ToolType::Rectangle: file = "rectangle.png"; break;
        case ToolType::Ellipse: file = "ellipse.png"; break;
        case ToolType::Bucket: file = "bucket.png"; break;
        case ToolType::Gradient: file = "gradient.png"; break;
        case ToolType::Eyedropper: file = "eyedropper.png"; break;
        case ToolType::CloneStamp: file = "clone_stamp.png"; break;
        case ToolType::RectSelect: file = "rectangle_select.png"; break;
        case ToolType::EllipseSelect: file = "ellipse_select.png"; break;
        case ToolType::LassoSelect: file = "lasso.png"; break;
        case ToolType::MagicWand: file = "magic_wand.png"; break;
        case ToolType::MovePixels: file = "move_pixels.png"; break;
        case ToolType::Text: file = "text.png"; break;
    }
    return QStringLiteral(":/icons/") + QString::fromLatin1(file);
}

QIcon tool_icon(ToolType tool) {
    return QIcon(tool_icon_path(tool));
}

QIcon icon_resource(const QString& file_name) {
    return QIcon(QStringLiteral(":/icons/") + file_name);
}

QIcon rotated_icon_resource(const QString& file_name, qreal degrees) {
    QPixmap source(QStringLiteral(":/icons/") + file_name);
    return QIcon(source.transformed(QTransform().rotate(degrees), Qt::SmoothTransformation));
}

QToolButton* icon_button(const QIcon& icon, const QString& label) {
    auto* button = new QToolButton;
    button->setIcon(icon);
    button->setIconSize(QSize(22, 22));
    button->setFixedSize(QSize(30, 30));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setToolTip(label);
    button->setStatusTip(label);
    button->setAccessibleName(label);
    return button;
}

void clear_layout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        if (QLayout* child = item->layout()) clear_layout(child);
        delete item;
    }
}

void draw_alpha_checker(QPainter& painter, const QRect& rect, int cell = 6) {
    for (int y = rect.top(); y < rect.bottom(); y += cell) {
        for (int x = rect.left(); x < rect.right(); x += cell) {
            const bool dark = (((x - rect.left()) / cell) + ((y - rect.top()) / cell)) % 2 == 0;
            painter.fillRect(QRect(x, y, cell, cell), dark ? QColor(178, 178, 178) : QColor(220, 220, 220));
        }
    }
}

QString rgba_hex(const QColor& color) {
    return QStringLiteral("#%1%2%3%4")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .arg(color.alpha(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

std::optional<QColor> parse_rgba_hex(QString text) {
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('#'))) text.remove(0, 1);
    if (text.size() != 6 && text.size() != 8) return std::nullopt;
    bool ok = false;
    const uint value = text.toUInt(&ok, 16);
    if (!ok) return std::nullopt;
    if (text.size() == 6) {
        return QColor(static_cast<int>((value >> 16) & 0xffU),
                      static_cast<int>((value >> 8) & 0xffU),
                      static_cast<int>(value & 0xffU), 255);
    }
    return QColor(static_cast<int>((value >> 24) & 0xffU),
                  static_cast<int>((value >> 16) & 0xffU),
                  static_cast<int>((value >> 8) & 0xffU),
                  static_cast<int>(value & 0xffU));
}

} // namespace

class ColorChip final : public QWidget {
public:
    explicit ColorChip(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(48, 28);
        setCursor(Qt::PointingHandCursor);
    }

    void set_color(QColor color) { color_ = color; update(); }
    void set_selected(bool selected) { selected_ = selected; update(); }
    std::function<void()> clicked;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRect chip = rect().adjusted(1, 1, -1, -1);
        draw_alpha_checker(painter, chip);
        painter.fillRect(chip, color_);
        painter.setPen(QPen(selected_ ? QColor(30, 144, 255) : QColor(70, 74, 82), selected_ ? 2 : 1));
        painter.drawRect(chip.adjusted(0, 0, -1, -1));
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && clicked) clicked();
    }

private:
    QColor color_ = Qt::black;
    bool selected_ = false;
};

class ColorSquare final : public QWidget {
public:
    explicit ColorSquare(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(140, 96);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setCursor(Qt::CrossCursor);
    }

    void set_hsv(int hue, int saturation, int value) {
        hue_ = std::clamp(hue, 0, 359);
        saturation_ = std::clamp(saturation, 0, 255);
        value_ = std::clamp(value, 0, 255);
        update();
    }

    std::function<void(int, int)> changed;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QImage image(size(), QImage::Format_RGB32);
        const int w = std::max(1, width() - 1);
        const int h = std::max(1, height() - 1);
        for (int y = 0; y < height(); ++y) {
            const int value = std::clamp(255 - (y * 255 / h), 0, 255);
            for (int x = 0; x < width(); ++x) {
                const int saturation = std::clamp(x * 255 / w, 0, 255);
                image.setPixelColor(x, y, QColor::fromHsv(hue_, saturation, value));
            }
        }
        painter.drawImage(rect(), image);
        const QPoint cursor(saturation_ * w / 255, (255 - value_) * h / 255);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(Qt::white, 2));
        painter.drawEllipse(cursor, 5, 5);
        painter.setPen(QPen(Qt::black, 1));
        painter.drawEllipse(cursor, 6, 6);
    }

    void mousePressEvent(QMouseEvent* event) override { update_from_position(event->position()); }
    void mouseMoveEvent(QMouseEvent* event) override { update_from_position(event->position()); }

private:
    void update_from_position(const QPointF& position) {
        const int w = std::max(1, width() - 1);
        const int h = std::max(1, height() - 1);
        saturation_ = std::clamp(static_cast<int>(std::round(position.x() * 255.0 / static_cast<double>(w))), 0, 255);
        value_ = std::clamp(255 - static_cast<int>(std::round(position.y() * 255.0 / static_cast<double>(h))), 0, 255);
        if (changed) changed(saturation_, value_);
        update();
    }

    int hue_ = 0;
    int saturation_ = 0;
    int value_ = 0;
};

constexpr int kFrameDurationRole = Qt::UserRole + 1;
constexpr int kFrameHandleWidth = 9;
constexpr int kFrameBaseWidth = 42;
constexpr int kFrameMaximumWidth = 600;
constexpr int kFrameItemHeight = 54;
constexpr int kFrameMillisecondsPerPixel = 4;
constexpr int kFrameDurationSnapMs = 10;

int frame_item_width(int duration_ms) {
    const int duration = std::clamp(duration_ms, kMinimumFrameDurationMs, kMaximumFrameDurationMs);
    return std::clamp(kFrameBaseWidth + duration / kFrameMillisecondsPerPixel,
                      kFrameBaseWidth + kMinimumFrameDurationMs / kFrameMillisecondsPerPixel,
                      kFrameMaximumWidth);
}

QString frame_item_label(int frame_index, int duration_ms) {
    return QObject::tr("Frame %1\n%2 ms").arg(frame_index + 1).arg(duration_ms);
}

void configure_frame_item(QListWidgetItem& item, int frame_index, int duration_ms) {
    const int duration = std::clamp(duration_ms, kMinimumFrameDurationMs, kMaximumFrameDurationMs);
    item.setText(frame_item_label(frame_index, duration));
    item.setData(kFrameDurationRole, duration);
    item.setSizeHint(QSize(frame_item_width(duration), kFrameItemHeight));
    item.setToolTip(QObject::tr("Drag the right edge to change this frame's duration"));
    item.setTextAlignment(Qt::AlignCenter);
}

class FrameTimelineDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        painter->save();
        QRect handle = option.rect;
        handle.setLeft(handle.right() - kFrameHandleWidth + 1);
        QColor handle_color = option.palette.mid().color();
        if (option.state.testFlag(QStyle::State_Selected)) {
            handle_color = option.palette.highlightedText().color();
        }
        handle_color.setAlpha(150);
        painter->fillRect(handle, handle_color);
        painter->setPen(QPen(option.palette.base().color(), 1.0));
        const int center = handle.center().x();
        painter->drawLine(center - 2, handle.top() + 10, center - 2, handle.bottom() - 10);
        painter->drawLine(center + 1, handle.top() + 10, center + 1, handle.bottom() - 10);
        painter->restore();
    }
};

class FrameTimelineWidget final : public QListWidget {
public:
    explicit FrameTimelineWidget(QWidget* parent = nullptr) : QListWidget(parent) {
        setFlow(QListView::LeftToRight);
        setWrapping(false);
        setResizeMode(QListView::Adjust);
        setMovement(QListView::Static);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSpacing(2);
        setMouseTracking(true);
        setMinimumHeight(kFrameItemHeight + 12);
        setItemDelegate(new FrameTimelineDelegate(this));
        setAccessibleDescription(tr("Timeline. Drag the right edge of a frame to stretch or shorten it."));
    }

    std::function<void(int, int)> duration_committed;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const QPoint position = event->position().toPoint();
            if (QListWidgetItem* item = itemAt(position); item != nullptr && on_resize_handle(item, position)) {
                resize_row_ = row(item);
                resize_start_x_ = position.x();
                resize_start_duration_ms_ = item->data(kFrameDurationRole).toInt();
                resize_duration_ms_ = resize_start_duration_ms_;
                setCurrentRow(resize_row_);
                setCursor(Qt::SplitHCursor);
                setProperty("resizingFrame", resize_row_);
                setProperty("previewDurationMs", resize_duration_ms_);
                event->accept();
                return;
            }
        }
        QListWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const QPoint position = event->position().toPoint();
        if (resize_row_ >= 0) {
            const double raw_duration = static_cast<double>(resize_start_duration_ms_) +
                                        static_cast<double>(position.x() - resize_start_x_) *
                                            static_cast<double>(kFrameMillisecondsPerPixel);
            const int bounded = static_cast<int>(std::lround(std::clamp(
                raw_duration,
                static_cast<double>(kMinimumFrameDurationMs),
                static_cast<double>(kMaximumFrameDurationMs))));
            resize_duration_ms_ = std::clamp(
                ((bounded + kFrameDurationSnapMs / 2) / kFrameDurationSnapMs) * kFrameDurationSnapMs,
                kMinimumFrameDurationMs,
                kMaximumFrameDurationMs);
            if (QListWidgetItem* item = this->item(resize_row_); item != nullptr) {
                configure_frame_item(*item, resize_row_, resize_duration_ms_);
            }
            setProperty("previewDurationMs", resize_duration_ms_);
            viewport()->update();
            event->accept();
            return;
        }

        QListWidgetItem* hovered = itemAt(position);
        setCursor(hovered != nullptr && on_resize_handle(hovered, position) ? Qt::SplitHCursor : Qt::ArrowCursor);
        QListWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (resize_row_ >= 0 && event->button() == Qt::LeftButton) {
            const int committed_row = resize_row_;
            const int committed_duration = resize_duration_ms_;
            const bool changed = committed_duration != resize_start_duration_ms_;
            resize_row_ = -1;
            setProperty("resizingFrame", -1);
            setProperty("lastCommittedDurationMs", committed_duration);
            setCursor(Qt::ArrowCursor);
            event->accept();
            if (changed && duration_committed) duration_committed(committed_row, committed_duration);
            return;
        }
        QListWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        if (resize_row_ < 0) setCursor(Qt::ArrowCursor);
        QListWidget::leaveEvent(event);
    }

private:
    [[nodiscard]] bool on_resize_handle(const QListWidgetItem* item, const QPoint& position) const {
        const QRect item_rect = visualItemRect(item);
        return item_rect.contains(position) && position.x() >= item_rect.right() - kFrameHandleWidth + 1;
    }

    int resize_row_ = -1;
    int resize_start_x_ = 0;
    int resize_start_duration_ms_ = 0;
    int resize_duration_ms_ = 0;
};

class PaletteList final : public QListWidget {
public:
    explicit PaletteList(QWidget* parent = nullptr) : QListWidget(parent) {}
    std::function<void(Pixel, bool)> color_chosen;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (QListWidgetItem* item = itemAt(event->position().toPoint()); item != nullptr) {
            const Pixel color = static_cast<Pixel>(item->data(Qt::UserRole).toULongLong());
            const bool secondary = event->button() == Qt::RightButton;
            if (color_chosen) color_chosen(color, secondary);
            if (secondary) {
                event->accept();
                return;
            }
        }
        QListWidget::mousePressEvent(event);
    }
};
