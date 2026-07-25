#include "cubism_model.hpp"
#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/image.h"

#include <Effect/CubismEyeBlink.hpp>
#include <Effect/CubismBreath.hpp>
#include <Id/CubismIdManager.hpp>
#include <Motion/CubismExpressionUpdater.hpp>
#include <Motion/CubismBreathUpdater.hpp>
#include <Motion/CubismEyeBlinkUpdater.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismPhysicsUpdater.hpp>
#include <Motion/CubismPoseUpdater.hpp>
#include <Motion/ICubismUpdater.hpp>
#include <Physics/CubismPhysics.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include <utility>
#include <yyjson.h>

namespace bongo_cat_neo {

namespace {

// This is the same late-update stage used by Bongo Cat Mver's myUserModel.
// Keeping it between expressions and breath is important: dragging is an
// additive contribution and must not overwrite motion/expression values.
class DragUpdater final : public Csm::ICubismUpdater {
public:
    explicit DragUpdater(const Csm::CubismTargetPoint &target)
        : Csm::ICubismUpdater(Csm::CubismUpdateOrder_Look), target_(target) {
        auto *ids = Csm::CubismFramework::GetIdManager();
        angle_x_ = ids->GetId("ParamAngleX");
        angle_y_ = ids->GetId("ParamAngleY");
        angle_z_ = ids->GetId("ParamAngleZ");
        body_angle_x_ = ids->GetId("ParamBodyAngleX");
        eye_ball_x_ = ids->GetId("ParamEyeBallX");
        eye_ball_y_ = ids->GetId("ParamEyeBallY");
    }

    void OnLateUpdate(Csm::CubismModel *model, Csm::csmFloat32) override {
        if (!model) return;
        const Csm::csmFloat32 x = target_.GetX();
        const Csm::csmFloat32 y = target_.GetY();
        model->AddParameterValue(angle_x_, x * 30.0f);
        model->AddParameterValue(angle_y_, y * 30.0f);
        model->AddParameterValue(angle_z_, x * y * -30.0f);
        model->AddParameterValue(body_angle_x_, x * 10.0f);
        model->AddParameterValue(eye_ball_x_, x);
        model->AddParameterValue(eye_ball_y_, y);
    }

private:
    const Csm::CubismTargetPoint &target_;
    Csm::CubismIdHandle angle_x_ = nullptr;
    Csm::CubismIdHandle angle_y_ = nullptr;
    Csm::CubismIdHandle angle_z_ = nullptr;
    Csm::CubismIdHandle body_angle_x_ = nullptr;
    Csm::CubismIdHandle eye_ball_x_ = nullptr;
    Csm::CubismIdHandle eye_ball_y_ = nullptr;
};

} // namespace

NativeModel::NativeModel() {
    _mocConsistency = true;
    _motionConsistency = true;
}

NativeModel::~NativeModel() {
    for (auto &item : motions_) Csm::ACubismMotion::Delete(item.second);
    for (auto &item : expressions_) Csm::ACubismMotion::Delete(item.second);
    release_render_resources();
    delete setting_;
}

std::vector<unsigned char> NativeModel::read(const std::string &file) const {
    FILE *stream = bongo_cat_neo_file_open(file.c_str(), "rb");
    if (!stream || std::fseek(stream, 0, SEEK_END) != 0) {
        if (stream) std::fclose(stream);
        return {};
    }
    long size = std::ftell(stream);
    if (size <= 0 || std::fseek(stream, 0, SEEK_SET) != 0) {
        std::fclose(stream);
        return {};
    }
    std::vector<unsigned char> bytes((size_t)size);
    bool read = std::fread(bytes.data(), 1, (size_t)size, stream) == (size_t)size;
    std::fclose(stream);
    if (!read) return {};
    return bytes;
}

std::string NativeModel::path(const char *relative) const {
    return directory_ + (relative ? relative : "");
}

bool NativeModel::load(const char *directory, const char *setting_file,
    bool direct_textures, BongoCatNeoError *error) {
    if (!directory || !setting_file) return false;
    direct_textures_ = direct_textures;
    directory_ = directory;
    if (!directory_.empty() && directory_.back() != '/' && directory_.back() != '\\')
        directory_ += '/';
    std::vector<unsigned char> json = read(path(setting_file));
    if (json.empty()) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO, "Cannot read model setting: %s", setting_file);
        return false;
    }
    setting_ = new(std::nothrow)
        Csm::CubismModelSettingJson(json.data(), (Csm::csmSizeInt)json.size());
    if (!setting_) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_MEMORY, "Cannot allocate model setting");
        return false;
    }
    if (!load_model(error)) return false;
    load_expressions();
    load_effects();
    load_motions();
    Csm::csmMap<Csm::csmString, Csm::csmFloat32> layout;
    setting_->GetLayoutMap(layout);
    _modelMatrix->SetupFromLayout(layout);
    _model->SaveParameters();
    return true;
}

