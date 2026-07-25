#ifndef BONGO_CAT_NEO_CUBISM_MODEL_HPP
#define BONGO_CAT_NEO_CUBISM_MODEL_HPP

#include "bongo_cat_neo/common.h"

#include <Model/CubismUserModel.hpp>
#include <CubismModelSettingJson.hpp>
#include <Motion/ACubismMotion.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <SDL3/SDL_opengl.h>
#include <map>
#include <string>
#include <vector>

namespace bongo_cat_neo {

class NativeModel final : public Csm::CubismUserModel {
public:
    NativeModel();
    ~NativeModel() override;
    bool load(const char *directory, const char *setting_file, bool direct_textures,
        BongoCatNeoError *error);
    bool load_textures(BongoCatNeoError *error);
    void release_render_resources();
    void resize(int width, int height);
    void reshape(int width, int height);
    bool update(float delta_seconds);
    void draw();
    void set_mirror(bool mirror);
    void set_dragging(float x, float y);
    void set_mver_compatibility(bool enabled);
    bool set_parameter(const char *id, float value);
    bool parameter(const char *id, float *minimum, float *maximum, float *value);
    bool start_motion(const char *group, int index);
    bool set_expression(int index);

private:
    using MotionMap = std::map<std::string, Csm::ACubismMotion *>;
    struct LockMotion {
        std::vector<int> parameters;
        std::vector<float> initial_values;
        bool enabled = false;
    };
    bool load_model(BongoCatNeoError *error);
    void load_expressions();
    void load_effects();
    void load_motions();
    void start_idle_motion();
    void load_lock_motion(const std::string &key,
        const std::vector<unsigned char> &bytes);
    bool toggle_lock_motion(const std::string &key, Csm::ACubismMotion *motion);
    void release_textures();
    void release_renderer();
    void bind_textures();
    std::vector<unsigned char> read(const std::string &path) const;
    std::string path(const char *relative) const;

    Csm::CubismModelSettingJson *setting_ = nullptr;
    MotionMap motions_;
    std::map<std::string, LockMotion> lock_motions_;
    MotionMap expressions_;
    std::vector<std::string> expression_names_;
    std::vector<GLuint> textures_;
    std::vector<float> parameter_snapshot_;
    std::vector<float> part_snapshot_;
    std::vector<float> pending_parameter_values_;
    std::vector<unsigned char> pending_parameters_;
    Csm::csmVector<Csm::CubismIdHandle> eye_blink_ids_;
    Csm::csmVector<Csm::CubismIdHandle> lip_sync_ids_;
    std::string directory_;
    int width_ = 612;
    int height_ = 354;
    int renderer_width_ = 0;
    int renderer_height_ = 0;
    bool motion_updated_ = false;
    bool mirror_ = false;
    bool direct_textures_ = false;
    bool external_parameters_dirty_ = false;
    bool mver_compatibility_ = false;
    std::vector<std::string> idle_motion_keys_;
    float opacity_snapshot_ = -1.0f;
};

} // namespace bongo_cat_neo

#endif
