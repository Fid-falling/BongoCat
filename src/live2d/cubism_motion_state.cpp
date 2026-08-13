#include "cubism_model.hpp"

#include <CubismFramework.hpp>
#include <Id/CubismIdManager.hpp>
#include <Model/CubismModel.hpp>
#include <algorithm>
#include <cmath>
#include <utility>
#include <yyjson.h>

namespace bongo_cat {

static bool curve_endpoints(yyjson_val *segments, float *start, float *end) {
    if (!yyjson_is_arr(segments) || yyjson_arr_size(segments) < 2) return false;
    yyjson_val *first = yyjson_arr_get(segments, 1);
    yyjson_val *last = yyjson_arr_get(segments, yyjson_arr_size(segments) - 1);
    if (!yyjson_is_num(first) || !yyjson_is_num(last)) return false;
    *start = (float)yyjson_get_num(first);
    *end = (float)yyjson_get_num(last);
    return true;
}

void NativeModel::load_motion_state(const std::string &key, const char *group,
    int motion_index, const std::vector<unsigned char> &bytes) {
    yyjson_doc *document = yyjson_read(
        reinterpret_cast<const char *>(bytes.data()), bytes.size(), 0);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : nullptr;
    yyjson_val *curves = yyjson_is_obj(root) ? yyjson_obj_get(root, "Curves") : nullptr;
    MotionState state; state.group = group ? group : "";
    state.index = motion_index;
    std::vector<std::string> targets;
    size_t index, count; yyjson_val *curve;
    if (yyjson_is_arr(curves)) yyjson_arr_foreach(curves, index, count, curve) {
        const char *target = yyjson_get_str(yyjson_obj_get(curve, "Target"));
        const char *id = yyjson_get_str(yyjson_obj_get(curve, "Id"));
        if (!target || !id) continue;
        targets.push_back(std::string(target) + ":" + id);
        MotionStateCurve value{target, id};
        if (curve_endpoints(yyjson_obj_get(curve, "Segments"),
            &value.start, &value.end) && std::fabs(value.start - value.end) > .0001f)
            state.curves.push_back(value);
    }
    if (document) yyjson_doc_free(document);
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    for (const std::string &target : targets) motion_signatures_[key] += target + '\n';
    std::sort(state.curves.begin(), state.curves.end(),
        [](const MotionStateCurve &a, const MotionStateCurve &b) {
            return a.target == b.target ? a.id < b.id : a.target < b.target;
        });
    motion_states_[key] = std::move(state);
}

static bool inverse(const NativeModel::MotionState &a,
    const NativeModel::MotionState &b) {
    if (a.group != b.group || a.curves.size() != b.curves.size() ||
        a.curves.empty()) return false;
    int first = std::min(a.index, b.index), second = std::max(a.index, b.index);
    if (first < 0 || (first & 1) || second != first + 1) return false;
    size_t reciprocal = 0;
    for (size_t i = 0; i < a.curves.size(); ++i) {
        const auto &left = a.curves[i], &right = b.curves[i];
        if (left.target != right.target || left.id != right.id) return false;
        bool same = std::fabs(left.start - right.start) <= .0001f &&
            std::fabs(left.end - right.end) <= .0001f;
        bool reversed = std::fabs(left.start - right.end) <= .0001f &&
            std::fabs(left.end - right.start) <= .0001f;
        if (!same && !reversed) return false;
        if (reversed && !same) {
            if (left.target != "Parameter") return false;
            reciprocal++;
        }
    }
    return reciprocal == 1;
}

static bool enabled_direction(Csm::CubismModel *model,
    const NativeModel::MotionState &a, const NativeModel::MotionState &b,
    bool *first_enabled) {
    for (size_t i = 0; i < a.curves.size(); ++i) {
        const auto &left = a.curves[i], &right = b.curves[i];
        bool same = std::fabs(left.start - right.start) <= .0001f &&
            std::fabs(left.end - right.end) <= .0001f;
        if (same) continue;
        auto id = Csm::CubismFramework::GetIdManager()->GetId(left.id.c_str());
        int parameter = model->GetParameterIndex(id);
        if (parameter < 0) return false;
        float normal = model->GetParameterDefaultValue(parameter);
        bool left_normal = std::fabs(left.end - normal) <= .0001f;
        bool right_normal = std::fabs(right.end - normal) <= .0001f;
        if (left_normal == right_normal) return false;
        *first_enabled = !left_normal;
        return true;
    }
    return false;
}

void NativeModel::pair_motion_states() {
    if (!render_options_.mver_projection) return;
    for (auto first = motion_states_.begin(); first != motion_states_.end(); ++first) {
        if (first->second.group != "CAT_motion_lock") continue;
        if (motion_toggle_owners_.find(first->first) != motion_toggle_owners_.end()) continue;
        auto second = first; ++second;
        for (; second != motion_states_.end(); ++second) {
            if (motion_toggle_owners_.find(second->first) != motion_toggle_owners_.end() ||
                motion_signatures_[first->first] != motion_signatures_[second->first] ||
                !inverse(first->second, second->second)) continue;
            bool first_enabled = false;
            if (!enabled_direction(_model, first->second, second->second,
                &first_enabled)) continue;
            const std::string &owner = first_enabled ? first->first : second->first;
            const std::string &partner = owner == first->first ? second->first : first->first;
            motion_toggle_partners_[owner] = partner;
            motion_toggle_owners_[owner] = motion_toggle_owners_[partner] = owner;
            break;
        }
    }
}

std::string NativeModel::motion_to_play(const std::string &key,
    bool *selected) const {
    auto owner = motion_toggle_owners_.find(key);
    const std::string &canonical = owner == motion_toggle_owners_.end() ?
        key : owner->second;
    auto partner = motion_toggle_partners_.find(canonical);
    bool active = selected_motion_keys_.find(canonical) != selected_motion_keys_.end();
    if (selected) *selected = partner == motion_toggle_partners_.end() || !active;
    return active && partner != motion_toggle_partners_.end() ?
        partner->second : canonical;
}

void NativeModel::select_motion(const std::string &key, bool selected) {
    auto owner = motion_toggle_owners_.find(key);
    const std::string &canonical = owner == motion_toggle_owners_.end() ?
        key : owner->second;
    if (!selected) { selected_motion_keys_.erase(canonical); return; }
    const std::string &signature = motion_signatures_[canonical];
    for (auto item = selected_motion_keys_.begin();
        item != selected_motion_keys_.end();) {
        auto existing = motion_signatures_.find(*item);
        if (!signature.empty() && existing != motion_signatures_.end() &&
            existing->second == signature) item = selected_motion_keys_.erase(item);
        else ++item;
    }
    selected_motion_keys_.insert(canonical);
}

} // namespace bongo_cat