bool NativeModel::load_model(BongoCatNeoError *error) {
    const char *name = setting_->GetModelFileName();
    std::vector<unsigned char> bytes = read(path(name));
    if (bytes.empty()) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO, "Cannot read moc3: %s", name);
        return false;
    }
    LoadModel(bytes.data(), (Csm::csmSizeInt)bytes.size(), true);
    if (!_model) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_CUBISM, "Cubism rejected moc3: %s", name);
        return false;
    }
    pending_parameter_values_.resize((size_t)_model->GetParameterCount());
    pending_parameters_.resize((size_t)_model->GetParameterCount());
    return true;
}

void NativeModel::load_expressions() {
    for (int i = 0; i < setting_->GetExpressionCount(); ++i) {
        const char *name = setting_->GetExpressionName(i);
        std::vector<unsigned char> bytes = read(path(setting_->GetExpressionFileName(i)));
        if (bytes.empty()) continue;
        Csm::ACubismMotion *motion = LoadExpression(bytes.data(),
            (Csm::csmSizeInt)bytes.size(), name);
        if (!motion) continue;
        expressions_[name] = motion;
        expression_names_.emplace_back(name);
    }
    if (!expressions_.empty()) {
        auto *updater = CSM_NEW Csm::CubismExpressionUpdater(*_expressionManager);
        _updateScheduler.AddUpdatableList(updater);
    }
}

void NativeModel::load_effects() {
    if (setting_->GetPhysicsFileName()[0]) {
        auto bytes = read(path(setting_->GetPhysicsFileName()));
        if (!bytes.empty()) LoadPhysics(bytes.data(), (Csm::csmSizeInt)bytes.size());
        if (_physics) _updateScheduler.AddUpdatableList(
            CSM_NEW Csm::CubismPhysicsUpdater(*_physics));
    }
    if (setting_->GetPoseFileName()[0]) {
        auto bytes = read(path(setting_->GetPoseFileName()));
        if (!bytes.empty()) LoadPose(bytes.data(), (Csm::csmSizeInt)bytes.size());
        if (_pose) _updateScheduler.AddUpdatableList(CSM_NEW Csm::CubismPoseUpdater(*_pose));
    }
    if (setting_->GetUserDataFile()[0]) {
        auto bytes = read(path(setting_->GetUserDataFile()));
        if (!bytes.empty()) LoadUserData(bytes.data(), (Csm::csmSizeInt)bytes.size());
    }
    Csm::csmVector<Csm::CubismBreath::BreathParameterData> breath;
    auto *ids = Csm::CubismFramework::GetIdManager();
    breath.PushBack(Csm::CubismBreath::BreathParameterData(
        ids->GetId("ParamAngleX"), 0.0f, 15.0f, 6.5345f, 0.5f));
    breath.PushBack(Csm::CubismBreath::BreathParameterData(
        ids->GetId("ParamAngleY"), 0.0f, 8.0f, 3.5345f, 0.5f));
    breath.PushBack(Csm::CubismBreath::BreathParameterData(
        ids->GetId("ParamAngleZ"), 0.0f, 10.0f, 5.5345f, 0.5f));
    breath.PushBack(Csm::CubismBreath::BreathParameterData(
        ids->GetId("ParamBodyAngleX"), 0.0f, 4.0f, 15.5345f, 0.5f));
    breath.PushBack(Csm::CubismBreath::BreathParameterData(
        ids->GetId("ParamBreath"), 0.5f, 0.5f, 3.2345f, 0.5f));
    _breath = Csm::CubismBreath::Create();
    _breath->SetParameters(breath);
    _updateScheduler.AddUpdatableList(CSM_NEW Csm::CubismBreathUpdater(*_breath));
    _updateScheduler.AddUpdatableList(CSM_NEW DragUpdater(*_dragManager));
    for (int i = 0; i < setting_->GetEyeBlinkParameterCount(); ++i)
        eye_blink_ids_.PushBack(setting_->GetEyeBlinkParameterId(i));
    for (int i = 0; i < setting_->GetLipSyncParameterCount(); ++i)
        lip_sync_ids_.PushBack(setting_->GetLipSyncParameterId(i));
    if (setting_->GetEyeBlinkParameterCount() > 0) {
        _eyeBlink = Csm::CubismEyeBlink::Create(setting_);
        _updateScheduler.AddUpdatableList(
            CSM_NEW Csm::CubismEyeBlinkUpdater(motion_updated_, *_eyeBlink));
    }
    _updateScheduler.SortUpdatableList();
}

