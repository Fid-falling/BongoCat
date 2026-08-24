#include "cubism_model.hpp"

namespace bongo_cat {

bool NativeModel::prepare_cover() {
    if (!_model) return false;
    int parameter_count = _model->GetParameterCount();
    int part_count = _model->GetPartCount();
    for (const auto &item : motion_states_) {
        auto owner = motion_toggle_owners_.find(item.first);
        if ((owner != motion_toggle_owners_.end() &&
                owner->second != item.first) ||
            !motion_is_persistent(item.first)) continue;
        for (const MotionStateCurve &curve : item.second.curves) {
            if (curve.parameter >= 0 && curve.parameter < parameter_count)
                _model->SetParameterValue(curve.parameter, curve.end);
            else if (curve.part >= 0 && curve.part < part_count)
                _model->SetPartOpacity(curve.part, curve.end);
            else if (curve.model_opacity)
                _model->SetModelOpacity(curve.end);
        }
    }
    _model->Update();
    return true;
}

} // namespace bongo_cat
