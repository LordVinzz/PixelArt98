QPoint QtCanvasWidget::pixel_at(const QPointF& position) const {
    const QRectF target = image_rect();
    return {static_cast<int>(std::floor((position.x() - target.left()) / zoom_)),
            static_cast<int>(std::floor((position.y() - target.top()) / zoom_))};
}

bool QtCanvasWidget::valid_pixel(const QPoint& pixel) const {
    return controller_.document().in_bounds(pixel.x(), pixel.y());
}

std::optional<QRect> QtCanvasWidget::selection_transform_bounds() const {
    const FloatingSelection& floating = controller_.document().floating_selection;
    if (floating.active && floating.width > 0 && floating.height > 0) {
        return QRect(floating.source_x + floating.offset_x,
                     floating.source_y + floating.offset_y,
                     floating.width, floating.height);
    }
    if (const auto bounds = controller_.document().selection.bounds(); bounds.has_value()) {
        return QRect((*bounds)[0], (*bounds)[1], (*bounds)[2] - (*bounds)[0] + 1,
                     (*bounds)[3] - (*bounds)[1] + 1);
    }
    return std::nullopt;
}

SelectionTransformHandle QtCanvasWidget::selection_transform_handle_at(const QPointF& position) const {
    if (controller_.tool().tool != ToolType::MovePixels) return SelectionTransformHandle::None;
    const auto bounds = selection_transform_bounds();
    if (!bounds.has_value()) return SelectionTransformHandle::None;
    const QRectF box = pixel_rect(image_rect(), zoom_, bounds->x(), bounds->y(),
                                  bounds->width(), bounds->height());
    const qreal middle_x = box.center().x();
    const qreal middle_y = box.center().y();
    const qreal rotation_y = box.top() - 22.0;
    const std::array<std::pair<SelectionTransformHandle, QPointF>, 9> handles = {{
        {SelectionTransformHandle::Rotate, {middle_x, rotation_y}},
        {SelectionTransformHandle::NorthWest, box.topLeft()},
        {SelectionTransformHandle::North, {middle_x, box.top()}},
        {SelectionTransformHandle::NorthEast, box.topRight()},
        {SelectionTransformHandle::East, {box.right(), middle_y}},
        {SelectionTransformHandle::SouthEast, box.bottomRight()},
        {SelectionTransformHandle::South, {middle_x, box.bottom()}},
        {SelectionTransformHandle::SouthWest, box.bottomLeft()},
        {SelectionTransformHandle::West, {box.left(), middle_y}}
    }};
    for (const auto& [handle, center] : handles) {
        if (QRectF(center.x() - 6.0, center.y() - 6.0, 12.0, 12.0).contains(position))
            return handle;
    }
    return SelectionTransformHandle::None;
}

SelectionTransformHandle QtCanvasWidget::raster_text_box_handle_at(
    const QPointF& position) const {
    if (controller_.tool().tool != ToolType::Text || !raster_text_box_active_) {
        return SelectionTransformHandle::None;
    }
    const QRectF box = pixel_rect(image_rect(), zoom_, raster_text_box_x_,
                                  raster_text_box_y_, raster_text_box_width_,
                                  raster_text_box_height_);
    const qreal middle_x = box.center().x();
    const qreal middle_y = box.center().y();
    const std::array<std::pair<SelectionTransformHandle, QPointF>, 8> handles = {{
        {SelectionTransformHandle::NorthWest, box.topLeft()},
        {SelectionTransformHandle::North, {middle_x, box.top()}},
        {SelectionTransformHandle::NorthEast, box.topRight()},
        {SelectionTransformHandle::East, {box.right(), middle_y}},
        {SelectionTransformHandle::SouthEast, box.bottomRight()},
        {SelectionTransformHandle::South, {middle_x, box.bottom()}},
        {SelectionTransformHandle::SouthWest, box.bottomLeft()},
        {SelectionTransformHandle::West, {box.left(), middle_y}}
    }};
    for (const auto& [handle, center] : handles) {
        if (QRectF(center.x() - 6.0, center.y() - 6.0, 12.0, 12.0)
                .contains(position)) {
            return handle;
        }
    }
    return SelectionTransformHandle::None;
}

