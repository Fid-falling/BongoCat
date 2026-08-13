#include "cubism_model.hpp"
#include "cubism_viewer_look.hpp"

#include <Id/CubismIdManager.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Model/CubismModel.hpp>
#include <Motion/CubismExpressionMotionManager.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
namespace bongo_cat {

void NativeModel::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    width_ = width;
    height_ = height;
    if (!_model || (width == renderer_width_ && height == renderer_height_)) return;
    if (!_model->IsBlendModeEnabled()) {
        renderer_width_ = width_;
        renderer_height_ = height_;
        return;
    }
    DeleteRenderer();
    CreateRenderer((Csm::csmUint32)width_, (Csm::csmUint32)height_);
    renderer_width_ = width_;
    renderer_height_ = height_;
    bind_textures();
}

void NativeModel::reshape(int width, int height) {
    if (width > 0 && height > 0) { width_ = width; height_ = height; }
}

template<typename Getter>
static bool changed(std::vector<float> &snapshot, int count, Getter value) {
    bool result = snapshot.size() != (size_t)count;
    if (result) snapshot.resize((size_t)count);
    for (int i = 0; i < count; ++i) {
        float current = value(i);
        if (result || std::fabs(snapshot[(size_t)i] - current) > 0.00001f) result = true;
        snapshot[(size_t)i] = current;
    }
    return result;
}

bool NativeModel::update(float delta_seconds) {
    if (!_model || delta_seconds <= 0.0f) return false;
    if (delta_seconds > 0.25f) delta_seconds = 0.25f;
    external_parameters_dirty_ = false;
    motion_updated_ = suppress_eye_blink_;
    _model->LoadParameters();
    for (int i = 0; i < _model->GetParameterCount(); ++i) {
        if (!pending_parameters_[(size_t)i]) continue;
        _model->SetParameterValue(i, pending_parameter_values_[(size_t)i]);
        pending_parameters_[(size_t)i] = 0;
    }
    if (automatic_idle_ && _motionManager->IsFinished())
        start_idle_motion();
    else if (!_motionManager->IsFinished())
        motion_updated_ = _motionManager->UpdateMotion(_model, delta_seconds);
    _model->SaveParameters();
    _updateScheduler.OnLateUpdate(_model, delta_seconds);
    _opacity = _model->GetModelOpacity();
    _model->Update();
    bool result = changed(parameter_snapshot_, _model->GetParameterCount(),
        [this](int index) { return _model->GetParameterValue(index); });
    result = changed(part_snapshot_, _model->GetPartCount(),
        [this](int index) { return _model->GetPartOpacity(index); }) || result;
    if (std::fabs(opacity_snapshot_ - _opacity) > 0.00001f) result = true;
    opacity_snapshot_ = _opacity;
    return result;
}

void NativeModel::draw() {
    if (!_model || width_ <= 0 || height_ <= 0) return;
    auto *manager = Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance();
    manager->BeginFrameProcess();
    Csm::CubismMatrix44 projection;
    if (render_options_.mver_projection) {
        float aspect = (float)render_options_.reference_width /
            (float)render_options_.reference_height;
        projection.Scale((mirror_ ? -1.0f : 1.0f) *
            render_options_.projection_scale,
            render_options_.projection_scale * aspect);
        projection.Translate(render_options_.offset_x, render_options_.offset_y);
    } else if (_model->GetCanvasWidth() > 1.0f && width_ < height_) {
        _modelMatrix->SetWidth(2.0f);
        projection.Scale(mirror_ ? -1.0f : 1.0f,
            (float)width_ / (float)height_);
    } else {
        projection.Scale((mirror_ ? -1.0f : 1.0f) * (float)height_ / (float)width_, 1.0f);
    }
    projection.MultiplyByMatrix(_modelMatrix);
    if (render_options_.mver_projection) {
        // Mver 0.1.6's Core reports canvas units at twice the modern scale.
        // Preserve authored Layout translation while matching its model scale.
        float *matrix = projection.GetArray();
        constexpr float mver_core_canvas_scale = 0.5f;
        matrix[0] *= mver_core_canvas_scale;
        matrix[1] *= mver_core_canvas_scale;
        matrix[4] *= mver_core_canvas_scale;
        matrix[5] *= mver_core_canvas_scale;
    }
    visual_state_ = BongoCatLive2DVisualState{};
    visual_state_.fit_scale = 1.0f;
    visual_state_.mver_projection = render_options_.mver_projection;
    record_visible_state(projection);
    visual_state_ready_ = true;
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    renderer->SetMvpMatrix(&projection);
    renderer->DrawModel();
    manager->EndFrameProcess();
}

