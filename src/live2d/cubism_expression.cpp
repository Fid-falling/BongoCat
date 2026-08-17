#include "cubism_model.hpp"

#include <Motion/CubismExpressionMotionManager.hpp>
#include <Motion/CubismMotionQueueEntry.hpp>

namespace bongo_cat {

bool NativeModel::set_expression(int index) {
    if (index == -1) {
        expression_index_ = -1;
        auto *entries = _expressionManager->GetCubismMotionQueueEntries();
        expression_clearing_ = entries && entries->GetSize() > 0;
        if (!expression_clearing_) {
            _expressionManager->StopAllMotions();
            return true;
        }
        for (Csm::csmUint32 i = 0; i < entries->GetSize(); ++i) {
            Csm::CubismMotionQueueEntry *entry = entries->At(i);
            if (!entry) continue;
            Csm::ACubismMotion *motion = entry->GetCubismMotion();
            if (motion) {
                float fade_out = motion->GetFadeOutTime();
                entry->SetFadeout(fade_out < 0.0f ? 0.0f : fade_out);
            }
        }
        return true;
    }
    if (index < 0 || (size_t)index >= expression_names_.size()) return false;
    auto found = expressions_.find(expression_names_[(size_t)index]);
    if (found == expressions_.end()) return false;
    Csm::CubismMotionQueueEntryHandle handle = _expressionManager->StartMotion(
        found->second, false);
    if (handle == Csm::InvalidMotionQueueEntryHandleValue) return false;
    expression_clearing_ = false;
    expression_index_ = index;
    return true;
}

void NativeModel::expire_expression_fade() {
    if (!expression_clearing_) return;
    auto *entries = _expressionManager->GetCubismMotionQueueEntries();
    if (!entries || entries->GetSize() == 0) {
        expression_clearing_ = false;
        return;
    }
    for (Csm::csmUint32 i = 0; i < entries->GetSize(); ++i) {
        Csm::CubismMotionQueueEntry *entry = entries->At(i);
        if (entry && (entry->GetEndTime() < 0.0f ||
            entry->GetStateTime() < entry->GetEndTime())) return;
    }
    _expressionManager->StopAllMotions();
    expression_clearing_ = false;
}

} // namespace bongo_cat