void QtCanvasWidget::draw_selection_transform_handles(QPainter& painter) const {
    if (controller_.tool().tool != ToolType::MovePixels) return;
    const auto bounds = selection_transform_bounds();
    if (!bounds.has_value()) return;
    const QRectF box = pixel_rect(image_rect(), zoom_, bounds->x(), bounds->y(),
                                  bounds->width(), bounds->height());
    paint_selection_transform_frame(painter, box);
}

void QtCanvasWidget::draw_raster_text_box_handles(QPainter& painter) const {
    if (controller_.tool().tool != ToolType::Text || !raster_text_box_active_) return;
    const QRectF box = pixel_rect(image_rect(), zoom_, raster_text_box_x_,
                                  raster_text_box_y_, raster_text_box_width_,
                                  raster_text_box_height_);
    const qreal middle_x = box.center().x();
    const qreal middle_y = box.center().y();
    const std::array<QPointF, 8> handles = {{
        box.topLeft(), {middle_x, box.top()}, box.topRight(),
        {box.right(), middle_y}, box.bottomRight(),
        {middle_x, box.bottom()}, box.bottomLeft(), {box.left(), middle_y}
    }};
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kSelectionStroke, 1.5));
    painter.drawRect(box);
    painter.setBrush(QColor(245, 248, 252));
    for (const QPointF& center : handles) {
        painter.drawRect(QRectF(center.x() - 4.0, center.y() - 4.0, 8.0, 8.0));
    }
    painter.restore();
}

void QtCanvasWidget::update_raster_text_box_resize(int x, int y) {
    if (!resizing_raster_text_box_ ||
        raster_text_box_handle_ == SelectionTransformHandle::None) {
        return;
    }
    int left = raster_text_box_before_resize_.x();
    int top = raster_text_box_before_resize_.y();
    int right = left + raster_text_box_before_resize_.width();
    int bottom = top + raster_text_box_before_resize_.height();
    const bool west = raster_text_box_handle_ == SelectionTransformHandle::West ||
                      raster_text_box_handle_ == SelectionTransformHandle::NorthWest ||
                      raster_text_box_handle_ == SelectionTransformHandle::SouthWest;
    const bool east = raster_text_box_handle_ == SelectionTransformHandle::East ||
                      raster_text_box_handle_ == SelectionTransformHandle::NorthEast ||
                      raster_text_box_handle_ == SelectionTransformHandle::SouthEast;
    const bool north = raster_text_box_handle_ == SelectionTransformHandle::North ||
                       raster_text_box_handle_ == SelectionTransformHandle::NorthWest ||
                       raster_text_box_handle_ == SelectionTransformHandle::NorthEast;
    const bool south = raster_text_box_handle_ == SelectionTransformHandle::South ||
                       raster_text_box_handle_ == SelectionTransformHandle::SouthWest ||
                       raster_text_box_handle_ == SelectionTransformHandle::SouthEast;
    if (west) left = std::clamp(x, 0, right - 1);
    if (east) right = std::clamp(x + 1, left + 1, controller_.document().width);
    if (north) top = std::clamp(y, 0, bottom - 1);
    if (south) bottom = std::clamp(y + 1, top + 1, controller_.document().height);
    if (raster_text_box_changed) {
        raster_text_box_changed(left, top, right - left, bottom - top);
    } else {
        set_raster_text_box(left, top, right - left, bottom - top);
    }
}

