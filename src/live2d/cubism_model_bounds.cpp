#include "cubism_model.hpp"

#include <Motion/CubismExpressionMotion.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

namespace bongo_cat {

bool NativeModel::canvas_size(int *width, int *height) const {
    if (!_model || !width || !height) return false;
    const float canvas_width = _model->GetCanvasWidthPixel();
    const float canvas_height = _model->GetCanvasHeightPixel();
    const float maximum = (float)std::numeric_limits<int>::max();
    if (!std::isfinite(canvas_width) || !std::isfinite(canvas_height) ||
        canvas_width <= 0.0f || canvas_height <= 0.0f ||
        canvas_width > maximum || canvas_height > maximum)
        return false;
    const int rounded_width = (int)std::lround(canvas_width);
    const int rounded_height = (int)std::lround(canvas_height);
    if (rounded_width <= 0 || rounded_height <= 0) return false;
    *width = rounded_width;
    *height = rounded_height;
    return true;
}

NativeModel::ModelBounds NativeModel::capture_visible_bounds() const {
    ModelBounds bounds;
    if (!_model) return bounds;
    struct DrawableBounds { ModelBounds bounds; float area; };
    std::vector<DrawableBounds> drawables;
    size_t largest = 0;
    for (int i = 0; i < _model->GetDrawableCount(); ++i) {
        if (!_model->GetDrawableDynamicFlagIsVisible(i) ||
            _model->GetDrawableOpacity(i) <= 0.001f) continue;
        const float *vertices = _model->GetDrawableVertices(i);
        int count = _model->GetDrawableVertexCount(i);
        if (!vertices || count <= 0) continue;
        int texture = _model->GetDrawableTextureIndex(i);
        const BongoCatImageAlphaMask *mask = texture >= 0 &&
            (size_t)texture < texture_alpha_.size() ? &texture_alpha_[(size_t)texture] : nullptr;
        const auto *uvs = _model->GetDrawableVertexUvs(i);
        const auto *indices = _model->GetDrawableVertexIndices(i);
        int index_count = _model->GetDrawableVertexIndexCount(i);
        ModelBounds drawable = {FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX, true};
        float visible_area = 0.0f;
        auto alpha = [&](float u, float v) {
            if (!mask || !mask->width || !mask->height) return 255;
            int x = std::max(0, std::min(mask->width - 1, (int)(u * mask->width)));
            int y = std::max(0, std::min(mask->height - 1, (int)(v * mask->height)));
            return (int)mask->pixels[(size_t)y * mask->width + x];
        };
        for (int j = 0; indices && uvs && j + 2 < index_count; j += 3) {
            int a = indices[j], b = indices[j + 1], c = indices[j + 2];
            if (a >= count || b >= count || c >= count) continue;
            float ax = vertices[a * 2], ay = vertices[a * 2 + 1];
            float bx = vertices[b * 2], by = vertices[b * 2 + 1];
            float cx = vertices[c * 2], cy = vertices[c * 2 + 1];
            float u = (uvs[a].X + uvs[b].X + uvs[c].X) / 3.0f;
            float v = (uvs[a].Y + uvs[b].Y + uvs[c].Y) / 3.0f;
            int opacity = std::max({alpha(u, v),
                alpha((uvs[a].X + uvs[b].X) * 0.5f, (uvs[a].Y + uvs[b].Y) * 0.5f),
                alpha((uvs[b].X + uvs[c].X) * 0.5f, (uvs[b].Y + uvs[c].Y) * 0.5f),
                alpha((uvs[c].X + uvs[a].X) * 0.5f, (uvs[c].Y + uvs[a].Y) * 0.5f)});
            if (opacity <= 8) continue;
            float area = std::fabs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
            visible_area += area * opacity / 255.0f;
            drawable.min_x = std::min({drawable.min_x, ax, bx, cx});
            drawable.min_y = std::min({drawable.min_y, ay, by, cy});
            drawable.max_x = std::max({drawable.max_x, ax, bx, cx});
            drawable.max_y = std::max({drawable.max_y, ay, by, cy});
        }
        if (visible_area <= FLT_EPSILON) continue;
        drawables.push_back({drawable, visible_area});
        if (visible_area > drawables[largest].area) largest = drawables.size() - 1;
    }
    if (drawables.empty()) return bounds;
    const DrawableBounds &anchor = drawables[largest];
    float extent = std::max(anchor.bounds.max_x - anchor.bounds.min_x,
        anchor.bounds.max_y - anchor.bounds.min_y);
    float padding = extent * 0.35f;
    bounds = anchor.bounds;
    // Small, remote effect particles must not dictate the scale of the subject.
    for (const DrawableBounds &drawable : drawables) {
        float dx = std::max({anchor.bounds.min_x - drawable.bounds.max_x,
            drawable.bounds.min_x - anchor.bounds.max_x, 0.0f});
        float dy = std::max({anchor.bounds.min_y - drawable.bounds.max_y,
            drawable.bounds.min_y - anchor.bounds.max_y, 0.0f});
        bool within_padding = dx <= padding && dy <= padding;
        bool major = drawable.area >= anchor.area * 0.05f;
        if (drawable.area < anchor.area * 0.0005f ||
            (!within_padding && !major)) continue;
        bounds.min_x = std::min(bounds.min_x, drawable.bounds.min_x);
        bounds.min_y = std::min(bounds.min_y, drawable.bounds.min_y);
        bounds.max_x = std::max(bounds.max_x, drawable.bounds.max_x);
        bounds.max_y = std::max(bounds.max_y, drawable.bounds.max_y);
    }
    return bounds;
}

static float frame_margin(float overflow, float padding) {
    /* Tiny mathematical overhangs are common at transparent mesh edges and
       do not produce visible clipping. */
    return overflow > 0.03f ? (overflow + padding) * 0.5f : 0.0f;
}

void NativeModel::prepare_expression_frame() {
    frame_ = BongoCatLive2DFrame{};
    /* Mver models have authored background/input layers in the same canvas;
       changing their viewport would break that compatibility contract. */
    if (!_model || render_options_.mver_projection) {
        update_viewport();
        return;
    }
    const int parameter_count = _model->GetParameterCount();
    std::vector<float> base((size_t)parameter_count);
    for (int i = 0; i < parameter_count; ++i)
        base[(size_t)i] = _model->GetParameterValue(i);

    _model->Update();
    ModelBounds envelope = capture_visible_bounds();
    auto include = [&envelope](const ModelBounds &source) {
        if (!source.valid) return;
        if (!envelope.valid) {
            envelope = source;
            return;
        }
        envelope.min_x = std::min(envelope.min_x, source.min_x);
        envelope.min_y = std::min(envelope.min_y, source.min_y);
        envelope.max_x = std::max(envelope.max_x, source.max_x);
        envelope.max_y = std::max(envelope.max_y, source.max_y);
    };
    for (size_t i = 0; i < expression_names_.size(); ++i) {
        auto found = expressions_.find(expression_names_[i]);
        if (found == expressions_.end()) continue;
        for (int parameter = 0; parameter < parameter_count; ++parameter)
            _model->SetParameterValue(parameter, base[(size_t)parameter]);
        auto *expression = static_cast<Csm::CubismExpressionMotion *>(
            found->second);
        const auto parameters = expression->GetExpressionParameters();
        for (Csm::csmUint32 parameter = 0;
            parameter < parameters.GetSize(); ++parameter) {
            const auto &value = parameters[parameter];
            int index = _model->GetParameterIndex(value.ParameterId);
            if (index < 0 || index >= parameter_count) continue;
            switch (value.BlendType) {
            case Csm::CubismExpressionMotion::Additive:
                _model->AddParameterValue(index, value.Value);
                break;
            case Csm::CubismExpressionMotion::Multiply:
                _model->MultiplyParameterValue(index, value.Value);
                break;
            case Csm::CubismExpressionMotion::Overwrite:
                _model->SetParameterValue(index, value.Value);
                break;
            }
        }
        _model->Update();
        include(capture_visible_bounds());
    }
    for (int parameter = 0; parameter < parameter_count; ++parameter)
        _model->SetParameterValue(parameter, base[(size_t)parameter]);
    _model->Update();

    int reference_width = 0, reference_height = 0;
    if (render_options_.mver_projection) {
        reference_width = render_options_.reference_width;
        reference_height = render_options_.reference_height;
    }
    if ((reference_width <= 0 || reference_height <= 0) &&
        !canvas_size(&reference_width, &reference_height)) {
        update_viewport();
        return;
    }
    if (!envelope.valid) {
        update_viewport();
        return;
    }
    Csm::CubismMatrix44 projection;
    build_projection(projection, reference_width, reference_height);
    float x0 = projection.TransformX(envelope.min_x);
    float x1 = projection.TransformX(envelope.max_x);
    float y0 = projection.TransformY(envelope.min_y);
    float y1 = projection.TransformY(envelope.max_y);
    float min_x = std::min(x0, x1), max_x = std::max(x0, x1);
    float min_y = std::min(y0, y1), max_y = std::max(y0, y1);
    if (!std::isfinite(min_x) || !std::isfinite(max_x) ||
        !std::isfinite(min_y) || !std::isfinite(max_y)) {
        update_viewport();
        return;
    }
    float span = std::max(max_x - min_x, max_y - min_y);
    float padding = std::max(0.04f, std::min(0.16f, span * 0.05f));
    float horizontal = std::max(
        frame_margin(-1.0f - min_x, padding),
        frame_margin(max_x - 1.0f, padding));
    /* Mirroring can move either horizontal overhang to the opposite side. */
    frame_.left = horizontal;
    frame_.right = horizontal;
    frame_.bottom = frame_margin(-1.0f - min_y, padding);
    frame_.top = frame_margin(max_y - 1.0f, padding);
    update_viewport();
}

void NativeModel::update_viewport() {
    float horizontal = 1.0f + frame_.left + frame_.right;
    float vertical = 1.0f + frame_.top + frame_.bottom;
    if (!std::isfinite(horizontal) || !std::isfinite(vertical) ||
        horizontal <= 0.0f || vertical <= 0.0f || width_ <= 0 || height_ <= 0) {
        viewport_x_ = viewport_y_ = 0;
        viewport_width_ = std::max(1, width_);
        viewport_height_ = std::max(1, height_);
        return;
    }
    float content_width = width_ / horizontal;
    float content_height = height_ / vertical;
    int left = (int)std::lround(frame_.left * content_width);
    int right = (int)std::lround(frame_.right * content_width);
    int bottom = (int)std::lround(frame_.bottom * content_height);
    int top = (int)std::lround(frame_.top * content_height);
    viewport_x_ = std::max(0, std::min(width_ - 1, left));
    viewport_y_ = std::max(0, std::min(height_ - 1, bottom));
    viewport_width_ = std::max(1, width_ - viewport_x_ - std::max(0, right));
    viewport_height_ = std::max(1, height_ - viewport_y_ - std::max(0, top));
}

bool NativeModel::frame(BongoCatLive2DFrame *frame) const {
    if (!_model || !frame) return false;
    *frame = frame_;
    return true;
}

bool NativeModel::viewport(int *x, int *y, int *width, int *height) const {
    if (!_model || !x || !y || !width || !height) return false;
    *x = viewport_x_;
    *y = viewport_y_;
    *width = viewport_width_;
    *height = viewport_height_;
    return true;
}

void NativeModel::apply_viewport_projection(
    Csm::CubismMatrix44 &projection) const {
    if (width_ <= 0 || height_ <= 0 || viewport_width_ <= 0 ||
        viewport_height_ <= 0) return;
    float scale_x = (float)viewport_width_ / (float)width_;
    float scale_y = (float)viewport_height_ / (float)height_;
    float translate_x = (2.0f * viewport_x_ + viewport_width_) /
        (float)width_ - 1.0f;
    float translate_y = (2.0f * viewport_y_ + viewport_height_) /
        (float)height_ - 1.0f;
    float *matrix = projection.GetArray();
    matrix[0] *= scale_x;
    matrix[4] *= scale_x;
    matrix[12] = matrix[12] * scale_x + translate_x;
    matrix[1] *= scale_y;
    matrix[5] *= scale_y;
    matrix[13] = matrix[13] * scale_y + translate_y;
}

void NativeModel::record_visible_state(Csm::CubismMatrix44 &projection) {
    visual_state_.drawable_count = _model->GetDrawableCount();
    for (int i = 0; i < _model->GetDrawableCount(); ++i) {
        if (_model->GetDrawableDynamicFlagIsVisible(i) &&
            _model->GetDrawableOpacity(i) > 0.001f)
            ++visual_state_.drawable_visible;
        if (_model->GetDrawableDynamicFlagVertexPositionsDidChange(i))
            ++visual_state_.drawable_vertex_changed;
    }
    visual_state_.offscreen_count = _model->GetOffscreenCount();
    for (int i = 0; i < _model->GetOffscreenCount(); ++i)
        if (_model->GetOffscreenOpacity(i) > 0.001f)
            ++visual_state_.offscreen_positive;
    visual_state_.part_count = _model->GetPartCount();
    for (int i = 0; i < _model->GetPartCount(); ++i)
        if (_model->GetPartOpacity(i) > 0.001f) ++visual_state_.part_positive;
    ModelBounds bounds = capture_visible_bounds();
    if (!bounds.valid) return;
    float x0 = projection.TransformX(bounds.min_x);
    float x1 = projection.TransformX(bounds.max_x);
    float y0 = projection.TransformY(bounds.min_y);
    float y1 = projection.TransformY(bounds.max_y);
    visual_state_.visible_min_x = std::min(x0, x1);
    visual_state_.visible_max_x = std::max(x0, x1);
    visual_state_.visible_min_y = std::min(y0, y1);
    visual_state_.visible_max_y = std::max(y0, y1);
    visual_state_.visible = true;
}

bool NativeModel::visual_state(BongoCatLive2DVisualState *state) const {
    if (!state || !visual_state_ready_) return false;
    *state = visual_state_;
    return true;
}

} // namespace bongo_cat