void NativeModel::set_mirror(bool mirror) { mirror_ = mirror; }

void NativeModel::set_render_options(const BongoCatLive2DRenderOptions &options) {
    render_options_ = options;
}

void NativeModel::set_dragging(float x, float y) {
    if (!viewer_look_) return;
    x = std::max(-1.0f, std::min(1.0f, x));
    y = std::max(-1.0f, std::min(1.0f, y));
    viewer_look_->set_target(x, y);
    external_parameters_dirty_ = true;
}

void NativeModel::prepare_viewer_audit() {
    automatic_idle_ = false;
    suppress_eye_blink_ = true;
    _motionManager->StopAllMotions();
    for (int i = 0; i < _model->GetParameterCount(); ++i)
        _model->SetParameterValue(i, _model->GetParameterDefaultValue(i));
    _model->SaveParameters();
}

bool NativeModel::set_parameter(const char *id, float value) {
    if (!_model || !id) return false;
    Csm::CubismIdHandle handle = Csm::CubismFramework::GetIdManager()->GetId(id);
    int index = _model->GetParameterIndex(handle);
    if (index < 0 || index >= _model->GetParameterCount()) return false;
    _model->SetParameterValue(index, value);
    pending_parameter_values_[(size_t)index] = value;
    pending_parameters_[(size_t)index] = 1;
    external_parameters_dirty_ = true;
    return true;
}

bool NativeModel::parameter(const char *id, float *minimum, float *maximum, float *value) {
    if (!_model || !id) return false;
    Csm::CubismIdHandle handle = Csm::CubismFramework::GetIdManager()->GetId(id);
    int index = _model->GetParameterIndex(handle);
    if (index < 0 || index >= _model->GetParameterCount()) return false;
    if (minimum) *minimum = _model->GetParameterMinimumValue(index);
    if (maximum) *maximum = _model->GetParameterMaximumValue(index);
    if (value) *value = _model->GetParameterValue(index);
    return true;
}

bool NativeModel::start_motion(const char *group, int index) {
    if (!group || index < 0) return false;
    std::string key = std::string(group) + "_" + std::to_string(index);
    bool selected = false;
    std::string playback = motion_to_play(key, &selected);
    auto found = motions_.find(playback);
    if (found == motions_.end()) return false;
    constexpr int priority = 2;
    bool started = _motionManager->StartMotionPriority(found->second, false,
        priority) != Csm::InvalidMotionQueueEntryHandleValue;
    if (started) select_motion(key, selected);
    return started;
}

void NativeModel::start_idle_motion() {
    if (idle_motion_keys_.empty()) return;
    constexpr int priority = 1;
    if (!_motionManager->ReserveMotion(priority)) return;
    int next = std::rand() % (int)idle_motion_keys_.size();
    if (next == last_idle_motion_) next = (next + 1) % (int)idle_motion_keys_.size();
    last_idle_motion_ = next;
    const std::string &key = idle_motion_keys_[(size_t)next];
    auto found = motions_.find(key);
    if (found != motions_.end())
        _motionManager->StartMotionPriority(found->second, false, priority);
}

void NativeModel::capture_motion_preview() {
    if (!_model || motion_preview_active_) return;
    const int parameter_count = _model->GetParameterCount();
    std::vector<float> current_parameters((size_t)parameter_count);
    for (int i = 0; i < parameter_count; ++i)
        current_parameters[(size_t)i] = _model->GetParameterValue(i);
    _model->LoadParameters();
    motion_preview_parameters_.resize((size_t)parameter_count);
    for (int i = 0; i < parameter_count; ++i) {
        motion_preview_parameters_[(size_t)i] = _model->GetParameterValue(i);
        _model->SetParameterValue(i, current_parameters[(size_t)i]);
    }
    const int part_count = _model->GetPartCount();
    motion_preview_parts_.resize((size_t)part_count);
    for (int i = 0; i < part_count; ++i)
        motion_preview_parts_[(size_t)i] = _model->GetPartOpacity(i);
    motion_preview_opacity_ = _model->GetModelOpacity();
    motion_preview_active_ = true;
}

