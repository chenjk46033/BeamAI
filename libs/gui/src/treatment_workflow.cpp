#include "gui/treatment_workflow.hpp"

#include <stdexcept>

namespace beam::gui {
namespace {

constexpr std::size_t toIndex(WorkflowStage stage) { return static_cast<std::size_t>(stage); }

}  // namespace

TreatmentWorkflow::TreatmentWorkflow() {
    stages_[index(WorkflowStage::CaseSetup)].status = WorkflowStatus::InProgress;
}

const WorkflowStageState& TreatmentWorkflow::state(WorkflowStage stage) const {
    if (stage == WorkflowStage::Count) throw std::out_of_range("WorkflowStage::Count is not a real stage");
    return stages_[index(stage)];
}

bool TreatmentWorkflow::canBegin(WorkflowStage stage, std::string* reason) const {
    if (stage == WorkflowStage::Count) {
        if (reason != nullptr) *reason = "Unknown workflow stage.";
        return false;
    }
    for (std::size_t i = 0; i < index(stage); ++i) {
        if (stages_[i].status != WorkflowStatus::Complete) {
            if (reason != nullptr) {
                *reason = std::string(workflowStageName(static_cast<WorkflowStage>(i))) +
                          " must be complete first.";
            }
            return false;
        }
    }
    return true;
}

bool TreatmentWorkflow::begin(WorkflowStage stage, std::string* reason) {
    if (!canBegin(stage, reason)) return false;
    WorkflowStageState& current = stages_[index(stage)];
    current.status = WorkflowStatus::InProgress;
    current.message.clear();
    return true;
}

bool TreatmentWorkflow::complete(WorkflowStage stage, std::string* reason) {
    if (!canBegin(stage, reason)) return false;
    WorkflowStageState& current = stages_[index(stage)];
    current.status = WorkflowStatus::Complete;
    current.message.clear();
    return true;
}

void TreatmentWorkflow::block(WorkflowStage stage, std::string message) {
    if (stage == WorkflowStage::Count) throw std::out_of_range("WorkflowStage::Count is not a real stage");
    WorkflowStageState& current = stages_[index(stage)];
    current.status = WorkflowStatus::Blocked;
    current.message = std::move(message);
}

void TreatmentWorkflow::change(WorkflowStage stage, std::string reason) {
    if (stage == WorkflowStage::Count) throw std::out_of_range("WorkflowStage::Count is not a real stage");
    WorkflowStageState& changed = stages_[index(stage)];
    changed.status = WorkflowStatus::InProgress;
    changed.message.clear();

    for (std::size_t i = index(stage) + 1; i < kStageCount; ++i) {
        if (stages_[i].status != WorkflowStatus::NotStarted) {
            stages_[i].status = WorkflowStatus::Invalidated;
            stages_[i].message = reason;
        }
    }
}

bool TreatmentWorkflow::canStartTreatment(std::string* reason) const {
    if (stages_[index(WorkflowStage::SafetyReview)].status != WorkflowStatus::Complete) {
        if (reason != nullptr) *reason = "Final safety review must be complete before treatment.";
        return false;
    }
    return canBegin(WorkflowStage::Treatment, reason);
}

WorkflowStage TreatmentWorkflow::nextStage() const {
    for (std::size_t i = 0; i < kStageCount; ++i) {
        if (stages_[i].status != WorkflowStatus::Complete) return static_cast<WorkflowStage>(i);
    }
    return WorkflowStage::Report;
}

std::string_view workflowStageName(WorkflowStage stage) {
    constexpr std::array<std::string_view, toIndex(WorkflowStage::Count)> names = {
        "Case setup",       "System check",  "Imaging",   "Registration", "Coupling",
        "Correction",       "Treatment plan", "Safety review", "Treatment",    "Report"};
    if (stage == WorkflowStage::Count) return "Unknown";
    return names[toIndex(stage)];
}

std::string_view workflowStatusName(WorkflowStatus status) {
    switch (status) {
        case WorkflowStatus::NotStarted: return "Not started";
        case WorkflowStatus::InProgress: return "In progress";
        case WorkflowStatus::Complete: return "Complete";
        case WorkflowStatus::Invalidated: return "Invalidated";
        case WorkflowStatus::Blocked: return "Blocked";
    }
    return "Unknown";
}

}  // namespace beam::gui