void QtCanvasWidget::paint_selection_transform_frame(QPainter& painter, const QRectF& box) {
    const qreal middle_x = box.center().x();
    const qreal middle_y = box.center().y();
    const QPointF rotation(middle_x, box.top() - 22.0);
    const std::array<QPointF, 8> handles = {{box.topLeft(), {middle_x, box.top()}, box.topRight(),
                                             {box.right(), middle_y}, box.bottomRight(),
                                             {middle_x, box.bottom()}, box.bottomLeft(),
                                             {box.left(), middle_y}}};
    painter.save();
    // The transform frame must never have a fill: filling this rectangle hid
    // the floating pixels under an opaque white box while they were moved.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kSelectionStroke, 1.5));
    painter.drawRect(box);
    painter.drawLine(QPointF(middle_x, box.top()), rotation);
    painter.setBrush(QColor(245, 248, 252));
    for (const QPointF& center : handles) {
        painter.drawRect(QRectF(center.x() - 4.0, center.y() - 4.0, 8.0, 8.0));
    }
    painter.drawEllipse(QRectF(rotation.x() - 5.0, rotation.y() - 5.0, 10.0, 10.0));
    painter.restore();
}

std::optional<QRect> QtCanvasWidget::active_selection_preview() const {
    if (!drawing_) return std::nullopt;

    const ToolType tool = controller_.tool().tool;
    if (tool == ToolType::RectSelect || tool == ToolType::EllipseSelect) {
        const auto endpoint = constrained_tool_endpoint(
            tool, drag_start_pixel_.x(), drag_start_pixel_.y(),
            drag_current_pixel_.x(), drag_current_pixel_.y(),
            QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) ||
                QApplication::keyboardModifiers().testFlag(Qt::MetaModifier));
        const int left = std::min(drag_start_pixel_.x(), endpoint[0]);
        const int top = std::min(drag_start_pixel_.y(), endpoint[1]);
        const int right = std::max(drag_start_pixel_.x(), endpoint[0]);
        const int bottom = std::max(drag_start_pixel_.y(), endpoint[1]);
        return QRect(left, top, right - left + 1, bottom - top + 1);
    }

    if (tool == ToolType::LassoSelect) {
        int left = drag_current_pixel_.x();
        int top = drag_current_pixel_.y();
        int right = left;
        int bottom = top;
        for (const auto& point : controller_.lasso_points()) {
            left = std::min(left, point[0]);
            top = std::min(top, point[1]);
            right = std::max(right, point[0]);
            bottom = std::max(bottom, point[1]);
        }
        return QRect(left, top, right - left + 1, bottom - top + 1);
    }

    return std::nullopt;
}

SelectionCombineMode QtCanvasWidget::selection_mode(Qt::KeyboardModifiers modifiers, bool secondary) const {
    const bool action = modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier);
    const bool alt = modifiers.testFlag(Qt::AltModifier);
    if (secondary && action) return SelectionCombineMode::Invert;
    if (secondary && alt) return SelectionCombineMode::Intersect;
    if (action) return SelectionCombineMode::Add;
    if (alt) return SelectionCombineMode::Subtract;
    return SelectionCombineMode::Replace;
}

void QtCanvasWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    notify_pointer_coordinates(event->position());
    const bool space_pan = space_down_ && event->button() == Qt::LeftButton;
    if (event->button() == Qt::MiddleButton || space_pan) {
        panning_ = true;
        pan_anchor_ = event->position();
        pan_start_ = pan_;
        return;
    }
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton) return;
    if (event->button() == Qt::LeftButton && controller_.tool().tool == ToolType::MovePixels) {
        const SelectionTransformHandle handle = selection_transform_handle_at(event->position());
        if (handle != SelectionTransformHandle::None) {
            const QPoint transform_pixel = pixel_at(event->position());
            transforming_selection_ = controller_.begin_selection_transform(
                handle, transform_pixel.x(), transform_pixel.y());
            if (transforming_selection_) {
                drag_start_pixel_ = drag_current_pixel_ = transform_pixel;
                event->accept();
                update();
                return;
            }
        }
    }
    if (event->button() == Qt::LeftButton &&
        controller_.tool().tool == ToolType::Text) {
        const SelectionTransformHandle handle =
            raster_text_box_handle_at(event->position());
        if (handle != SelectionTransformHandle::None) {
            resizing_raster_text_box_ = true;
            raster_text_box_handle_ = handle;
            raster_text_box_before_resize_ = raster_text_box();
            event->accept();
            return;
        }
    }
    const QPoint pixel = pixel_at(event->position());
    if (!valid_pixel(pixel)) return;
    const bool secondary = event->button() == Qt::RightButton;
    if (controller_.tool().tool == ToolType::Text) {
        if (text_requested) text_requested(pixel.x(), pixel.y(), secondary);
        update();
        return;
    }
    drawing_ = true;
    drag_start_pixel_ = drag_current_pixel_ = pixel;
    controller_.begin_stroke(pixel.x(), pixel.y(), secondary, selection_mode(event->modifiers(), secondary));
    if (selection_geometry_changed) selection_geometry_changed();
    notify_selection_preview();
    update();
}

void QtCanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        pan_ = pan_start_ + event->position() - pan_anchor_;
        notify_pointer_coordinates(event->position());
        update();
        return;
    }
    notify_pointer_coordinates(event->position());
    if (resizing_raster_text_box_) {
        const QPoint pixel = pixel_at(event->position());
        update_raster_text_box_resize(pixel.x(), pixel.y());
        update();
        return;
    }
    if (transforming_selection_) {
        const QPoint pixel = pixel_at(event->position());
        drag_current_pixel_ = pixel;
        controller_.update_selection_transform(
            pixel.x(), pixel.y(),
            event->modifiers().testFlag(Qt::ControlModifier) ||
                event->modifiers().testFlag(Qt::MetaModifier));
        if (selection_geometry_changed) selection_geometry_changed();
        update();
        return;
    }
    if (controller_.tool().tool == ToolType::Text && raster_text_box_active_) {
        const SelectionTransformHandle handle =
            raster_text_box_handle_at(event->position());
        switch (handle) {
            case SelectionTransformHandle::NorthWest:
            case SelectionTransformHandle::SouthEast:
                setCursor(Qt::SizeFDiagCursor);
                break;
            case SelectionTransformHandle::NorthEast:
            case SelectionTransformHandle::SouthWest:
                setCursor(Qt::SizeBDiagCursor);
                break;
            case SelectionTransformHandle::North:
            case SelectionTransformHandle::South:
                setCursor(Qt::SizeVerCursor);
                break;
            case SelectionTransformHandle::East:
            case SelectionTransformHandle::West:
                setCursor(Qt::SizeHorCursor);
                break;
            default:
                unsetCursor();
                break;
        }
    } else {
        unsetCursor();
    }
    if (!drawing_) return;
    const QPoint pixel = pixel_at(event->position());
    if (valid_pixel(pixel)) {
        drag_current_pixel_ = pixel;
        controller_.update_stroke(pixel.x(), pixel.y(), event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::MetaModifier));
        if (selection_geometry_changed) selection_geometry_changed();
        notify_selection_preview();
        update();
    }
}

void QtCanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    notify_pointer_coordinates(event->position());
    if (panning_ && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning_ = false;
        return;
    }
    if (resizing_raster_text_box_ && event->button() == Qt::LeftButton) {
        resizing_raster_text_box_ = false;
        raster_text_box_handle_ = SelectionTransformHandle::None;
        if (raster_text_box_resize_finished) raster_text_box_resize_finished();
        event->accept();
        update();
        return;
    }
    if (transforming_selection_ && event->button() == Qt::LeftButton) {
        transforming_selection_ = false;
        if (controller_.end_selection_transform()) notify_changed();
        return;
    }
    if (!drawing_) return;
    const QPoint pixel = valid_pixel(pixel_at(event->position())) ? pixel_at(event->position()) : drag_current_pixel_;
    controller_.end_stroke(pixel.x(), pixel.y(), event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::MetaModifier));
    drawing_ = false;
    notify_selection_preview();
    notify_changed();
}