void NativeModel::load_motions() {
    idle_motion_keys_.clear();
    for (int group_index = 0; group_index < setting_->GetMotionGroupCount(); ++group_index) {
        const char *group = setting_->GetMotionGroupName(group_index);
        for (int i = 0; i < setting_->GetMotionCount(group); ++i) {
            auto bytes = read(path(setting_->GetMotionFileName(group, i)));
            if (bytes.empty()) continue;
            std::string key = std::string(group) + "_" + std::to_string(i);
            auto *motion = static_cast<Csm::CubismMotion *>(LoadMotion(bytes.data(),
                (Csm::csmSizeInt)bytes.size(), key.c_str(), nullptr, nullptr,
                setting_, group, i, _motionConsistency));
            if (!motion) continue;
            motion->SetEffectIds(eye_blink_ids_, lip_sync_ids_);
            motions_[key] = motion;
            if (std::strcmp(group, "Idle") == 0) idle_motion_keys_.push_back(key);
            size_t suffix = std::strlen(group);
            if (suffix >= 5 && std::strcmp(group + suffix - 5, "_lock") == 0)
                load_lock_motion(key, bytes);
        }
    }
    _motionManager->StopAllMotions();
}

void NativeModel::load_lock_motion(const std::string &key,
    const std::vector<unsigned char> &bytes) {
    yyjson_doc *document = yyjson_read(
        reinterpret_cast<const char *>(bytes.data()), bytes.size(), 0);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : nullptr;
    yyjson_val *curves = yyjson_is_obj(root) ? yyjson_obj_get(root, "Curves") : nullptr;
    LockMotion lock;
    size_t index, count;
    yyjson_val *curve;
    if (yyjson_is_arr(curves)) yyjson_arr_foreach(curves, index, count, curve) {
        const char *target = yyjson_get_str(yyjson_obj_get(curve, "Target"));
        const char *id = yyjson_get_str(yyjson_obj_get(curve, "Id"));
        if (!target || std::strcmp(target, "Parameter") != 0 || !id) continue;
        Csm::CubismIdHandle handle = Csm::CubismFramework::GetIdManager()->GetId(id);
        int parameter = _model->GetParameterIndex(handle);
        if (parameter < 0 || parameter >= _model->GetParameterCount()) continue;
        if (std::find(lock.parameters.begin(), lock.parameters.end(), parameter) ==
            lock.parameters.end()) lock.parameters.push_back(parameter);
    }
    if (document) yyjson_doc_free(document);
    if (!lock.parameters.empty()) lock_motions_[key] = std::move(lock);
}

bool NativeModel::load_textures(BongoCatNeoError *error) {
    release_textures();
    textures_.assign((size_t)setting_->GetTextureCount(), 0);
    for (int i = 0; i < setting_->GetTextureCount(); ++i) {
        textures_[(size_t)i] = bongo_cat_neo_image_texture_mipmapped(
            path(setting_->GetTextureFileName(i)).c_str(), direct_textures_,
            nullptr, nullptr, error);
        if (!textures_[(size_t)i]) {
            release_textures();
            return false;
        }
    }
    release_renderer();
    CreateRenderer((Csm::csmUint32)width_, (Csm::csmUint32)height_);
    renderer_width_ = width_;
    renderer_height_ = height_;
    bind_textures();
    return true;
}

void NativeModel::release_textures() {
    if (!textures_.empty())
        glDeleteTextures((GLsizei)textures_.size(), textures_.data());
    textures_.clear();
}

void NativeModel::release_renderer() {
    DeleteRenderer();
    renderer_width_ = 0;
    renderer_height_ = 0;
}

void NativeModel::release_render_resources() {
    release_textures();
    release_renderer();
}

void NativeModel::bind_textures() {
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (!renderer) return;
    for (size_t i = 0; i < textures_.size(); ++i)
        if (textures_[i]) renderer->BindTexture((Csm::csmInt32)i, textures_[i]);
    renderer->IsPremultipliedAlpha(false);
}

} // namespace bongo_cat_neo