void NativeModel::restore_motion_preview_state() {
    if (!_model || !motion_preview_active_) return;
    _motionManager->StopAllMotions();
    const int parameter_count = _model->GetParameterCount();
    for (int i = 0; i < parameter_count &&
        (size_t)i < motion_preview_parameters_.size(); ++i) {
        _model->SetParameterValue(i, motion_preview_parameters_[(size_t)i]);
        pending_parameters_[(size_t)i] = 0;
    }
    _model->SaveParameters();
    const int part_count = _model->GetPartCount();
    for (int i = 0; i < part_count &&
        (size_t)i < motion_preview_parts_.size(); ++i)
        _model->SetPartOpacity(i, motion_preview_parts_[(size_t)i]);
    _model->SetModelOpacity(motion_preview_opacity_);
    _opacity = motion_preview_opacity_;
    external_parameters_dirty_ = true;
}

bool NativeModel::preview_motion(const char *group, int index) {
    if (!_model || !group || index < 0) return false;
    if (motion_preview_active_) restore_motion_preview_state();
    else capture_motion_preview();
    std::string key = std::string(group) + "_" + std::to_string(index);
    std::string playback = motion_to_play(key, &motion_preview_selected_);
    auto found = motions_.find(playback);
    if (found == motions_.end()) return false;
    auto owner = motion_toggle_owners_.find(key);
    motion_preview_key_ = owner == motion_toggle_owners_.end() ? key : owner->second;
    constexpr int priority = 2;
    return _motionManager->StartMotionPriority(found->second, false, priority) !=
        Csm::InvalidMotionQueueEntryHandleValue;
}

bool NativeModel::commit_motion_preview(const char *group, int index) {
    if (!motion_preview_active_ || !group || index < 0) return false;
    std::string key = std::string(group) + "_" + std::to_string(index);
    auto owner = motion_toggle_owners_.find(key);
    const std::string &canonical = owner == motion_toggle_owners_.end() ?
        key : owner->second;
    if (canonical != motion_preview_key_) return false;
    select_motion(canonical, motion_preview_selected_);
    motion_preview_parameters_.clear();
    motion_preview_parts_.clear();
    motion_preview_key_.clear();
    motion_preview_active_ = false;
    return true;
}

bool NativeModel::restore_motion_preview() {
    if (!motion_preview_active_) return false;
    restore_motion_preview_state();
    motion_preview_parameters_.clear();
    motion_preview_parts_.clear();
    motion_preview_key_.clear();
    motion_preview_active_ = false;
    return true;
}

bool NativeModel::motion_selected(const char *group, int index) const {
    if (!group || index < 0) return false;
    std::string key = std::string(group) + "_" + std::to_string(index);
    auto owner = motion_toggle_owners_.find(key);
    if (owner != motion_toggle_owners_.end()) key = owner->second;
    return selected_motion_keys_.find(key) != selected_motion_keys_.end();
}

bool NativeModel::motion_visible(const char *group, int index) const {
    if (!group || index < 0) return false;
    std::string key = std::string(group) + "_" + std::to_string(index);
    auto owner = motion_toggle_owners_.find(key);
    return owner == motion_toggle_owners_.end() || owner->second == key;
}
bool NativeModel::set_expression(int index) {
    if (index == -1) {
        _expressionManager->StopAllMotions();
        expression_index_ = -1;
        return true;
    }
    if (index < 0 || (size_t)index >= expression_names_.size()) return false;
    auto found = expressions_.find(expression_names_[(size_t)index]);
    if (found == expressions_.end()) return false;
    _expressionManager->StopAllMotions();
    Csm::CubismMotionQueueEntryHandle handle = _expressionManager->StartMotion(
        found->second, false);
    if (handle == Csm::InvalidMotionQueueEntryHandleValue) return false;
    expression_index_ = index;
    return true;
}
} // namespace bongo_cat