void QtCanvasWidget::tabletEvent(QTabletEvent* event) {
    if (controller_.tool().tool == ToolType::Text) {
        event->ignore();
        return;
    }
    const QPoint pixel = pixel_at(event->position());
    const bool constrain = event->modifiers().testFlag(Qt::ControlModifier) ||
                           event->modifiers().testFlag(Qt::MetaModifier);
    const bool secondary = event->button() == Qt::RightButton;
    if (event->type() == QEvent::TabletPress) {
        setFocus();
        if (controller_.tool().tool == ToolType::MovePixels) {
            const SelectionTransformHandle handle = selection_transform_handle_at(event->position());
            if (handle != SelectionTransformHandle::None) {
                transforming_selection_ = controller_.begin_selection_transform(
                    handle, pixel.x(), pixel.y());
                if (transforming_selection_) {
                    drag_start_pixel_ = drag_current_pixel_ = pixel;
                    event->accept();
                    update();
                    return;
                }
            }
        }
        if (!valid_pixel(pixel)) {
            event->ignore();
            return;
        }
        drawing_ = true;
        drag_start_pixel_ = drag_current_pixel_ = pixel;
        controller_.begin_stroke(pixel.x(), pixel.y(), secondary,
                                 selection_mode(event->modifiers(), secondary),
                                 static_cast<float>(event->pressure()));
        event->accept();
        update();
        return;
    }
    if (event->type() == QEvent::TabletMove) {
        if (transforming_selection_) {
            drag_current_pixel_ = pixel;
            controller_.update_selection_transform(pixel.x(), pixel.y(), constrain);
            event->accept();
            update();
            return;
        }
        if (!drawing_ || !valid_pixel(pixel)) {
            event->ignore();
            return;
        }
        drag_current_pixel_ = pixel;
        controller_.update_stroke(pixel.x(), pixel.y(), constrain,
                                  static_cast<float>(event->pressure()));
        event->accept();
        update();
        return;
    }
    if (event->type() == QEvent::TabletRelease) {
        if (transforming_selection_) {
            transforming_selection_ = false;
            if (controller_.end_selection_transform()) notify_changed();
            event->accept();
            return;
        }
        if (!drawing_) {
            event->ignore();
            return;
        }
        const QPoint endpoint = valid_pixel(pixel) ? pixel : drag_current_pixel_;
        controller_.end_stroke(endpoint.x(), endpoint.y(), constrain,
                               static_cast<float>(event->pressure()));
        drawing_ = false;
        notify_selection_preview();
        notify_changed();
        event->accept();
        return;
    }
    event->ignore();
}

void QtCanvasWidget::leaveEvent(QEvent* event) {
    if (pointer_coordinates_changed) pointer_coordinates_changed(std::nullopt);
    QOpenGLWidget::leaveEvent(event);
}

void QtCanvasWidget::notify_pointer_coordinates(const QPointF& position) {
    if (!pointer_coordinates_changed) return;
    const QPoint pixel = pixel_at(position);
    pointer_coordinates_changed(valid_pixel(pixel) ? std::optional<QPoint>(pixel) : std::nullopt);
}

void QtCanvasWidget::notify_selection_preview() {
    if (selection_preview_changed) selection_preview_changed(active_selection_preview());
}

void QtCanvasWidget::wheelEvent(QWheelEvent* event) {
    const double factor = std::pow(1.25, static_cast<double>(event->angleDelta().y()) / 120.0);
    change_zoom(zoom_ * factor, event->position());
    event->accept();
}

void QtCanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) { space_down_ = true; event->accept(); return; }
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        controller_.cancel_interaction();
        controller_.clear_selection();
        drawing_ = false;
        transforming_selection_ = false;
        notify_selection_preview();
        notify_changed();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) { controller_.delete_selection(); notify_changed(); return; }
    if (event->key() == Qt::Key_BracketLeft) { controller_.tool().brush_size = std::max(1, controller_.tool().brush_size - 1); notify_changed(); return; }
    if (event->key() == Qt::Key_BracketRight) { controller_.tool().brush_size = std::min(256, controller_.tool().brush_size + 1); notify_changed(); return; }
    int dx = 0;
    int dy = 0;
    if (event->key() == Qt::Key_Left) dx = -1;
    if (event->key() == Qt::Key_Right) dx = 1;
    if (event->key() == Qt::Key_Up) dy = -1;
    if (event->key() == Qt::Key_Down) dy = 1;
    if (dx != 0 || dy != 0) { controller_.nudge_selection(dx * (event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1), dy * (event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1)); notify_changed(); return; }
    QOpenGLWidget::keyPressEvent(event);
}

void QtCanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) { space_down_ = false; event->accept(); return; }
    QOpenGLWidget::keyReleaseEvent(event);
}

void QtCanvasWidget::change_zoom(double next_zoom, const QPointF& anchor) {
    next_zoom = std::clamp(next_zoom, 0.02, 128.0);
    const QRectF before = image_rect();
    const QPointF document_point((anchor.x() - before.left()) / zoom_, (anchor.y() - before.top()) / zoom_);
    zoom_ = next_zoom;
    const QRectF after = image_rect();
    pan_ += anchor - QPointF(after.left() + document_point.x() * zoom_, after.top() + document_point.y() * zoom_);
    fit_requested_ = false;
    update();
}

void QtCanvasWidget::zoom_in() { change_zoom(zoom_ * 1.25, rect().center()); }
void QtCanvasWidget::zoom_out() { change_zoom(zoom_ / 1.25, rect().center()); }
void QtCanvasWidget::actual_size() { zoom_ = 1.0; pan_ = {}; fit_requested_ = false; update(); }
void QtCanvasWidget::fit_to_canvas() {
    const double horizontal = std::max(1.0, static_cast<double>(width() - 32)) / static_cast<double>(controller_.document().width);
    const double vertical = std::max(1.0, static_cast<double>(height() - 32)) / static_cast<double>(controller_.document().height);
    zoom_ = std::clamp(std::min(horizontal, vertical), 0.02, 128.0);
    pan_ = {};
    fit_requested_ = true;
    update();
}
void QtCanvasWidget::set_grid_visible(bool visible) { grid_visible_ = visible; update(); }
void QtCanvasWidget::set_checker_visible(bool visible) { checker_visible_ = visible; update(); }
void QtCanvasWidget::set_onion_visible(bool visible) { onion_visible_ = visible; update(); }
void QtCanvasWidget::set_raster_text_preview(int x, int y, RasterTextImage preview) {
    raster_text_preview_x_ = x;
    raster_text_preview_y_ = y;
    raster_text_preview_ = std::move(preview);
    ++raster_text_preview_revision_;
    update();
}
void QtCanvasWidget::clear_raster_text_preview() {
    if (raster_text_preview_.pixels.empty()) return;
    raster_text_preview_ = {};
    ++raster_text_preview_revision_;
    update();
}
void QtCanvasWidget::set_raster_text_box(int x, int y, int width, int height) {
    raster_text_box_active_ = true;
    raster_text_box_x_ = x;
    raster_text_box_y_ = y;
    raster_text_box_width_ = std::max(1, width);
    raster_text_box_height_ = std::max(1, height);
    update();
}
void QtCanvasWidget::clear_raster_text_box() {
    if (!raster_text_box_active_) return;
    raster_text_box_active_ = false;
    resizing_raster_text_box_ = false;
    raster_text_box_handle_ = SelectionTransformHandle::None;
    update();
}
void QtCanvasWidget::notify_changed() { update(); if (editor_changed) editor_changed(); }

} // namespace px
